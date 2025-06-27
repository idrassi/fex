# FeX C API Documentation

## Introduction

FeX is a tiny, modern, and embeddable scripting language implemented in pure ANSI C. It is designed to be easily integrated into C/C++ applications to provide a flexible and powerful scripting layer.

FeX builds on the minimal Lisp-like core fe and adds a modern surface syntax plus richer runtime features.

This document details the C API for embedding the FeX interpreter. It covers context management, running scripts, exchanging data between C and FeX, and extending the language with custom C functions and types.

## Core Concepts

Before diving into the API, it's important to understand a few core concepts.

### fe_Context
A `fe_Context` is an opaque struct that holds the entire state of a FeX interpreter instance, including all allocated objects, the symbol table, the call stack, and the garbage collector state. Your application will pass a pointer to this context to nearly every API function. (See *Threading Model* for details on thread safety).

### fe_Object
`fe_Object` is the universal data type in FeX. It represents all values, including numbers, strings, lists, and functions. Some simple types are "immediate" values that are not allocated on the heap:

*   **Numbers**: Can be a 64-bit `double` (a "boxed" heap object) or a pointer-sized integer (an "immediate" value called a "fixnum"). The API provides helpers to work with both transparently. All numbers are IEEE-754 doubles semantically; fixnums are just a space/time optimisation, not a distinct numeric type.
*   **Booleans**: `true` and `false` are immediate singleton values.
*   **Nil**: A special singleton value representing nothingness, also an immediate.

All other types are heap-allocated objects managed by the garbage collector.

### Garbage Collection
FeX uses a mark-and-sweep garbage collector. From the C API perspective, this means you must be careful with `fe_Object` pointers. Any heap-allocated object is considered temporary and may be collected during a subsequent allocation.

To protect an object from being collected, you must push it onto the GC's root stack. Think of `fe_savegc`/`fe_restoregc` as pushing and popping a bookmark in a ledger. Any objects created after the bookmark are protected until the stack is restored to that bookmark.

 All objects constructed by any helper are already on the root stack: the frame merely scopes them.

```c
int gc_idx = fe_savegc(ctx);
fe_Object *my_string = fe_string(ctx, "hello");
// When you call fe_string(ctx, "hello") the allocator automatically pushes every chunk cell of the new string onto the GC-root stack, so the resulting string is protected for the duration of the current fe_savegc/fe_restoregc frame.

// ... do more work that might allocate memory ...
// my_string is safe here

fe_restoregc(ctx, gc_idx); // Pops my_string (and any other temp objects) off the stack
```

### Source Location Tracking
FeX can map any compiled code object back to its original source location. This is a key feature for providing rich error diagnostics. You can access this information with `fex_lookup_span()`, which is especially useful in custom error handlers. (See *Error Handling*).

> **Design Note: Nil Safety**
> FeX never dereferences `nil`. Accessors like `fe_car(ctx, fe_nil(ctx))` are defensive and simply return `nil`. You can exploit this to write clearer, safer algorithms by treating `nil` as a stable base case.

## Basic Setup and Teardown

To embed FeX, you must first allocate a block of memory and initialize a `fe_Context` within it.

### fe_open()
```c
fe_Context* fe_open(void *ptr, int size);
```
Initializes a `fe_Context` in a provided block of memory. The memory block is used by the context for all allocations and must remain valid for its lifetime. 1 MiB is a comfortable starter for tests; production applications often use 8 MiB–32 MiB. Roughly 24 bytes per live cons, and ~24 bytes per boxed object (pair + payload).

### fex_init()
```c
void fex_init(fe_Context *ctx);
```
Initializes the FeX-specific environment. This function must be called **after** `fe_open()`. It registers FeX built-in functions (like `println`) and, importantly, sets up a rich error handler that provides detailed source location information.

### fe_close()
```c
void fe_close(fe_Context *ctx);
```
Releases resources used by the context. If you created any Pointer Objects with `gc` finalizers, this function ensures they are called for any remaining objects.

### Example
```c
#include "fe.h"
#include "fex.h"

int main() {
    size_t mem_size = 1024 * 1024; // 1 MiB
    void *memory = malloc(mem_size);

    fe_Context *ctx = fe_open(memory, mem_size);
    fex_init(ctx);

    // ... use the FeX context ...

    fe_close(ctx);
    free(memory);
    return 0;
}
```

## Running FeX Code

### fex_do_string()
```c
fe_Object* fex_do_string(fe_Context *ctx, const char *source);
```
A convenience function that compiles and then evaluates a string of FeX source code. It returns the result of the last expression as an `fe_Object*`. On a **compile-time error**, the function returns `NULL`. Runtime errors are delivered through the installed `error` handler; by default, this terminates the process.

