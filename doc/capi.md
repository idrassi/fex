# FeX C API Documentation

## Introduction

FeX is a small embeddable scripting language implemented in ANSI C. It builds on the Lisp-like `fe` core and adds a modern surface syntax, source spans, maps, and optional extended builtins.

This document covers the public C API used to create an interpreter, run FeX code, exchange values with C, and extend the runtime with host functions.

## Core Concepts

### fe_Context

`fe_Context` is the interpreter state. It owns the object arena, symbol table, module state, and garbage collector metadata. Almost every API call takes a `fe_Context *`.

### fe_Object

`fe_Object` is the universal value type. It represents numbers, strings, pairs, symbols, functions, C functions, pointers, booleans, and `nil`.

Some values are immediate and do not allocate:

- Numbers may be either boxed doubles or immediate fixnums.
- `true` and `false` are immediate singleton values.
- `nil` is an immediate singleton value.

All other values live in the interpreter arena and are managed by the garbage collector.

### Garbage Collection

FeX uses a mark-and-sweep collector. From the C side, any freshly created `fe_Object *` should be treated as temporary unless it is protected by the GC root stack.

Use `fe_savegc()` and `fe_restoregc()` to scope temporary allocations:

```c
int gc_idx = fe_savegc(ctx);
fe_Object *my_string = fe_string(ctx, "hello", 5);

/* ... do more work that may allocate ... */

fe_restoregc(ctx, gc_idx);
```

Objects created after `fe_savegc()` remain protected until the matching `fe_restoregc()`.

### Source Location Tracking

FeX can associate AST nodes with source locations. This is mainly useful when you install a custom error handler or when you want to correlate `FexError` stack frames with compiled nodes. Use `fex_lookup_span()` for that lookup.

## Basic Setup and Teardown

To embed FeX, allocate a block of memory and initialize a `fe_Context` inside it.

### fe_open()

```c
fe_Context *fe_open(void *ptr, size_t size);
```

Initializes a `fe_Context` in the supplied memory block. The block must remain valid for the life of the interpreter. A 1 MiB arena is fine for small tests; larger applications may want several MiB.

### fex_init()

```c
void fex_init(fe_Context *ctx);
```

Initializes the base FeX environment. This must be called after `fe_open()`. It installs the default FeX error behavior and registers the always-available builtins such as `print` and `println`.

### fex_init_with_config()

```c
void fex_init_with_config(fe_Context *ctx, FexConfig config);
```

Like `fex_init()`, but enables optional features through flags:

- `FEX_CONFIG_ENABLE_SPANS` enables source span tracking.
- `FEX_CONFIG_ENABLE_EXTENDED_BUILTINS` registers the optional extended builtins set, including helpers such as `sqrt`, `map`, `filter`, `parsejson`, `readjson`, and `pathjoin`.

### fex_init_with_builtins()

```c
void fex_init_with_builtins(fe_Context *ctx, FexConfig config,
                            FexBuiltinsConfig builtins);
```

Like `fex_init_with_config()`, but lets the host choose specific builtin categories instead of enabling the full extended set.

- Categories: `FEX_BUILTINS_MATH`, `FEX_BUILTINS_STRING`, `FEX_BUILTINS_LIST`, `FEX_BUILTINS_IO`, `FEX_BUILTINS_SYSTEM`, `FEX_BUILTINS_TYPE`, `FEX_BUILTINS_DATA`
- Presets: `FEX_BUILTINS_SAFE` (all categories except I/O and system) and `FEX_BUILTINS_ALL`

Example:

```c
fex_init_with_builtins(ctx,
    FEX_CONFIG_ENABLE_SPANS,
    FEX_BUILTINS_STRING | FEX_BUILTINS_DATA);
```

### fe_close()

```c
void fe_close(fe_Context *ctx);
```

Releases resources associated with the interpreter. If pointer objects have GC finalizers, `fe_close()` ensures they are run for remaining live objects.

### Execution Resource Controls

