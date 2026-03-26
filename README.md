# FeX

FeX is a tiny, embeddable scripting language with a modern, C-like syntax. It's implemented in portable C and is designed for easy integration into other projects.

FeX is built on top of an enhanced version of `fe` core. For details on the new features and improvements, see the [FeX implementation document](doc/FeX-implementation.md) and the [Fe Core Language — 2025 Edition](doc/lang.md).

For full user-facing language documentation, start with the
[FeX Language Guide](doc/FeX-lang.md). For lower-level runtime and
implementation details, see the
[FeX implementation document](doc/FeX-implementation.md), the
[Fe Core Language - 2025 Edition](doc/lang.md), and the
[FeX C API guide](doc/capi.md).

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
*   **Tail-Call Optimization**: Tail-recursive functions run in constant stack space via a trampoline in the evaluator, enabling deep recursion without stack overflow.
*   **Garbage Collection**: A simple and fast mark-and-sweep garbage collector manages the memory arena.
*   **Recoverable Diagnostics**: The CLI and C API can surface structured compile, runtime, and file I/O errors without terminating the host process.
*   **Embeddable C API**: A clean API allows you to easily embed FeX into your C projects, call FeX functions from C, and expose C functions to FeX.
*   **Highly Portable**: The public CMake build targets C99 and is exercised on MSVC, GCC, and Clang.
*   **Compact**: The core evaluator/compiler remains small enough to audit, while optional builtins, import machinery, and diagnostics live in separate translation units.

## Language Documentation

The README gives a short orientation, but the main language reference now lives
in [doc/FeX-lang.md](doc/FeX-lang.md).

- [FeX Language Guide](doc/FeX-lang.md): syntax, values, operators, functions,
  lists, maps, modules, imports, bytes, JSON, builtins, CLI behavior, and
  common gotchas
- [FeX C API guide](doc/capi.md): embedding, structured errors, runtime limits,
  and extending FeX from C
- [Fe Core Language - 2025 Edition](doc/lang.md): the lower-level `fe` forms
  that FeX compiles into
- [FeX implementation document](doc/FeX-implementation.md): implementation
  details, architecture, and portability notes

## Building

FeX uses CMake for building. You will need `cmake` and a C compiler supported by CMake, such as GCC, Clang, or MSVC.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

On single-config generators such as Ninja or Unix Makefiles, omit `--config Debug`. The executable is typically `build/fex` on single-config generators and `build/Debug/fex.exe` on Visual Studio generators.

### Installing

The build now installs both the CLI interpreter and an embedding package:

```bash
cmake --install build --prefix <install-prefix>
```

Installed layout:

- `bin/fex`: interpreter CLI
- `lib/libfex.a` or `lib/fex.lib`: embeddable library
- `include/fex`: public headers (`fe.h`, `fex.h`, `fex_builtins.h`)
- `share/fex/src`: source bundle for vendoring/integration
- `share/fex/doc`: bundled documentation
- `share/fex/examples`: sample FeX scripts

To produce a release archive instead of only an install tree:

```bash
cpack --config build/CPackConfig.cmake -G ZIP
```

Supported package formats by platform:

- Windows: `ZIP` (`msvc` and `mingw` variants)
- macOS: `TGZ`
- Linux: `TGZ`

The GitHub `CI` workflow now validates install and package output on Windows, macOS, and Linux. Windows packaging is built twice so releases contain both `msvc` and `mingw` archives. Pushing a `v*` tag also runs the `Release Packages` workflow, which builds Release archives for all platforms and uploads them to the GitHub release for that tag.

## Usage

The `fex` executable can run a script file, evaluate inline source, read source from stdin, or start an interactive Read-Eval-Print Loop (REPL). The examples below use `<path-to-fex>` to stand in for your generator-specific executable path.

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

### Inline Source

To evaluate a snippet directly from the command line:

```bash
<path-to-fex> -e "println(40 + 2);"
```

`-e` may be repeated; FeX concatenates the snippets with newlines before compiling them.

