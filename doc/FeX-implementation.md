**FeX — The Modern-Syntax Veneer over the 2025 *fe* Core**
*A companion to “Fe / FeX — A Tiny Embeddable Language (2025 Edition)”*

---

### 0 · Why this addendum exists

The core document tells the full story of the **2025 fe runtime**.
What it does not cover is **how FeX layers a contemporary, statement-oriented surface over that runtime**—turning parenthesis-heavy s-expressions into something that feels closer to a small-talking JavaScript or Lua. This paper is that missing half: it dissects FeX’s lexer, parser, span-aware error system, and the handful of builtin functions that make the new syntax pleasant inside a C host.

---

## 1 · Bird’s-eye view

1. **`fex_init`**
   Registers an error hook plus *print / println*—the only C builtins FeX adds.
2. **`fex_compile`**
   *Lex -> Pratt parse -> produce a **pure fe AST*** (no private node kinds).
   Every AST cons cell is immediately recorded in a **span table** that maps it back to *line/column* pairs inside the original source buffer.
3. **`fex_do_string`**
   Convenience: *compile*, *eval*, *pop GC frame* in one call.

Because the result of `fex_compile` is 100 % vanilla *fe* s-experssion, **the evaluator, garbage collector, closure machinery, module loader \&c. remain untouched**. FeX is therefore *zero-cost* once code is in AST form—the host pays only during compilation.

---

## 2 · Lexical layer: from text to tokens

*Lexer `scan_token()`* is a minimal, single-pass DFA:

| Category        | Examples                                                            | Notes                                                                                             |
| --------------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| **Single-char** | `(` `)` `{` `}` `[` `]` `,` `.` `+` `-` `*` `/` `;`                 | One-byte look-ups.                                                                                |
| **Dual-char**   | `==` `!=` `<=` `>=`                                                 | `match()` peeks ahead.                                                                            |
| **Literals**    | strings, numbers                                                    | Numbers are scanned as *double* text; final fixnum check happens later.                           |
| **Keywords**    | `fn let return module import export while if and or true false nil` | `identifier_type()` turns raw identifiers into keyword tokens with a perfect-hash-style `switch`. |

A single global `Lexer L` keeps track of **line starts** so that we can compute `column = start – line_start + 1` for span recording.

---

## 3 · Pratt parser: modern syntax, lisp heart

FeX’s grammar is Pratt-expressed through one static `rules[]` table:

```c
ParseRule rules[TOKEN_EOF+1] = {
  [TOKEN_LPAREN] = {parse_grouping, INFIX_MARKER, PREC_CALL},
  /* ... */
  [TOKEN_FN]     = {fn_expression , NULL,         PREC_NONE},
};
```

* **Prefix functions** build leaf expressions (`parse_number`, `parse_string`, `fn_expression`...).
* **Infix marker** is the trick: a `NULL` *function* ≠ no rule; instead a *non-NULL* means “operator present but its logic is in the generic loop”.
* Binding powers (`PREC_*`) follow Lua/JS tradition.

### 3.1 AST shape conventions

The output is always an *fe list* whose **head is a symbol naming the operator**. Examples:

| Source               | FeX AST              | fe core form encountered by the evaluator          |
| -------------------- | -------------------- | -------------------------------------------------- |
| `a + b`              | `(+ a b)`            | primitive `+`                                      |
| `x = y`              | `(= x y)`            | special form `=` (becomes `let` in var-decl sugar) |
| `[1,2,3]`            | `(list 1 2 3)`       | plain variadic function call                       |
| `fn (x,y) { x + y }` | `(fn (x y) (+ x y))` | new 2025 closure compiler                          |

Therefore **no interpreter changes were required**—only symbols and lists the old core already understands.

### 3.2 Declarations become sugar for core primitives

```text
let   x = 42;         ->  (= x 42)    ;; fe’s "let"
fn    add(a,b){...}     ->  (= add (fn (a b) ...))
export let x = 1;     ->  (export (= x 1))
import math;          ->  (import math)
module("m"){ ... }      ->  (module "m" ...)
```