```c
int gc_idx = fe_savegc(ctx);
const char *script = "let result = 10 + 32; result;";
fe_Object *res_obj = fex_do_string(ctx, script);
if (res_obj) {
    printf("Result: %g\n", fe_tonumber(ctx, res_obj)); // "Result: 42"
}
fe_restoregc(ctx, gc_idx);
```

### fex_compile() and fe_eval()
For more control, you can separate compilation and evaluation. This is useful for running the same code multiple times.

```c
fe_Object* fex_compile(fe_Context *ctx, const char *source);
fe_Object* fe_eval(fe_Context *ctx, fe_Object *obj);
```
`fex_compile` takes FeX source and produces an evaluatable `fe_Object` (the AST). `fe_eval` executes this compiled object.

```c
int gc_idx = fe_savegc(ctx);
fe_Object *code = fex_compile(ctx, "100 / 2;");
if (code) {
    fe_Object *res = fe_eval(ctx, code);
    // ...
}
fe_restoregc(ctx, gc_idx);
```

## Interacting with FeX from C

You can create FeX objects in C, pass them to FeX functions, and inspect the results.

### Creating FeX Objects
To create values, use these functions: `fe_nil(ctx)` returns the `nil` object. `fe_bool(ctx, int b)` returns the `true` or `false` singleton. For numbers, you have two choices: **`fe_number(ctx, fe_Number n)`** always creates a boxed `double` object on the heap, while **`fe_make_number(ctx, fe_Number n)`** is a "smart" constructor that will create an immediate fixnum if the value is an integer that fits, otherwise falling back to a boxed double. Use **`fe_string(ctx, const char *s)`** for strings and **`fe_symbol(ctx, const char *s)`** for interned symbols. `fe_symbol` guarantees pointer equality for identical names.
To build a list, use **`fe_list(ctx, fe_Object **objs, int n)`**.
*Note: `fe_list` consumes the input C array from back to front to build the list in the correct logical order without a second reversal pass.*

### Inspecting FeX Objects
To check an object's type, use **`fe_type(ctx, fe_Object *obj)`**, which returns an enum like `FE_TSTRING` or `FE_TNUMBER`. `fe_type()` returns `FE_TNUMBER` for *both* fixnums and boxed doubles; to distinguish them, use the macro **`FE_IS_FIXNUM(obj)`**. Check for nil with **`fe_isnil(ctx, fe_Object *obj)`** (the `ctx` parameter is ignored; it's kept for API uniformity). Convert objects to C types with **`fe_tonumber(ctx, fe_Object *obj)`** (which works for both number representations), **`fe_tostring(ctx, fe_Object *obj, char *dst, int size)`**, and **`fe_toptr(ctx, fe_Object *obj)`** for Pointer Objects.

 ⚠ **Nil guard:** Unlike the plain accessors `fe_car` and `fe_cdr`, the low-level helper `fe_cdr_ptr(ctx, obj)` **throws an error** if `obj` is `nil`, because returning a writable pointer to “nothing” would be unsafe. Keep a non-nil guard around this call.

### Calling a FeX Function from C
To call a FeX function, construct a list where the first element is the function and the rest are arguments, then `fe_eval` that list.

```c
// Assumes a function `add(a, b)` exists. We'll call `add(10, 20)`.
int gc_idx = fe_savegc(ctx);
fe_Object *call_parts[3];
call_parts[0] = fe_symbol(ctx, "add");
call_parts[1] = fe_make_number(ctx, 10);
call_parts[2] = fe_make_number(ctx, 20);

// fe_list internally protects its allocations, so no extra fe_pushgc is needed here.
fe_Object *call_list = fe_list(ctx, call_parts, 3);
fe_Object *result_obj = fe_eval(ctx, call_list);

printf("Result: %g\n", fe_tonumber(ctx, result_obj));
fe_restoregc(ctx, gc_idx);
```

## Extending FeX with C

### Creating C Functions
Expose C functions to FeX scripts by defining a function with the `fe_CFunc` signature:
`typedef fe_Object* (*fe_CFunc)(fe_Context *ctx, fe_Object *args);`

It receives the context and a single list of evaluated arguments. It must return an `fe_Object*`. **Never return `NULL`**; to return `nil`, use `fe_nil(ctx)`. The `fe_nextarg()` helper consumes arguments from the list.

```c
#include <math.h>

static fe_Object* c_pow(fe_Context *ctx, fe_Object *args) {
    fe_Number base = fe_tonumber(ctx, fe_nextarg(ctx, &args));
    fe_Number exp = fe_tonumber(ctx, fe_nextarg(ctx, &args));
    return fe_make_number(ctx, pow(base, exp));
}

// In your setup:
fe_Object *cfunc = fe_cfunc(ctx, c_pow);
fe_set(ctx, fe_symbol(ctx, "c_pow"), cfunc);

// In FeX: println(c_pow(2, 10)); // -> 1024
```

