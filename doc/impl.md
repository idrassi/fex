# **Fe / FeX — A Tiny Embeddable Language (2025 Edition)**

*A self-contained description of the current implementation, weaving original 2020 concepts with every improvement introduced in FeX.*

---

## 1 Origin Story

When **fe** first appeared in 2020 it offered the allure of a Scheme-flavoured interpreter packed into *< 1 kLoC* of strictly portable ANSI C. In 2025 its successor, **FeX**, keeps that spartan ethos while embracing modern conveniences: immediate numbers and booleans, real closures, and a lightweight module system. This document folds the original design notes together with the FeX delta so you have a *single source of truth*.

---

## 2 Design Goals — Unchanged, Yet Evolved

| Goal                                          | 2020 Status    | 2025 Status         |
| --------------------------------------------- | -------------- | ------------------- |
| **No dynamic `malloc`**                       | ✔ fixed buffer | ✔ unchanged         |
| **≤ 1000 LoC**                                | \~950          | **920**             |
| **Host-supplied memory size**                 | ✔              | ✔                   |
| **Portable (Win / Linux / DOS, 32 & 64-bit)** | ✔              | ✔                   |
| **Simple C API**                              | single header  | still single header |
| **Sweet-spot: tiny scripts, configs, REPLs**  | ✔              | ✔ + *modules*       |

### *New since 2020*

* Immediate **fixnums** & **booleans** (zero allocation).
* Dynamic GC threshold (`live × 2`, min 1024).
* Proper lexical **closures** via *up-value frames*.
* **return**, **module/export/import/get** special forms.
* Static pass that computes a function’s free variables at definition time.
* Optional boxed double fallback if a number outgrows fixnum range.

---

## 3 Memory Layout at a Glance

```
┌─ contiguous buffer from host ───────────────────────────────────────────┐
│ fe_Context • object[0...N-1] • (padding)                                 │
└─────────────────────────────────────────────────────────────────────────┘
```

* `fe_open(ptr,len)` installs the interpreter inside that buffer.
* A **freelist** threads through the object array; *no allocation* ever leaves it.
* `gcstack[1024]` temporarily roots freshly-made objects between C calls.

The GC runs when either:

1. `allocs_since_gc ≥ max(live × 2, 1024)` –– the *heuristic path*, or
2. the freelist is already empty –– the *fallback path*.

---

## 4 Data Representation

*(Why it matters: understanding tags unlocks the entire C API.)*

### 4.1 Immediates (never stored on the heap)

| Kind        | Low-order bit pattern           | Notes                      |
| ----------- | ------------------------------- | -------------------------- |
| **Fixnum**  | ...`xxx1`                         | Signed, word-size-1 bits.  |
| **Boolean** | ...`0010` = false, ...`0110` = true | Separate tag from numbers. |

Nil remains a unique heap cell for historical reasons.

> **Rule of thumb** If you can represent it as an immediate, you get allocation-free speed.

### 4.2 Heap Objects

```c
struct fe_Object {
  Value   car;
  Value   cdr;
  uint8_t flags;    /* bit0: 0 = pair, 1 = non-pair
                       bit1: GC mark
                       bits2-7: type tag              */
};
```

| Tag              | Extra payload (`cdr`)          |
| ---------------- | ------------------------------ |
| `PAIR`           | linked cons cell               |
| `STRING`         | next chunk (roped string)      |
| `NUMBER`         | boxed `double`                 |
| `SYMBOL`         | `(string . global-value)` pair |
| `FUNC` / `MACRO` | closure record (see § 6)       |
| `PRIM`           | `uint8_t` dispatch index       |
| `CFUNC`          | `fe_CFunc` pointer             |
| `PTR`            | host pointer + optional hooks  |
| `FREE`           | member of freelist             |

---

## 5 Evaluator & Core Forms (2025 Set)