Notice that `export` is parsed as a *unary operator* whose right-hand side is whatever it decorates. This keeps the desugar pipeline elegant and orthogonal.

---

## 4 · Span table: precise source mapping inside a moving GC

`fex_span.c` implements a **pointer-keyed open-addressing hash table** with 8192 buckets:

```c
void fex_record_span(const fe_Object* node,
                     const char* src,
                     int sline, scol, eline, ecol);
```

* **Key** The *exact* address of the cons cell—the object never moves because *fe* uses a *non-compacting* mark-and-sweep GC.
* **Value** Start/end line · column plus the original buffer pointer (useful for REPL slices).

Each consing helper `CONS1` wraps `fe_cons` and *immediately* records a span, guaranteeing a 1:1 mapping. The overhead is one `malloc` per AST node, which is acceptable for scripts; if your host is memory-starved you can plug in a bump allocator replacement.

### 4.1 Error printing

`fex_on_error` overrides `fe_handlers(ctx)->error`. It walks the *call stack list* provided by the core, looks up every node’s span, and prints an annotated trace:

```
error: division by zero
[0] <string>:12:7  =>  x / 0
[1] <string>:8:5   =>  println(add(1,2) / 0)
```

Where no span is found (e.g., an older macro expanded list) it falls back to `fe_tostring`.

---

## 5 · Runtime embellishments

Only two new C-level functions are injected:

```c
(print . args)     ;; writes objects, no newline
(println . args)   ;; idem, then prints '\n'
```

Because they are *ordinary cfunc objects* the host can replace, shadow, or delete them like any other global binding.

---

## 6 · Interfacing with the host C program

The public header **`fex.h`** exposes three calls:

```c
void       fex_init(fe_Context*);
fe_Object* fex_compile(fe_Context*, const char* src);
fe_Object* fex_do_string(fe_Context*, const char* src);
```

A canonical embedding looks like:

```c
char buf[64*1024];
fe_Context* L = fe_open(buf, sizeof buf);
fex_init(L);

fex_do_string(L,
  "module(\"demo\"){\n"
  "  fn fac(n){ if(n<=1) return 1; return n*fac(n-1); }\n"
  "  println(fac(10));\n"
  "}"
);
```

No additional headers or libraries beyond the stock *fe* ones are required.

---

## 7 · Extending FeX

Because the **front-end is self-contained** you can evolve the language without touching GC or evaluator logic:

* **Add operators** Insert a token, extend `rules[]`, and map it to an existing primitive or macro.
* **Add keywords** Upgrade `identifier_type()`; point the rule at your syntax sugar function.
* **Custom debug info** Replace `fex_span.*` with dwarf emission or any schema you like—the core sees just cons cells.

For heavier alterations—pattern matching, algebraic data types, etc.—you still translate to the *fe* core forms. The moment you need a runtime change, edit `fe.c` instead and keep FeX a pure compiler layer.

---

## 8 · Known trade-offs specific to FeX

* **One AST node = one `malloc`ed span record**
  Fine for configs and scripting, but you might pool-allocate or truncate spans in‐production.
* **No macro system (yet)**
  Fe’s hygienic macros still operate at s-exp level. A future “typed quasi-quote” syntax could compile to that.
* **Error recovery is statement-level**
  The parser’s `synchronize()` skips until the next semicolon or brace. REPL friendliness is good, but not perfect.
* **Big-endian machines** inherit the integer-tag caveat from the core.

---

## 9 · Closing reflection

FeX exemplifies **systems thinking at different zoom levels**:

*Zoom in* A span is four `int`s and a pointer; a Pratt rule is three plain fields.
*Zoom out* Together they form a language in which “`println map(list,fn(x){x+1})`” can be embedded inside firmware without a single dynamic allocation escaping the arena.

By cleanly **separating *syntax* (FeX) from *semantics* (fe core)**, the project maintains the 1 kLoC readability promise while letting you experiment with syntactic sugar as liberally as you please. Study it, fork it, or simply drop it into your next tool—the whole thing still fits in a weekend brain-load.