```c
void fe_set_step_limit(fe_Context *ctx, size_t max_steps);
size_t fe_get_step_limit(fe_Context *ctx);
size_t fe_get_steps_executed(fe_Context *ctx);
void fe_set_timeout_ms(fe_Context *ctx, uint64_t timeout_ms);
uint64_t fe_get_timeout_ms(fe_Context *ctx);
void fe_set_interrupt_handler(fe_Context *ctx, fe_InterruptFn fn,
                              void *udata, size_t check_interval_steps);
```

These low-level `fe` APIs let a host bound script execution without killing the process.

- `fe_set_step_limit()` sets a per-top-level evaluation budget. `0` disables the limit.
- `fe_set_timeout_ms()` sets a per-top-level wall-clock timeout. `0` disables the timeout.
- `fe_get_steps_executed()` reports the number of eval steps consumed by the current or most recent evaluation.
- `fe_set_interrupt_handler()` installs a callback that runs every `check_interval_steps` evaluations. If it returns non-zero, evaluation stops with the runtime error `"execution interrupted"`.

Use `fe_set_timeout_ms()` when a simple wall-clock deadline is enough. Use `fe_set_interrupt_handler()` when the host needs custom timeout policy or external cancellation.

### Minimal Example

```c
#include <stdlib.h>

#include "fe.h"
#include "fex.h"

int main(void) {
    size_t mem_size = 1024 * 1024;
    void *memory = malloc(mem_size);
    fe_Context *ctx;

    if (!memory) return 1;

    ctx = fe_open(memory, mem_size);
    if (!ctx) {
        free(memory);
        return 1;
    }

    fex_init(ctx);

    /* ... run scripts ... */

    fe_close(ctx);
    free(memory);
    return 0;
}
```

## Running FeX Code

### Legacy Convenience Calls

```c
fe_Object *fex_do_string(fe_Context *ctx, const char *source);
fe_Object *fex_do_file(fe_Context *ctx, const char *path);
```

These helpers are convenient for simple tools:

- `fex_do_string()` compiles and evaluates a source string.
- `fex_do_file()` reads, compiles, and evaluates a file, resolving relative imports against that file's directory.
- On compile errors they return `NULL`.
- On runtime errors they go through the installed error handler. The default FeX handler prints a traceback and terminates the process.

### Recoverable Execution APIs

```c
FexStatus fex_try_compile(fe_Context *ctx, const char *source,
                          const char *source_name, fe_Object **out_code,
                          FexError *out_error);
FexStatus fex_try_eval(fe_Context *ctx, fe_Object *obj, fe_Object **out_result,
                       FexError *out_error);
FexStatus fex_try_do_string(fe_Context *ctx, const char *source,
                            fe_Object **out_result, FexError *out_error);
FexStatus fex_try_do_file(fe_Context *ctx, const char *path,
                          fe_Object **out_result, FexError *out_error);
void fex_error_clear(FexError *error);
void fex_print_error(FILE *fp, const FexError *error);
```

Use these APIs when the host must recover from failures instead of exiting.

- `fex_try_compile()` returns a compiled AST or a structured compile error. `source_name` is copied into diagnostics; pass `NULL` for `"<string>"`.
- `fex_try_eval()` evaluates a precompiled AST and captures runtime failures.
- `fex_try_do_string()` and `fex_try_do_file()` cover the full compile/eval path.
- `FexStatus` is one of `FEX_STATUS_OK`, `FEX_STATUS_COMPILE_ERROR`, `FEX_STATUS_RUNTIME_ERROR`, or `FEX_STATUS_IO_ERROR`.
- `FexError` carries the copied error message, source name, line/column, and up to `FEX_ERROR_TRACE_MAX` traceback frames.

```c
int gc_idx = fe_savegc(ctx);
fe_Object *result = NULL;
FexError error;
FexStatus status;

status = fex_try_do_string(ctx, "let x = 10 + 32; x;", &result, &error);
if (status == FEX_STATUS_OK) {
    printf("Result: %g\n", fe_tonumber(ctx, result));
} else {
    fex_print_error(stderr, &error);
}

fe_restoregc(ctx, gc_idx);
```

