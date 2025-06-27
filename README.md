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
*   **Rich Data Types**: Numbers (doubles and fixnums), Strings, `nil`, Booleans, and Pairs (for lists).
*   **Zero-Malloc Runtime**: Operates within a single, fixed-size memory arena. No `malloc`/`free` calls are made during script execution, making it highly predictable.
*   **Garbage Collection**: A simple and fast mark-and-sweep garbage collector manages the memory arena.
*   **Great Error Reporting**: The parser provides precise error messages with source file line and column numbers.
*   **Embeddable C API**: A clean API allows you to easily embed FeX into your C projects, call FeX functions from C, and expose C functions to FeX.
*   **Highly Portable**: Written in pure ANSI C (C89), it compiles on any standard-compliant C compiler.
*   **Concise**: The entire implementation (parser, compiler, and VM) is approximately 1800 lines of code.

## Building

FeX uses CMake for building. You will need `cmake` and a C89-compatible compiler (like GCC or Clang).

```bash
# 1. Create a build directory
mkdir build
cd build

# 2. Configure the project
cmake ..

# 3. Build the project
make
```

This will create an executable named `fex` in the `build` directory.

## Usage

The `fex` can be used to run a script file or to start an interactive Read-Eval-Print Loop (REPL).

### REPL

To start the REPL, run `fex` with no arguments:

```bash
./build/fex
FeX v1.0 (Modern Syntax Layer for fe)
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
./build/fex your_script.fex
```

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

## Embedding API

FeX is easy to embed. Here is a minimal example of running a FeX script from C.

```c
#include <stdio.h>
#include <stdlib.h>

#include "fe.h"
#include "fex.h"

#define MEMORY_POOL_SIZE (1024 * 1024) // 1MB

int main() {
    // 1. Allocate a memory pool for the interpreter.
    void* mem = malloc(MEMORY_POOL_SIZE);
    if (!mem) return 1;

    // 2. Open a fe_Context within the pool.
    fe_Context *ctx = fe_open(mem, MEMORY_POOL_SIZE);

    // 3. Initialize the FeX environment (registers built-ins like println).
    fex_init(ctx);

    // 4. Compile and run a string of code.
    const char *script = "println('Hello from embedded FeX!');";
    fex_do_string(ctx, script);

    // 5. Clean up.
    fe_close(ctx);
    free(mem);

    return 0;
}
```

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