### Standard Input

If stdin is piped and no file or `-e` input is provided, FeX executes stdin instead of starting the REPL. You can also force stdin mode explicitly with:

```bash
<path-to-fex> -
```

### CLI Flags

Pass `--builtins` to enable the full optional builtins set, or use repeated `--builtin NAME` flags to opt into specific categories such as `string`, `data`, `io`, or the `safe` preset. `--spans` enables richer source-location diagnostics, `--module-path PATH` adds file-based import search directories, `--max-steps N` aborts runaway evaluation after roughly `N` eval steps, `--timeout-ms N` adds a wall-clock timeout, `--max-memory N` aborts when tracked context memory exceeds `N` bytes, `--max-eval-depth N` and `--max-read-depth N` limit recursion depth (default 512, 0 disables), `--json-output` emits structured JSON diagnostics to `stderr` instead of plain text (for pipeline integration), `--stats` prints a runtime stats snapshot to `stderr` after non-REPL execution, and `--version` prints the CLI version. Imports accept bare names (`import settings;`), dotted package names (`import feature.helper;`), and string paths (`import "./helper";`). Imported files act as implicit module scopes, so top-level `export let` / `export fn` populate the imported module directly. Resolution still tries both `name.fex` and `name/index.fex`, so directory-style packages work out of the box. Import specifiers containing `..` path components are rejected to prevent directory traversal. The CLI exits with `65` for compile errors, `70` for runtime errors, and `74` for file I/O errors.

## Language Quick Tour

This section is intentionally brief. For comprehensive language documentation,
see the [FeX Language Guide](doc/FeX-lang.md).

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

### Modules and Packages

Imports can use simple module names, dotted package paths, or explicit string paths:

```c
import settings;
import feature.helper;
import "./local_helper";

println(settings.mode);
println(feature.helper.value);
```

When a file is loaded through `import`, the file itself is an implicit module scope. That means module files can export directly without wrapping everything in `module("name") { ... }`:

```c
// feature/helper.fex
export let value = 41;
```

Directly executed scripts still behave like scripts, so top-level `export` remains reserved for real module contexts.

### JSON, Path, And Filesystem Helpers

The extended builtins set also includes lightweight JSON, path, and filesystem helpers for scripting and config loading:

```c
let cfg = makemap("env", "prod", "port", 8080);
println(tojson([1, 2, 3]));              // [1,2,3]

let path = pathjoin("config", "app.json");
mkdirp("config");
writejson(path, cfg);
println(readjson(path).port);            // 8080
println(listdir("config"));              // ("app.json")
```

### Bytes And Binary I/O

FeX now also has a native `bytes` type for binary-safe file handling:

```c
let payload = tobytes("ABC");
println(payload);                        // #bytes[41 42 43]
println(byteat(payload, 1));             // 66

writebytes("payload.bin", payload);
println(readbytes("payload.bin"));       // #bytes[41 42 43]
```

### Command Capture

With the `system` builtin group enabled, `runcommand()` executes a shell command and returns a map with:

- `code`: the process exit code
- `ok`: `true` when `code == 0`
- `output`: merged stdout/stderr as `bytes`

```c
let proc = runcommand("your-tool --version");
println(proc.code);
println(proc.ok);
println(proc.output);
```

Captured output is currently capped at 4 MiB. Larger command output raises a runtime error instead of growing without bound.

### Structured Process Execution

For non-shell process spawning, use `runprocess(exe, args, opts)`. It launches `exe` directly, so spaces and quoting in `args` are passed as real argv entries instead of being re-parsed by a shell.

- `exe`: string executable path or program name
- `args`: list of strings, or `nil`
- `opts.stdin`: optional string or `bytes`, passed to the child on stdin
- `opts.cwd`: optional working-directory string
- `opts.env`: optional string-valued map of environment overrides
- `opts.stdout`: optional stream mode, one of `"capture"`, `"inherit"`, or `"discard"`
- `opts.stderr`: optional stream mode, one of `"capture"`, `"inherit"`, or `"discard"`
- `opts.max_stdout`: optional per-stream capture limit in bytes, or `0` to disable the cap
- `opts.max_stderr`: optional per-stream capture limit in bytes, or `0` to disable the cap