### fex_compile() and fe_eval()

```c
fe_Object *fex_compile(fe_Context *ctx, const char *source);
fe_Object *fe_eval(fe_Context *ctx, fe_Object *obj);
```

Use `fex_compile()` and `fe_eval()` directly when you want the low-level split between parsing and evaluation and are willing to manage errors through either the installed handler or the `fex_try_*` wrappers.

### Import Search Paths

For file-based `import`, configure additional search roots with:

```c
int fex_add_import_path(fe_Context *ctx, const char *path);
void fex_clear_import_paths(fe_Context *ctx);
```

`fex_add_import_path()` returns non-zero on success.

## Creating and Inspecting Values

### Creating Values

Useful constructors include:

- `fe_nil(ctx)`
- `fe_bool(ctx, int b)`
- `fe_number(ctx, fe_Number n)` for a boxed double
- `fe_make_number(ctx, fe_Number n)` for a fixnum when possible, otherwise a boxed double
- `fe_string(ctx, const char *s, size_t len)`
- `fe_symbol(ctx, const char *s)`
- `fe_map(ctx)` for a mutable string/symbol-keyed map
- `fe_list(ctx, fe_Object **objs, int n)`
- `fe_cfunc(ctx, fe_CFunc fn)`
- `fe_ptr(ctx, void *ptr)`

`fe_symbol()` interns names, so identical symbol names compare by pointer identity.

### Inspecting Values

Useful inspection helpers include:

- `fe_type(ctx, obj)`
- `fe_isnil(ctx, obj)`
- `fe_tonumber(ctx, obj)`
- `fe_tostring(ctx, obj, char *dst, int size)`
- `fe_toptr(ctx, obj)`
- `fe_strlen(ctx, obj)`

For numbers, `fe_type()` returns `FE_TNUMBER` for both fixnums and boxed doubles. Use `FE_IS_FIXNUM(obj)` if you need to distinguish them.

`fe_car(ctx, obj)` and `fe_cdr(ctx, obj)` are nil-safe: if `obj` is `nil`, they return `nil`.

> `fe_cdr_ptr(ctx, obj)` is different: it throws an error on `nil`, because returning a writable pointer to nothing would be unsafe.

### Maps

Maps are mutable associative containers keyed by strings or symbols. They are a good fit for configuration objects, JSON-like data, and module-style exports.

```c
fe_Object *map = fe_map(ctx);
fe_map_set(ctx, map, fe_symbol(ctx, "host"), fe_string(ctx, "localhost", 9));
```

Useful helpers include:

- `fe_map_set(ctx, map, key, value)`
- `fe_map_get(ctx, map, key)`
- `fe_map_has(ctx, map, key)`
- `fe_map_delete(ctx, map, key)`
- `fe_map_count(ctx, map)`
- `fe_map_keys(ctx, map)`

Map keys must be strings or symbols. Symbols are normalized to their name strings, so `fe_map_set(ctx, map, fe_symbol(ctx, "host"), v)` and `fe_map_get(ctx, map, fe_string(ctx, "host", 4))` refer to the same entry.

## Calling FeX Functions from C

To call a FeX function, build a list whose first element is the callable and whose remaining elements are arguments, then evaluate that list.

```c
int gc_idx = fe_savegc(ctx);
fe_Object *call_parts[3];
fe_Object *call_list;
fe_Object *result;

call_parts[0] = fe_symbol(ctx, "add");
call_parts[1] = fe_make_number(ctx, 10);
call_parts[2] = fe_make_number(ctx, 20);

call_list = fe_list(ctx, call_parts, 3);
result = fe_eval(ctx, call_list);

printf("Result: %g\n", fe_tonumber(ctx, result));
fe_restoregc(ctx, gc_idx);
```

## Extending FeX with C

### C Functions