#### Returning early from FeX code

FeX ships with a `return` special form that works in both script code **and** C-defined functions generated at runtime:

```fex
fn fact(n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
```

From the C side nothing special is required—just build the list `(return <value>)` if you need an early exit when synthesising ASTs.

> **Note for host-side C functions (`fe_CFunc`):** *Do **not** signal a return by returning `NULL`.*
> Your function should always return a valid `fe_Object*`—use `fe_nil(ctx)` when you have no meaningful value.

### Pointer Objects
The `ptr` type wraps arbitrary C pointers, exposing them to FeX. This is ideal for file handles, database connections, etc. Use `fe_ptr(ctx, void *p)` to create one and `fe_toptr(ctx, obj)` to retrieve the pointer.

To manage the C data's lifecycle, you can set GC handlers via `fe_handlers(ctx)`:

- `gc`: A `fe_CFunc` finalizer called when a `ptr` object is collected. Use this to `free()` the associated C memory.
- `mark`: A `fe_CFunc` called when the GC marks live objects. If your pointer's struct contains other `fe_Object*`s, you must call `fe_mark()` on them here to keep them alive.

*Note: The `fe_Object*` returned by `gc` and `mark` handlers is ignored by the VM; it only cares that the C function signature matches `fe_CFunc`.*

### Working with Modules
The FeX modern syntax includes a module system. From C, you can interact with modules by treating them as tables (association lists).

To access an exported function or value `member` from a module named `my_module`:
1. Get the module object, which is bound to a global symbol: `fe_Object *mod = fe_eval(ctx, fe_symbol(ctx, "my_module"));`
2. Construct a `(get module member)` call as a list.
3. Evaluate the list.

```c
// Assume a script ran: module("my_mod") { export let version = "1.0"; }
int gc_idx = fe_savegc(ctx);

// Get the module object (it's a list of bindings)
fe_Object *mod_sym = fe_symbol(ctx, "my_mod");
fe_Object *mod_obj = fe_eval(ctx, mod_sym);
fe_pushgc(ctx, mod_obj); // Protect the module object

// Build the call: (get my_mod version)
fe_Object *call_parts[3];
call_parts[0] = fe_symbol(ctx, "get");
call_parts[1] = mod_obj;
call_parts[2] = fe_symbol(ctx, "version");
fe_Object *call_list = fe_list(ctx, call_parts, 3);

// Evaluate and get the result
fe_Object *res = fe_eval(ctx, call_list);
char buf[32];
fe_tostring(ctx, res, buf, sizeof(buf));
printf("Module version: %s\n", buf); // "Module version: 1.0"

fe_restoregc(ctx, gc_idx);
```

## Error Handling

By default, `fex_init()` installs an error handler that prints a detailed message with source location and exits. For applications that must recover from errors, you can set a custom handler using `longjmp`.

### fe_handlers()
`fe_handlers(ctx)->error` can be set to a custom `fe_ErrorFn`:
`typedef void (*fe_ErrorFn)(fe_Context *ctx, const char *err, fe_Object *cl);`
The `cl` argument is the FeX callstack, a list of AST nodes.

### fex_lookup_span()
To get source info from a callstack node, use `fex_lookup_span(node)`, which returns a `const FexSpan*`:
```c
typedef struct FexSpan {
    const fe_Object *node;      // The AST node this span refers to
    const char      *source;    // Pointer to the original source buffer
    int  start_line, start_col; // 1-based start position
    int  end_line,   end_col;   // 1-based end position
} FexSpan;
```

## Threading and Re-entrancy

The `fe_Context` is **not thread-safe**. All API calls for a given context must be made from the same thread. However, you can create multiple `fe_Context`s, each in its own thread, to run scripts in parallel.

The interpreter **is re-entrant**. A C function called from `fe_eval` can safely call `fe_eval` again on the same context. This is a fully supported pattern.

## Common Pitfalls

- **Forgetting GC protection:** Any `fe_Object*` returned from an API function is temporary. If you need to hold onto it across other API calls that might allocate memory, you must protect it within a `fe_savegc`/`fe_restoregc` block.
- **Dangling C pointers in `fe_ptr`:** The GC manages the `fe_Object` wrapper, not the C pointer inside it. If the C data is freed while FeX can still access the `fe_ptr` object, you will have a use-after-free bug. Use the `gc` handler to correctly tie the C data's lifetime to the `fe_ptr` object's lifetime.