The result map contains:

- `code`: the process exit code
- `ok`: `true` when `code == 0`
- `stdout`: captured stdout as `bytes`, or `nil` when the stream was inherited or discarded
- `stderr`: captured stderr as `bytes`, or `nil` when the stream was inherited or discarded

```c
let proc = runprocess(
  "python",
  ["-c", "import sys; sys.stdout.write('ok'); raise SystemExit(2)"],
  makemap("env", makemap("MODE", "test"))
);

println(proc.code);    // 2
println(proc.ok);      // false
println(proc.stdout);  // #bytes[6f 6b]
println(proc.stderr);  // #bytes[]
```

By default, each captured output stream is capped at 4 MiB. Use `max_stdout` and `max_stderr` to lower or raise those caps, or set either option to `0` to disable that stream's cap. Use `stdout: "inherit"` to stream child stdout directly to the parent console, and `stderr: "discard"` to drop a stream completely.

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

If you want optional helpers such as `sqrt`, `map`, `filter`, `parsejson`, `pathjoin`, `exists`, `listdir`, `mkdirp`, `cwd`, `getenv`, `runcommand`, or `runprocess`, you can still use `fex_init_with_config(ctx, FEX_CONFIG_ENABLE_EXTENDED_BUILTINS)` for the full set. For production embedding, prefer `fex_init_with_builtins(ctx, flags, mask)` so you can expose only the categories you actually want, for example `FEX_BUILTINS_SAFE`, `FEX_BUILTINS_IO`, or `FEX_BUILTINS_SYSTEM`.

For runtime sandboxing, the core `fe` API now also exposes `fe_set_step_limit(ctx, max_steps)`, `fe_set_memory_limit(ctx, max_bytes)`, `fe_set_timeout_ms(ctx, timeout_ms)`, `fe_set_interrupt_handler(...)`, and `fe_poll_abort(ctx)`. Hosts can inspect `fe_get_memory_used(ctx)`, `fe_get_peak_memory_used(ctx)`, or take a full `fe_get_stats(ctx, &stats)` snapshot to observe current runtime state. Use the fixed step and memory limits for simple sandboxing, the timeout convenience layer for wall-clock deadlines, an interrupt callback for custom cancellation policy, and `fe_poll_abort(ctx)` inside long-running native helpers that need to honor those limits after their own cleanup.

When installed, the package also exports a CMake package and a `pkg-config` file:

```cmake
find_package(fex CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE fex::fex)
```

```bash
pkg-config --cflags --libs fex
```

## Architecture

FeX is a compiler that targets the `fe` virtual machine. The process is as follows:

1.  **Parsing**: `fex.c` contains a hand-written recursive descent parser (using Pratt parsing for expressions) that consumes FeX source code.
2.  **Compilation**: The parser builds an Abstract Syntax Tree (AST) directly as `fe` S-expressions (lists of objects). For example, the FeX code `let x = 10;` is compiled into the `fe` list `(let x 10)`.
3.  **Evaluation**: The resulting S-expression is passed to `fe_eval()`, which evaluates it using the core `fe` interpreter.

The evaluator implements trampoline-based tail-call optimization: when a function call is in tail position (the last expression in a function body, `if`/`else` branch, or `do` block), the evaluator reuses the current C stack frame instead of recursing. Combined with FeX's recursive binding rewrite for named function declarations, this lets both direct and mutually recursive named functions run for millions of iterations in constant stack space.

This layered design keeps the VM (`fe.c`) simple and stable, while allowing the user-facing language (`fex.c`) to be expressive and modern.

## Contributing

The library focuses on being lightweight and minimal; pull requests will likely not be merged. Bug reports and questions are welcome.

## License

This library is free software; you can redistribute it and/or modify it under the terms of the MIT license. See `LICENSE` for details.
