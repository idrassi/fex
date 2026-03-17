# FeX

FeX is a tiny, embeddable scripting language with a modern, C-like syntax. It's implemented in portable ANSI C (C89) and is designed for easy integration into other projects.

FeX is built on top of an enhanced version of `fe` core. For details on the new features and improvements, see the [FeX implementation document](doc/FeX-implementation.md) and the [Fe Core Language — 2025 Edition](doc/lang.md).

```c
// Define a function to reverse a list.
fn reverse(lst) {
  let res = nil;
  while (lst) {
    res = cons(car(lst), res);
    lst = cdr(lst);
  }
  return res;
}

// Create a list of strings.
let animals = ["cat", "dog", "fox"];

// Reverse the list and print the result.
println(reverse(animals)); // Outputs: (fox dog cat)
```

FeX provides a familiar "curly-brace" syntax front-end that compiles down to the simple, powerful S-expression format used by its `fe` core. This gives you the best of both worlds: a pleasant, modern language and a small, stable, and easy-to-understand runtime.

## Overview

*   **Modern Syntax**: Familiar C-like syntax for functions, variables (`let`), `if`/`else`, `while` loops, and operators.
*   **Powerful Core**: Supports first-class functions, lexical scoping, closures, and macros inherited from its `fe` backend.
*   **Rich Data Types**: Numbers (doubles and fixnums), Strings, `nil`, Booleans, Pairs (for lists), and Maps for associative data.
*   **Pair Sugar**: `::` builds pairs, `.head`/`.first` and `.tail`/`.rest` read them, and pair selectors can be assigned.
*   **Arena-Based Core Runtime**: Core values live in a fixed-size interpreter arena, while higher-level features such as source spans, import bookkeeping, and maps may use auxiliary heap storage.
*   **Garbage Collection**: A simple and fast mark-and-sweep garbage collector manages the memory arena.
*   **Recoverable Diagnostics**: The CLI and C API can surface structured compile, runtime, and file I/O errors without terminating the host process.
*   **Embeddable C API**: A clean API allows you to easily embed FeX into your C projects, call FeX functions from C, and expose C functions to FeX.
*   **Highly Portable**: Written in pure ANSI C (C89), it compiles on any standard-compliant C compiler.
*   **Concise**: The entire implementation (parser, compiler, and VM) is approximately 1800 lines of code.

## Building

FeX uses CMake for building. You will need `cmake` and a C compiler supported by CMake, such as GCC, Clang, or MSVC.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

On single-config generators such as Ninja or Unix Makefiles, omit `--config Debug`. The executable is typically `build/fex` on single-config generators and `build/Debug/fex.exe` on Visual Studio generators.

## Usage

The `fex` executable can be used to run a script file or to start an interactive Read-Eval-Print Loop (REPL). The examples below use `<path-to-fex>` to stand in for your generator-specific executable path.

### REPL

To start the REPL, run `fex` with no arguments:

```bash
<path-to-fex>
FeX v1.0 (Modern Syntax Layer for enhanced Fe code)
> let x = 10 * 2;
20
> println("Hello, value is " + x);
Hello, value is 20
nil
>
```

### Running a File

To execute a script file, pass the file path as an argument:

```bash
<path-to-fex> your_script.fex
```

Pass `--builtins` to enable the optional extended builtins set, `--spans` for richer source-location diagnostics, and `--module-path PATH` to add file-based import search directories. The CLI exits with `65` for compile errors, `70` for runtime errors, and `74` for file I/O errors.

## Language Quick Tour

### Variables and Types

Variables are declared with `let`. FeX is dynamically typed.

```c
let message = "Hello, world!"; // String
let n = 123;                   // Number (Fixnum)
let pi = 3.14;                 // Number (Double)
let is_active = true;          // Boolean
let my_list = [1, 2, 3];       // List
let empty = nil;               // Nil
```

### Control Flow

`if`/`else` and `while` are supported. `false` and `nil` are considered "falsy".

```c
if (n > 100) {
  println("n is large");
} else {
  println("n is small");
}

let i = 3;
while (i > 0) {
  println(i);
  i = i - 1;
}
```

