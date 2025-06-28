# FeX Programming Language

*A tiny, modern-syntax, self-hosting language with a two-kilobyte ANSI C runtime.*

---

## 1 A Bird’s-Eye View

FeX (pronounced *“fecks”*) asks a simple question:

> **What is the smallest curly-brace language that still feels “grown-up”?**

The answer turned out to be a handful of grammar rules, an immutable cons-cell heap, and a dash of syntactic sugar. Feed the sugar to the *fex* compiler and you obtain portable C objects ready to run in any embedding—desktop, microcontroller, or web assembly alike.

With FeX:

* **Everything is an expression.** Every construct returns a value, so REPL play feels natural.
* **Homoiconicity re-imagined.** Source code compiles straight into the same pair objects your programs manipulate, enabling powerful metaprogramming without macros that mutate global state.
* **Host-agnostic.** A single call `fex_do_string(ctx, source)` interprets code inside your application; or pre-compile once, ship the AST, and evaluate it many times with `fe_eval`.

---

## 2 Lexical Grammar — Quick Reference

| Category        | Example                                                                                                         | Notes                                                         |
| --------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **Identifiers** | `snake_case`, `Point3D`, `_private`                                                                             | Unicode letters allowed.                                      |
| **Keywords**    | `module`, `export`, `import`, `let`, `fn`, `return`, `if`, `else`, `while`, `true`, `false`, `nil`, `and`, `or` | Lower-case; reserved.                                         |
| **Numbers**     | `42`, `-3`, `0xFF`, `3.14`, `6.022e23`                                                                          | Hex prefixed with `0x`; decimals may use scientific notation. |
| **Strings**     | `"hello\nworld"`                                                                                                | Double quotes; C-style escapes.                               |
| **Comments**    | `// until end-of-line`                                                                                          | Ignored by compiler.                                          |
| **Punctuation** | `()[]{},;.`                                                                                                     | Familiar C-family set.                                        |

Whitespace separates tokens and terminates single-line comments; otherwise insignificant.

---

## 3 Literals & Core Data Shapes

FeX has exactly one mutable heap object—**the pair** `(car . cdr)`. Every other structure is a pattern layered on top.

| Literal | Example                   | Behaviour                                                 |
| ------- | ------------------------- | --------------------------------------------------------- |
| `nil`   | `nil`                     | Singleton object, **falsey**, also the empty list.        |
| Boolean | `true`, `false`           | Only `false` and `nil` are false.                         |
| Fixnum  | `123`                     | Pointer-tagged small integer, fits native word width.     |
| Double  | `3.14`                    | Boxed on heap if not representable as fixnum.             |
| String  | `"FeX"`                   | Rope of 7-byte segments; zero-copy slices.                |
| List    | `[1, 2, 3]`               | Syntactic sugar for pairs: equivalent to `list(1, 2, 3)`. |
| Symbol  | Implicit from identifiers | Interned globally, pointer equality.                      |

> **Thought experiment:**
> If you took away the list literal syntax, you could still build arrays, hash-tables, and ASTs—just cons them together.

---

## 4 Expressions & Operators

FeX borrows JavaScript-style precedence but **evaluates strictly left-to-right**.

| Category | Example            | Internal Form\*                                                  |
| -------- | ------------------ | ---------------------------------------------------------------- |
| Primary  | `x`, `42`, `"hi"`  | –                                                                |
| Grouping | `(expr)`           | `(expr)`                                                         |
| List     | `[a, b]`           | `(list a b)`                                                     |
| Unary    | `-n`, `!cond`      | `(- n)`, `(not cond)`                                            |
| Binary   | `a + b * c`        | Nested `(+ ...)` `(* ...)`                                           |
| Equality | `a == b`, `a != b` | `(is a b)`, `(not (is a b))`                                     |
| Compare  | `< <= > >=`        | `<` and `<=` as-is; `>` flips operands into `<`, `>=` into `<=`. |
| Logical  | `and`, `or`        | Short-circuiting `(and ...)` / `(or ...)`                            |
| Assign   | `x = y`            | `(= x y)`                                                        |
| Call     | `fn(a, b)`         | `(fn a b)`                                                       |
| Member   | `obj.field`        | `(get obj 'field)`                                               |

\*Internal forms are shown to illustrate semantics; you rarely see them unless you inspect the runtime AST.

---

## 5 Statements

Statements are expressions with a semicolon—or blocks that delimit themselves.

| Syntax                       | Purpose                                                      | Notes                                                                       |
| ---------------------------- | ------------------------------------------------------------ | --------------------------------------------------------------------------- |
| `expr ;`                     | Evaluate for side-effects.                                   | Value is discarded unless at REPL.                                          |
| `let x = expr ;`             | Bind fresh local (inside function) or global (at top level). | Recursion works: the binding exists while `expr` is evaluated.              |
| `fn name(params) { body }`   | Named function declaration.                                  | Functions are first-class values.                                           |
| `return expr ;`              | Exit nearest function, yielding `expr`.                      | If omitted, returns `nil`.                                                  |
| `if (cond) stmt1 else stmt2` | Conditional; else is optional.                               | Both branches are expressions, so they can appear inside other expressions. |
| `while (cond) stmt`          | Loop while `cond` remains truthy.                            | Tail-call optimisation is not required—plain loops are faster.              |
| `{ ... }`                      | Block; result is last inner expression.                      | Enables let-scoped temporaries.                                             |

