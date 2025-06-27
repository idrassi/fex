# **Fe Core Language — 2025 Edition**

*A tiny, self-contained, Lisp-dialect designed for embedding (MIT-licensed, ANSI C implementation).*

---

## 1. Conceptual Orientation

Fe is a homoiconic, expression-oriented language that occupies the sweet spot between “small enough to understand in one sitting” and “powerful enough to build a sizeable system.” Every runtime value is an *object* managed by an incremental, non-moving garbage collector; integers are stored as *immediate* “fixnums,” while other objects live on the heap. Evaluation follows a few simple rules:

1. **Atoms** (numbers, strings, booleans, symbols, `nil`) evaluate to themselves.
2. **Pairs** (the cons-cell backbone of code and data) are treated as *calls*:
   the `car` is evaluated to obtain a *function, macro,* or *special form*; the `cdr` supplies the raw arguments.
3. Special forms define their own evaluation strategy.
4. Everything that is not `false` (`false` literal) or `nil` is *truthy*.

With that framing, the rest of the language fits in one table-top poster.

---

## 2. Primitive Data & Syntax

| Literal                | Example                 | Notes                                                                                              |
| ---------------------- | ----------------------- | -------------------------------------------------------------------------------------------------- |
| **Nil**                | `nil`                   | Unique “empty / falsey” object.                                                                    |
| **Booleans**           | `true`, `false`         | Only `false` and `nil` are false.                                                                  |
| **Fixnums** (integers) | `42`, `-3`              | Stored as tagged immediates; range = one machine word minus one bit.                               |
| **Doubles**            | `3.14`, `6.022e23`      | Boxed when necessary; arithmetic transparently mixes the two.                                      |
| **String**             | `"hello"`               | Internally chunked into 7-byte blocks; quotes and backslashes can be escaped in the usual C‐style. |
| **Symbol**             | `x`, `+`, `my/function` | Interned globally; equality is pointer equality.                                                   |
| **Pair / List**        | `'(a . b)`, `'(1 2 3)`  | Constructed with `cons`, deconstructed with `car` / `cdr`. Quote abbreviates `(quote ...)`.          |

```clojure
> '(1 . 2)        ; dotted pair
(1 . 2)
> '(1 2 3)        ; proper list
(1 2 3)
```

---

## 3. Special Forms (the “syntax” of the language)

Special forms shape evaluation; they *must* appear in operator position.

| Form                                        | Purpose                                                                                                                                                                       | Sketch & Example                                                    |
| ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| **`(let sym expr)`**                        | Bind a **new** variable in the current lexical environment; returns the value. Recursive definitions are allowed because the symbol is introduced before `expr` is evaluated. | `(let factorial (fn (n) (if (< n 2) 1 (* n (factorial (- n 1))))))` |
| **`(= sym expr)`**                          | Mutate an **existing** binding (walks outward through lexical frames, falling back to the global table). Creates a global if none exists.                                     | `(= counter (+ counter 1))`                                         |
| **`(if cond then [cond2 then2 ...] [else])`** | Multi-branch conditional. Each test is evaluated left-to-right; the body attached to the first truthy condition is run. Optional “dangling” expression acts as `else`.        | `(if (is n 0) "zero" (is n 1) "one" "many")`                        |
| **`(fn (params) body...)`**                   | Create a *function* (a first-class closure). Parameters are symbols or dotted pairs for variadics. Body executes in a fresh lexical frame that closes over free variables.    | `(fn (x y) (+ x y))`                                                |
| **`(mac (params) body...)`**                  | Like `fn`, but produces a **macro**—its arguments arrive *unevaluated*; the macro returns a new AST which is spliced back and evaluated.                                      | `(mac (x) (list '+ 1 x))`                                           |
| **`(while cond body...)`**                    | Standard pre-test loop.                                                                                                                                                       | `(while (< i 10) (print i) (= i (+ i 1)))`                          |
| **`(quote expr)`** *(abbrev. `'expr`)*      | Return the expression verbatim without evaluation.                                                                                                                            | `'(1 2 3)`                                                          |
| **`(and a b c...)`**                          | Logical “and”; short-circuits on first falsey value, otherwise returns last value.                                                                                            |                                                                     |
| **`(or a b c...)`**                           | Logical “or”; short-circuits on first truthy value, otherwise returns `nil`.                                                                                                  |                                                                     |
| **`(do expr...)`**                            | Evaluate each expression in sequence, return last value. Frequently used to build block-structured code inside `fn`/`mac`.                                                    |                                                                     |
| **`(return expr)`**                         | Immediately exit the current function, yielding `expr`. Has no effect outside a function.                                                                                     |                                                                     |
| **Module system**                           | See next subsection.                                                                                                                                                          |                                                                     |

