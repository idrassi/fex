**FeX - Front-End And Integration Notes**

*A focused companion to the lower-level runtime notes.*

---

## 1. What FeX adds

The `fe` runtime is the evaluator, GC, object model, closure machinery, and low-level module/import execution.
FeX adds the parts that make that runtime pleasant to use in products and pipelines:

- a C-like surface syntax
- file/module ergonomics
- span-aware diagnostics
- recoverable compile/eval wrappers
- optional builtin groups for data, filesystem, and process automation

---

## 2. Compilation pipeline

At a high level, FeX does:

1. lex source text into tokens
2. parse with a Pratt parser
3. lower the result into ordinary `fe` ASTs
4. optionally record source spans for diagnostics
5. hand the AST to the core evaluator

The output of `fex_compile()` is still a normal `fe` tree, not a separate bytecode or VM-specific IR.

---

## 3. Surface syntax lowering

Representative examples:

- `let x = 1;` -> `(let x 1)`
- `if (cond) { a; } else { b; }` -> `(if cond a b)`
- `fn add(a, b) { return a + b; }` -> a named binding plus an `fn` form
- `pair.head` -> `(get pair head)`
- `pair.head = 10` -> `(setcar pair 10)`
- `obj.key = value` -> `(put obj key value)`

Named function declarations are rewritten before evaluation so the core only needs to understand its existing forms.

---

## 4. Parser and diagnostics

The front-end keeps enough source information to produce useful compile and runtime errors.

When span tracking is enabled:

- AST nodes are associated with source name, line, and column data
- runtime trace frames can show the FeX expression that triggered the failure
- the recoverable `fex_try_*` APIs can return structured errors instead of terminating the process

The parser's recovery strategy is intentionally simple: it synchronizes at statement boundaries such as semicolons and braces.

---

## 5. Modules and imports

FeX supports both explicit modules and file-backed imports.

### 5.1 Explicit modules

`module("name") { ... }` creates and returns an export map and binds it under `name`.

### 5.2 File imports

`import` supports:

- bare names such as `import settings;`
- dotted package names such as `import feature.helper;`
- string paths such as `import "./local_helper";`

Resolution order:

1. importing file directory
2. configured import paths
3. current working directory

Within each root, FeX tries both `name.fex` and `name/index.fex`.
Import specifiers containing `..` path components are rejected.

Imported files execute with an implicit export map, so top-level `export let` and `export fn` populate the imported module directly.

---

## 6. Recoverable execution

For embedding, the important APIs are:

- `fex_try_compile`
- `fex_try_eval`
- `fex_try_do_string`
- `fex_try_do_file`

These wrappers keep failures in-process and report:

- compile, runtime, or I/O status
- message text
- source location when available
- a bounded traceback

This is the recommended integration path for hosts that need predictable failure handling.

---

## 7. Optional builtin surface

The base language stays small.
Larger scripting features are opt-in through builtin groups.

Examples:

- string/list/data helpers
- JSON helpers
- filesystem helpers
- process execution helpers

This keeps the core embedding surface smaller while still allowing CLI and automation-oriented deployments to expose richer capabilities.

---

## 8. Practical tradeoffs

The current FeX implementation is pragmatic rather than purist:

- spans, import bookkeeping, and maps use tracked heap allocations in addition to the arena
- the codebase is no longer the original sub-1-kLoC experiment
- optional helpers for files and processes materially increase the implementation footprint
- the benefit is a much more usable language for real integration work

---

## 9. Summary

FeX is best viewed as a lowering front-end plus an integration layer around the `fe` runtime.
That split is what lets the project stay understandable while still supporting modules, diagnostics, selective builtins, and host-safe recoverable APIs.
