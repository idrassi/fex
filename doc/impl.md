# **Fe / FeX - Implementation Notes (2025)**

*An accurate implementation-level summary of the current runtime and syntax layer.*

---

## 1. Scope

Fe is the small Lisp-like runtime.
FeX is the C-like surface syntax, module/import layer, diagnostics layer, and optional builtin set built on top of that runtime.

This document focuses on the implementation that ships in this repository today, not the smaller historical prototype it grew from.

---

## 2. Current Design Goals

The current code still aims for the same broad niche:

- small enough to audit without a large toolchain
- easy to embed in a host process
- practical for scripts, configs, REPL use, and lightweight automation
- portable across the major desktop toolchains

What changed since the original prototype:

- Core values still live in a host-supplied arena.
- The implementation is no longer "no malloc" and no longer a sub-1-kLoC experiment.
- The public CMake build now targets C99.
- Higher-level features such as import bookkeeping, spans, and maps use tracked auxiliary heap allocations in addition to the arena.

---

## 3. Memory Model

### 3.1 Arena-backed core values

`fe_open(ptr, size)` installs `fe_Context` and the object arena inside a caller-provided memory block.
Pairs, symbols, functions, macros, primitive wrappers, and other core values are allocated from that arena.

### 3.2 Auxiliary tracked heap state

FeX also maintains tracked heap allocations for features that are awkward or wasteful to force into the object arena, including:

- source span tables
- import search-path state
- source buffers for imported files
- module-cache metadata
- map backing arrays

Tracked allocations participate in the runtime memory accounting exposed by `fe_get_memory_used()` and enforced by `fe_set_memory_limit()`.

### 3.3 Garbage collection

The collector is mark-and-sweep.
Core objects are marked from:

- the GC root stack
- globals and interned symbols
- the active call stack
- active module/import state
- cached imported module values

The known structural limitation remains the same: marking still recurses down some object graph shapes, especially deep `car` chains.

---

## 4. Data Representation

### 4.1 Immediates

- fixnums are tagged immediates
- booleans are tagged immediates
- `nil` is a singleton runtime value

### 4.2 Heap objects

The runtime stores the usual Lisp pair type plus several non-pair object kinds, including:

- strings
- bytes
- symbols
- functions and macros
- primitive and C function wrappers
- host pointers
- maps

Maps are important in modern FeX because they back:

- user-visible map values
- module export tables
- several host-facing builtin results

---

## 5. Evaluation Model

Key semantics implemented in `fe.c`:

- lexical scoping with closures
- mutable captured bindings
- tail-call optimization through an evaluator trampoline
- explicit `return` implemented with a hidden sentinel object
- `do` blocks that thread an evolving local environment

Named `fn` declarations in FeX are lowered into the core forms before evaluation.

---

## 6. Modules And Imports

### 6.1 Explicit modules

`module("name") { ... }` creates an export map, evaluates the body, then binds that map under the given module name.

### 6.2 Imported files as implicit modules

When a file is loaded via `import`, the runtime pushes an implicit export map for that file.
Top-level `export let` and `export fn` populate that map directly.
Non-exported top-level names stay local to the imported file's evaluation scope.

### 6.3 Lookup and caching

File-based import resolution searches in this order:

1. the importing file's directory
2. each configured import path
3. the current working directory

Within each root, the runtime tries both:

- `name.fex`
- `name/index.fex`

Imported module values are cached by resolved path.
Import specifiers containing `..` path components are rejected.

---

## 7. Diagnostics And Recovery

The low-level evaluator still reports errors through the installed `fe_handlers(ctx)->error` hook.

FeX adds recoverable wrappers:

- `fex_try_compile`
- `fex_try_eval`
- `fex_try_do_string`
- `fex_try_do_file`

These wrappers preserve structured error details, including:

- status class
- message
- source name
- line and column when available
- a bounded traceback of FeX expressions

Span tracking is optional and can be enabled through FeX init flags or the CLI.

---

## 8. Resource Controls

The runtime exposes several host-controlled limits:

- eval step budget
- wall-clock timeout
- tracked memory ceiling
- eval recursion depth
- read recursion depth
- host interrupt callback

These controls are intended for embedding and automation scenarios where runaway or untrusted scripts must stay in-process.

---

## 9. Portability Notes

The current codebase is written in portable C, but it is not a strict ANSI C / C89 codebase anymore.

Important practical notes:

- the public CMake build targets C99
- the code relies on modern integer headers and formatting macros
- thread-local try-state uses compiler or platform support when available
- the project is exercised on MSVC, GCC, and Clang

---

## 10. Known Limitations

- Big-endian portability is still not a focus for the tagged-value implementation.
- GC marking still has recursive cases.
- Symbol lookup is linear in the interned symbol list.
- Optional host-facing helpers such as filesystem and process builtins substantially increase code size relative to the original minimal runtime.

---

## 11. Summary

The current Fe / FeX implementation is best understood as a compact arena-based core plus a pragmatic set of higher-level subsystems layered around it.
It is no longer the original tiny fixed-buffer-only experiment, but it remains small enough to reason about and practical enough to embed in real products and pipelines.