---

## 6 Functions & Closures

### 6.1 Literal Syntax

```fex
let add = fn (x, y) { x + y };
println(add(2, 3));  // -> 5
```

Parameters are positional. A missing argument becomes `nil`; extras are ignored.

### 6.2 Capturing Rules

Free variables are captured **by reference**, producing an *up-value list* stored in the function object. Mutating the outer binding mutates what the closure observes.

### 6.3 `return`

`return` unwinds directly to the most recent function frame—no extra C stack is introduced—so it is cheap and deterministic.

---

## 7 Modules

```fex
module ("math") {
  export fn square(n) { n * n }
  export let pi = 3.14159;
}

import math;

println(math.square(9)); // 81
println(math.pi);        // 3.14159
```

* **`module("name") { ... }`** – Executes body once; its export table is a plain association list and is registered globally under `'name'`.
* **`export decl`** – Any `let` or `fn` may be marked for export inside a module.
* **`import ident ;`** – Compile-time directive; at run-time you access members with dot syntax (`math.pi`) or `get`.

Because a module is just a list of `(symbol . value)` pairs, you can create alternative loaders—lazy, remote, or version-pinned—in FeX itself.

---

## 8 Standard Library Snapshot

All primitives live in the global environment; add your own with `fe_cfunc`.

| Name                    | Signature                    | Behaviour                                       |
| ----------------------- | ---------------------------- | ----------------------------------------------- |
| `print`                 | `v₁, ... vₙ -> nil`             | Writes values separated by spaces (no newline). |
| `println`               | `v₁, ... vₙ -> nil`             | Same as `print` plus trailing newline.          |
| Arithmetic & comparison | `+ - * / < <=` etc.          | Operate on numbers (fixnum or double).          |
| List ops                | `list`, `car`, `cdr`, `cons` | Standard pair manipulation.                     |
| Predicates              | `atom`, `is`, `not`          | Truthiness as described earlier.                |

Feel free to shadow or extend any of these in user space.

---

## 9 Execution & Memory at a Glance

* **Evaluation strategy** – Eager, left-to-right. Special forms (`if`, `and`, `or`, `fn`, etc.) control when their arguments are evaluated.
* **Garbage collection** – Incremental mark-and-sweep; pairs never move, so native pointers into the heap stay valid.
* **Truthiness** – Only `false` and `nil` are false. Zero, empty strings, and empty lists are *true*.
* **Tail calls** – Ordinary function calls use the C stack; expect ≈ 1000 recursive levels on typical systems. Prefer `while` for unbounded loops.
* **Error handling** – Default handler prints a back-trace annotated with source spans. Override `fe_handlers(ctx)->error` to integrate with your host.

---

## 10 Embedding Cheat-Sheet

| Goal                             | API Call                                                           |
| -------------------------------- | ------------------------------------------------------------------ |
| **Open VM**                      | `fe_open(buf, size); fex_init(ctx);`                               |
| **Run source once**              | `fex_do_string(ctx, "code");`                                      |
| **Compile, then run many times** | `fe_Object* ast = fex_compile(ctx, src); fe_eval(ctx, ast);`       |
| **Expose C function**            | `fe_set(ctx, fe_symbol(ctx,"read_file"), fe_cfunc(ctx,&my_c_fn));` |
| **Custom error spans**           | Provide your own `fe_Handlers.error` before `fex_init`.            |

The compiler is **re-entrant**; you may compile on one thread, ship the AST to another, and evaluate there—objects never relocate.

---

## 11 Idiomatic Patterns

* **Pipelines**

  ```fex
  [1,2,3]
    .map(fn (x){ x + 1 })
    .filter(fn (x){ x > 2 });
  ```

* **Domain objects as tables**

  ```fex
  module ("vec2") {
    export fn dot(a, b) { a.x * b.x + a.y * b.y }
    export fn len(v)   { sqrt(dot(v, v)) }
  }
  ```

* **Live REPL**

  Every block returns its last value, so you can paste any snippet and immediately see the result without wrapping it in `print`.

---

## 12 Glossary

> **Pair** – Two-slot heap object `(car . cdr)`; the only mutable container.
> **Fixnum** – Pointer-tagged signed integer stored in one machine word.
> **Special form** – Operator whose arguments are *not* automatically evaluated.
> **Up-value** – Closed-over binding carried inside a function object.
> **Homoiconic** – Code and data share the same representation; the compiler rearranges concrete syntax around identical pair structures.

---

### One Last Thought

Languages often grow by accretion—keywords piled on keywords. FeX chooses subtraction: start with a core small enough to *see in full*, then add syntax that makes newcomers feel at home without taking away the power to reshape the language from within.

May your programs be **small, sharp, and joyful**.