Expose a host function with the `fe_CFunc` signature:

```c
typedef fe_Object *(*fe_CFunc)(fe_Context *ctx, fe_Object *args);
```

Arguments arrive as an evaluated list. Use `fe_nextarg()` to consume them.

```c
#include <math.h>

static fe_Object *c_pow(fe_Context *ctx, fe_Object *args) {
    fe_Number base = fe_tonumber(ctx, fe_nextarg(ctx, &args));
    fe_Number exp = fe_tonumber(ctx, fe_nextarg(ctx, &args));
    return fe_make_number(ctx, pow(base, exp));
}

/* During setup */
fe_set(ctx, fe_symbol(ctx, "c_pow"), fe_cfunc(ctx, c_pow));
```

Never return `NULL` from a `fe_CFunc`. Return `fe_nil(ctx)` if you have no meaningful value.

### Pointer Objects

Pointer objects wrap host pointers for use inside FeX:

```c
fe_Object *handle = fe_ptr(ctx, some_pointer);
```

Recover the pointer with `fe_toptr(ctx, obj)`.

Use `fe_handlers(ctx)` to install lifecycle hooks:

- `gc` finalizes a collected pointer object.
- `mark` marks any embedded `fe_Object *` references owned by the pointed-to host structure.

### Modules

FeX modules are ordinary runtime values backed by maps, so you can access them from C like any other binding.

```c
int gc_idx = fe_savegc(ctx);
fe_Object *parts[3];
fe_Object *module_obj;
fe_Object *call_list;
fe_Object *result;
char buf[32];

module_obj = fe_eval(ctx, fe_symbol(ctx, "my_mod"));
fe_pushgc(ctx, module_obj);

parts[0] = fe_symbol(ctx, "get");
parts[1] = module_obj;
parts[2] = fe_symbol(ctx, "version");

call_list = fe_list(ctx, parts, 3);
result = fe_eval(ctx, call_list);

fe_tostring(ctx, result, buf, sizeof(buf));
printf("Module version: %s\n", buf);

fe_restoregc(ctx, gc_idx);
```

## Error Handling

By default, `fex_init()` installs an error handler that prints a message and terminates the process. For most embedders, the recommended path is to keep that default handler and call `fex_try_*` when you need recovery.

### fe_handlers()

```c
fe_Handlers *fe_handlers(fe_Context *ctx);
```

You may still replace `fe_handlers(ctx)->error` with a custom `fe_ErrorFn`:

```c
typedef void (*fe_ErrorFn)(fe_Context *ctx, const char *err, fe_Object *cl);
```

The `cl` argument is the FeX call stack represented as a list of AST nodes. Custom handlers are useful for advanced integrations, but `fex_try_*` is usually simpler because it packages the same information into `FexError` and keeps the interpreter in-process.

### fex_lookup_span()

```c
const FexSpan *fex_lookup_span(const fe_Object *node);
```

If span tracking is enabled, this returns source information for an AST node. `FexSpan` records start/end line and column information plus a stable source-name label for diagnostics.

## Threading and Re-entrancy

`fe_Context` is not thread-safe. All operations on a given context must stay on one thread.

You may create multiple contexts and use one per thread.

Compiled ASTs are ordinary `fe_Object *` values owned by the context that created them. Reuse them within that same context. Do not pass them to a different context or thread.

The interpreter is re-entrant on a single context: a host `fe_CFunc` may call back into `fe_eval()` on that same context.

## Common Pitfalls

- Forgetting GC protection for temporary objects across further allocations.
- Returning `NULL` from a `fe_CFunc`.
- Passing a compiled AST or other `fe_Object *` from one context into another.
- Assuming optional helpers such as `sqrt`, `map`, or `parsejson` are always present. They require `FEX_CONFIG_ENABLE_EXTENDED_BUILTINS`, `fex_init_with_builtins(...)`, `--builtins`, or `--builtin NAME`.
- Using `fe_cdr_ptr()` without first ensuring the target is non-nil.