| Special / Primitive                    | Change since 2020                               |
| -------------------------------------- | ----------------------------------------------- |
| `let`                                  | Now supports *let-rec*, returns bound value     |
| `fn` / `mac`                           | Real closure objects                            |
| `return`                               | **New** — multi-level return                    |
| `module` / `export` / `import` / `get` | **New** — minimal module system                 |
| `while`, `if`, `=`                     | Behaviour preserved                             |
| Arithmetic `+ - * / < <=`              | Works on fixnum *or* boxed double automatically |

All previous list-processing, logic and I/O primitives remain intact.

---

## 6 Environments, Closures & the *\[frame]* Sentinel

### 6.1 Why a Sentinel?

Imagine looking up a symbol. Under FeX you might be traversing:

```
([frame] . (locals . upvalues))
```

without clashing with the *old* association-list world. The literal symbol **\[frame]** in the `car` gives an O(1) test: “is this a runtime closure frame or an ordinary a-list?”

> *A tiny marker buys a bullet-proof separation between legacy code and new closures.*

### 6.2 Closure Record Layout

```
(tag FUNC|MACRO)
cdr = (definition-env  free-vars  params . body)
```

* At call time the interpreter builds an *upvalue* list by grabbing each recorded free variable from `definition-env`.
* A runtime frame is assembled, evaluated, and a `(return . value)` sentinel bubbles up if `return` is used.

A one-time **static analysis pass** computes the free-var list so calls pay *zero* overhead.

---

## 7 Garbage Collection (Mark-and-Sweep, Updated)

1. **Mark** – Iteratively follows `cdr`, recurses only down `car`; PTR hooks fire here.
2. **Sweep** – Returns dead cells to the freelist and counts survivors -> updates threshold.

The left-leaning recursion hazard (*deep car chains*) still exists but is rarely hit in real-world scripts.

---

## 8 Error Strategy

`fe_handlers(ctx)->error` receives `(msg, callstack)` and may `longjmp` out. *Never* allocate new objects inside the handler—the GC root stack is frozen until control returns to C.

---

## 9 C API Highlights (Additions in Bold)

```c
fe_Object *fe_fixnum(long n);          /* immediate wrapper          */
fe_Object *fe_make_number(ctx, val);   /* **auto fixnum / double**   */
fe_Object *fe_bool(ctx, int b);        /* **true / false immediates** */
void      *fe_cdr_ptr(ctx,pair);       /* **mutable cdr access**     */
double      fe_num_value(ctx,obj);     /* works on either number rep */
```

Everything else — `fe_cons`, `fe_symbol`, `fe_eval`, etc. — is source-compatible with 2020 code.

---

## 10 Known Limitations

* Still recurses on the `car` side during marking.
* Little-endian bit tricks make the tag scheme non-portable to big-endian.
* No proper tail-call elimination (use `while`).
* Strings remain NUL-terminated; binary blobs require an external pointer type.
* `import` only establishes a naming convention — host code must load the module.

---

## 11 Migration Cheatsheet

| 2020 Idiom                 | Modern Replacement                                |                               |
| -------------------------- | ------------------------------------------------- | ----------------------------- |
| *All* numbers boxed        | `fe_make_number` -> automatic fixnum when possible |                               |
| `nil` as boolean           | \`fe\_bool(ctx,1                                  | 0)`or literals`true`/`false\` |
| DIY closure capture hacks  | just write `fn` — upvalues handled for you        |                               |
| Macro `(return x)` pattern | use built-in `return`                             |                               |
| Global table as module     | `(module "name" ...)` with explicit `export`s       |                               |

*Rule:*  re-compile against `fe.c`/`fe.h` (2025) and your existing code should run, usually faster, with richer semantics.

---

## 12 Closing Perspective

FeX demonstrates a systems-thinking upgrade path: **zoom in** to a single tagged pointer and shave cycles; **zoom out** to a module system that helps organise whole programs. By retaining the fixed-buffer discipline and a < 1 kLoC code base, the language remains *graspable*—small enough to read in an afternoon, yet big enough to script entire tools. Re-embed it, extend it, or simply study it as a living example of minimalist interpreter engineering.