### Functions and Closures

Functions are first-class citizens. They support closures, capturing their lexical environment.

```c
// A simple function
fn add(a, b) {
  return a + b;
}
println(add(5, 7)); // 12

// A function that returns another function (closure)
fn make_counter() {
  let count = 0;
  fn counter() {
    count = count + 1;
    return count;
  }
  return counter;
}

let c1 = make_counter();
println(c1()); // 1
println(c1()); // 2
```

### Lists

Lists are built on `fe`'s pair type. They can be created with `[]` syntax. The core Lisp-like functions `car` (first element), `cdr` (rest of the list), and `cons` (construct a new pair) are available.

```c
let items = ["a", "b", "c"];

println(car(items)); // "a"
println(cdr(items)); // (b c)

let new_items = cons("z", items);
println(new_items); // (z a b c)
```

Pairs also support right-associative `::` sugar plus selector syntax:

```c
let pair = 1 :: 2 :: 3 :: nil;
println(pair.head);      // 1
println(pair.tail.head); // 2
pair.head = 10;
println(pair);           // (10 2 3)
```

### Maps

When extended builtins are enabled, FeX also supports mutable string/symbol-keyed maps for configuration-style data and module-like objects:

```c
let cfg = makemap("host", "localhost", "port", 8080);

println(cfg.host);                 // localhost
cfg.host = "127.0.0.1";
println(mapget(cfg, "host"));      // 127.0.0.1
println(maphas(cfg, "port"));      // true
println(mapcount(cfg));            // 2
```

Module exports now use the same map representation internally, so `settings.mode = "debug";` updates an imported module property directly.

## Embedding API

FeX is easy to embed. For host applications, prefer the recoverable `fex_try_*` APIs so script failures stay in-process and return structured diagnostics.

```c
#include <stdio.h>
#include <stdlib.h>

#include "fe.h"
#include "fex.h"

#define MEMORY_POOL_SIZE (1024 * 1024) /* 1MB */

int main(void) {
    void *mem = malloc(MEMORY_POOL_SIZE);
    fe_Context *ctx;
    FexError error;
    FexStatus status;
    const char *script = "println(\"Hello from embedded FeX!\");";

    if (!mem) return 1;

    ctx = fe_open(mem, MEMORY_POOL_SIZE);
    if (!ctx) {
        free(mem);
        return 1;
    }

    fex_init(ctx);

    status = fex_try_do_string(ctx, script, NULL, &error);
    if (status != FEX_STATUS_OK) {
        fex_print_error(stderr, &error);
        fe_close(ctx);
        free(mem);
        return 1;
    }

    fe_close(ctx);
    free(mem);
    return 0;
}
```

`fex_do_string()` and `fex_do_file()` are still available for simple tools, but on runtime faults they go through the installed error handler. The default FeX handler prints a traceback and exits.

If you want optional helpers such as `sqrt`, `map`, `filter`, and `makestring`, initialize with `fex_init_with_config(ctx, FEX_CONFIG_ENABLE_EXTENDED_BUILTINS)` instead of plain `fex_init(ctx)`.

## Architecture

FeX is a compiler that targets the `fe` virtual machine. The process is as follows:

1.  **Parsing**: `fex.c` contains a hand-written recursive descent parser (using Pratt parsing for expressions) that consumes FeX source code.
2.  **Compilation**: The parser builds an Abstract Syntax Tree (AST) directly as `fe` S-expressions (lists of objects). For example, the FeX code `let x = 10;` is compiled into the `fe` list `(let x 10)`.
3.  **Evaluation**: The resulting S-expression is passed to `fe_eval()`, which evaluates it using the core `fe` interpreter.

This layered design keeps the VM (`fe.c`) simple and stable, while allowing the user-facing language (`fex.c`) to be expressive and modern.

## Contributing

The library focuses on being lightweight and minimal; pull requests will likely not be merged. Bug reports and questions are welcome.

## License

This library is free software; you can redistribute it and/or modify it under the terms of the MIT license. See `LICENSE` for details.