### 3.1 Modules, Exports, Imports, and Dynamic Lookup

Fe’s module mechanism is deliberately minimal but powerful enough for componentisation.

* **`(module "name" body...)`**
  Creates an isolated export table, runs `body` inside the caller’s environment, then interns the table under global symbol `"name"` (converted to a symbol). The module evaluates to its own table.

* **`(export (let sym expr))`**
  Must appear inside a `module`; evaluates the declaration (thereby installing a *local* binding) **and** records the binding in the module’s export table. Returns the exported value.

* **`(import table-sym)`**
  Currently a no-op placeholder; future reader macros can expand it into `let`s that copy names out of the module’s table.

* **`(get table symbol)`**
  Runtime associative lookup. `table` is any alist of `(sym . value)` pairs (including a module table); `symbol` is *not* evaluated. Returns `nil` if absent.

```clojure
(module "math"
  (export (let square (fn (n) (* n n))))
  (export (let pi 3.14159)))

(get math 'pi)          ; -> 3.14159
((get math 'square) 9)  ; -> 81
```

---

## 4. Library Primitives (functions, not forms)

These are defined in C, so they evaluate all arguments left-to-right before executing.

| Function                                 | Purpose                                                                                          | Comment |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------ | ------- |
| `cons`, `car`, `cdr`, `setcar`, `setcdr` | Classical list primitives.                                                                       |         |
| `list`                                   | Pack its arguments into a proper list.                                                           |         |
| `not`                                    | `true` ↦ `false`, everything else ↦ `false`.                                                     |         |
| `is`                                     | Value equality (`number` and `string` compare by contents; everything else by pointer identity). |         |
| `atom`                                   | True if argument is *not* a pair.                                                                |         |
| `print`                                  | Writes each argument, separated by spaces, followed by newline.                                  |         |
| `+`, `-`, `*`, `/`                       | Variadic arithmetic over numbers (fixnum and double mix freely). `/` is left-associative.        |         |
| `<`, `<=`                                | Binary numeric comparisons; return `true` or `false`.                                            |         |

---

## 5. Execution Model & Scoping

1. **Lexical closure** – `fn` and `mac` capture *free variables* by reference; mutation through `=` is visible to every closure sharing that environment.
2. **`let` builds the local environment** by **extending** the current frame; recursive definitions work because the placeholder cell is allocated first.
3. **Tail position** in `do` or function bodies is not specially optimised in this reference implementation, but the evaluator is iterative enough for practical programs on moderate stack depth.
4. **Return handling** is implemented by throwing a hidden pair `'(return . value)` up the call chain until the enclosing `fn` intercepts it.

---

## 6. Truthiness, Errors, and Edge-Cases

* Only `false` and `nil` are false—everything else, including `0` and empty strings, is true.
* Type errors abort evaluation via `fe_error`; the default handler prints a back-trace and terminates the process. Embedders may supply their own hooks (`fe_handlers`).
* Garbage collection is mark-and-sweep, triggered adaptively by allocation count; most C-resident pointers never move, so foreign data structures remain valid.

---

## 7. Idiomatic Patterns

> *“A language that fits in your head invites you to extend it.”*

* **Generic loops**: `(while cond (do ...))` or `(for ...)` built as a macro.
* **Named-let recursion**:

  ```clojure
  (let fib
    (fn (n)
      (if (< n 2) n
          (+ (fib (- n 1)) (fib (- n 2))))))
  ```
* **Domain-specific syntax** through `mac`—e.g. pattern matching, async pipelines, etc.
* **REPL-driven development**: use `let` for new globals during experimentation; convert to `module`/`export` when you commit.

---

## 8. Embedding Checklist

| Need                          | API                                                                                                       |
| ----------------------------- | --------------------------------------------------------------------------------------------------------- |
| Initialise                    | `fe_open(memoryBlock, size)`                                                                              |
| Evaluate code                 | `fe_eval(ctx, fe_read(ctx, reader, udata))`                                                               |
| Call Fe from C                | Store a `fe_Object*` to a function, then build an argument list with `fe_list` and call it via `fe_eval`. |
| Call C from Fe                | Expose C-side primitive with `fe_cfunc`; assign it to a symbol using `fe_set`.                            |
| Custom I/O or error reporting | Fill `fe_handlers` (`error`, `mark`, `gc`).                                                               |

---

### *Closing Thought*

Fe’s entire user-visible semantic surface fits in this document; its implementation—under 2 000 lines of portable C—can be read in an afternoon. Use it as a bootstrapping macro-assembler for ideas: start with a DSL, grow into an application language, or embed it as your configuration/runtime layer. Above all, enjoy the freedom of comprehensibility.
