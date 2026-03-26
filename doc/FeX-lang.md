# FeX Language Guide

FeX is the user-facing curly-brace language that runs on top of the `fe`
runtime. This guide documents the language you write in `.fex` files, in
inline `-e` snippets, and in the REPL.

Use the other documents in `doc/` for adjacent topics:

- `doc/lang.md`: the lower-level Fe core language and evaluation model
- `doc/FeX-implementation.md`: implementation-focused notes
- `doc/capi.md`: embedding and extending FeX from C

## 1. What FeX looks like

```fex
fn reverse(lst) {
  let out = nil;
  while (lst) {
    out = cons(car(lst), out);
    lst = cdr(lst);
  }
  out;
}

let animals = ["cat", "dog", "fox"];
println(reverse(animals));   // (fox dog cat)
```

FeX is:

- expression-oriented: blocks, `if`, and function bodies all produce values
- lexically scoped: closures capture surrounding bindings
- dynamically typed: values carry their own runtime type
- pair/list based: lists are built from pairs rather than array objects
- small but practical: maps, bytes, JSON, filesystem, and process helpers are
  available through opt-in builtin groups

## 2. Core language model

The most important rules are:

1. Everything evaluates to a value.
2. Only `false` and `nil` are falsey. `0`, `""`, `[]`, and maps are truthy.
3. Blocks return the value of their last expression. An empty block returns
   `nil`.
4. Functions capture lexical bindings by reference, so mutation is visible to
   every closure sharing the same scope.
5. Lists are linked lists built from pairs. Use maps for named fields and
   `bytes` for binary data.

## 3. Lexical syntax

### Comments and whitespace

- Whitespace is insignificant except as a token separator.
- `//` starts a line comment that runs to the end of the line.
- There is no block-comment syntax.

```fex
let answer = 40 + 2;   // this is a comment
```

### Identifiers

Identifiers:

- start with a letter or `_`
- may continue with letters, digits, or `_`
- are case-sensitive

Examples:

- `name`
- `snake_case`
- `_private`
- `Point3D`

Reserved keywords:

- `module`
- `export`
- `import`
- `let`
- `fn`
- `return`
- `if`
- `else`
- `while`
- `true`
- `false`
- `nil`
- `and`
- `or`

### Numbers

FeX accepts:

- decimal integers: `0`, `42`, `123456`
- hexadecimal integers: `0xFF`, `0x10`
- decimal floating-point numbers: `3.14`, `0.5`
- exponent notation: `6.022e23`, `1e-9`

Negative numbers are parsed as unary minus applied to a positive literal:

```fex
let a = -3;
let b = -1.25e2;
```

### Strings

Strings use double quotes:

```fex
let msg = "hello";
```

Supported escapes:

- `\n`
- `\t`
- `\r`
- `\\`
- `\"`
- `\0`

Unknown escapes are kept literally:

```fex
println("x\q");   // the backslash is preserved
```

### Semicolons and statement boundaries

Use `;` after:

- expression statements
- `let` declarations
- `import` statements
- `return` statements

Do not use `;` after:

- `fn name(...) { ... }` declarations
- `if (...) ... else ...`
- `while (...) ...`
- `{ ... }` blocks used as statements
- `module("name") { ... }`

## 4. Values and data types

| Kind | Example | Notes |
| --- | --- | --- |
| `nil` | `nil` | Singleton empty/falsey value and the empty list. |
| Boolean | `true`, `false` | Only `false` and `nil` are falsey. |
| Number | `42`, `3.14`, `0xFF` | Backed by fixnums or doubles internally. |
| String | `"hello"` | Text value. |
| Pair | `1 :: 2` | The primitive linked structure. |
| List | `[1, 2, 3]` | Sugar over a proper pair list. |
| Map | `makemap("host", "localhost")` | Mutable string/symbol keyed object-style table. |
| Bytes | `#bytes[...]` when printed | Binary-safe sequence of bytes. |
| Function | `fn(x) { x + 1; }` | First-class closure. |
| Module | imported or created via `module(...)` | Runtime representation is a map. |

## 5. Variables, scope, and assignment

Declare a variable with `let`:

```fex
let name = "FeX";
let count = 0;
let uninitialized;
```

`let` creates a binding in the current scope. Scopes are introduced by:

- the top level of a file or REPL input
- blocks: `{ ... }`
- function bodies
- module bodies

Assignment updates an existing binding:

```fex
let x = 1;
x = x + 1;
println(x);   // 2
```

Closures capture variables by reference:

```fex
fn make_counter() {
  let count = 0;
  fn next() {
    count = count + 1;
    count;
  }
  next;
}

let c = make_counter();
println(c());   // 1
println(c());   // 2
```

Named function declarations are hoisted within their enclosing block or
function body so mutually recursive local functions work:

```fex
fn parity(n) {
  fn even(x) {
    if (x <= 0) { true; } else { odd(x - 1); }
  }

  fn odd(x) {
    if (x <= 0) { false; } else { even(x - 1); }
  }

  even(n);
}
```

## 6. Expressions and operators

### Operator overview

| Category | Syntax | Notes |
| --- | --- | --- |
| Call | `f(a, b)` | Highest-precedence surface operator. |
| Property access | `obj.field` | Field name must be an identifier. |
| Unary | `-x`, `!flag` | `!` maps to logical `not`. |
| Multiplicative | `a * b`, `a / b` | Numeric operators. |
| Additive | `a + b`, `a - b` | Numeric operators. |
| Cons | `a :: b` | Right-associative pair construction. |
| Comparison | `<`, `<=`, `>`, `>=` | Numeric comparisons. |
| Equality | `==`, `!=` | Uses Fe's `is` equality semantics. |
| Logical | `and`, `or` | Short-circuiting. |
| Assignment | `x = y` | Lowest precedence. |

Notes:

- `==` compares numbers and strings by value; most other heap objects compare
  by identity.
- `::` is right-associative, so `1 :: 2 :: nil` means `1 :: (2 :: nil)`.
- When mixing `::` with arithmetic, add parentheses for clarity.
- Property access is static. For dynamic keys use `mapget(obj, key)` or
  `mapset(obj, key, value)`.

### Arithmetic and logical expressions

```fex
println(1 + 2 * 3);           // 7
println((1 + 2) * 3);         // 9
println(true and false);      // false
println(nil or "fallback");   // fallback
println(!false);              // true
```

### Assignment targets

FeX accepts assignment to:

- a variable: `x = 1;`
- a map/module property: `cfg.port = 8080;`
- a pair selector: `pair.head = 10;`, `pair.tail = nil;`

Invalid assignment targets are compile errors.

## 7. Control flow and blocks

### `if` / `else`

`if` is an expression:

```fex
let label =
  if (score >= 90) {
    "excellent";
  } else {
    "keep going";
  };
```

Because `if` produces a value, it works naturally in assignments and returns.

### `while`

`while` repeats while its condition stays truthy:

```fex
let i = 3;
while (i > 0) {
  println(i);
  i = i - 1;
}
```

### Blocks

Blocks are expressions:

```fex
let value = {
  let base = 40;
  base + 2;
};

println(value);   // 42
```

An empty block returns `nil`.

### `return`

`return` exits the nearest enclosing function immediately:

```fex
fn first_big(lst, threshold) {
  while (lst) {
    let v = car(lst);
    if (v > threshold) { return v; }
    lst = cdr(lst);
  }
  nil;
}
```

If `return` is omitted, a function returns the value of its last body
expression. An empty function body returns `nil`.

## 8. Functions and closures

### Function declarations

```fex
fn add(a, b) {
  a + b;
}
```

### Function expressions

```fex
let twice = fn(x) {
  x * 2;
};
```

### Argument behavior

- arguments are positional
- missing arguments evaluate as `nil`
- extra arguments are ignored

```fex
fn show(a, b) {
  println(a);
  println(b);
}

show(1);          // prints 1, then nil
show(1, 2, 3);    // prints 1, then 2
```

### Tail calls

FeX performs tail-call optimization for calls in tail position, including
direct and mutual recursion. This lets tail-recursive functions run in
constant C stack space:

```fex
fn sum(n, acc) {
  if (n <= 0) { return acc; }
  return sum(n - 1, acc + n);
}
```

## 9. Lists and pairs

### List literals

```fex
let xs = [1, 2, 3];
println(xs);   // (1 2 3)
```

`[a, b, c]` is surface syntax for building a proper list.

### Pair construction with `::`

```fex
let xs = 1 :: 2 :: 3 :: nil;
println(xs);   // (1 2 3)
```

### Core list primitives

Always-available primitives from the Fe core include:

- `list`
- `cons`
- `car`
- `cdr`
- `setcar`
- `setcdr`

Example:

```fex
let xs = ["a", "b", "c"];
println(car(xs));          // a
println(cdr(xs));          // (b c)
println(cons("z", xs));    // (z a b c)
```

### Pair selector sugar

Pairs support dedicated selector names:

- `.head` or `.first` -> `car`
- `.tail` or `.rest` -> `cdr`

These selectors are readable and assignable:

```fex
let pair = 1 :: 2 :: 3 :: nil;

println(pair.head);         // 1
println(pair.tail.head);    // 2

pair.head = 10;
pair.tail = 20 :: 30 :: nil;
println(pair);              // (10 20 30)
```

Important limits:

- only `.head`, `.first`, `.tail`, and `.rest` are special on pairs
- `pair.foo` is a runtime error
- property access on non-map, non-pair values is also a runtime error

## 10. Maps and object-style data

Maps are mutable object-style containers for string or symbol keys.

```fex
let cfg = makemap("host", "localhost", "port", 8080);

println(cfg.host);              // localhost
cfg.host = "127.0.0.1";
println(mapget(cfg, "host"));   // 127.0.0.1
println(maphas(cfg, "port"));   // true
println(mapcount(cfg));         // 2
```

Map helpers in the `data` builtin group:

- `makemap`
- `mapset`
- `mapget`
- `maphas`
- `mapdelete`
- `mapkeys`
- `mapcount`

Use dot syntax when the property name is static and identifier-shaped:

```fex
cfg.mode = "debug";
println(cfg.mode);
```

Use `mapget` / `mapset` when the key is computed:

```fex
let key = "port";
println(mapget(cfg, key));
mapset(cfg, key, 9090);
```

Modules use the same runtime representation, so imported modules and explicit
`module(...)` values can be read and written with the same property syntax.

## 11. Modules and imports

FeX supports two related module styles:

- explicit `module("name") { ... }` declarations
- implicit file modules loaded with `import`

### Explicit modules

```fex
module("math") {
  export fn square(n) { n * n; }
  export let pi = 3.14159;
}

println(math.square(9));
println(math.pi);
```

Rules:

- `module("name") { ... }` binds a module value under `name`
- `export` may decorate only `let` and `fn` declarations
- module values are maps at runtime

### File modules and `import`

Imports may use:

- bare names: `import settings;`
- dotted package paths: `import feature.helper;`
- explicit string paths: `import "./local_helper";`

Example:

```fex
import settings;
import feature.helper;
import "./local_helper";

println(settings.mode);
println(feature.helper.value);
println(local_helper.answer);
```

Imported files behave as implicit module scopes. That means a file can export
directly at top level without wrapping itself in `module(...)`:

```fex
// helper.fex
export let answer = 42;
export fn greet(name) {
  "hello " + name;
}
```

### Import resolution

Import lookup tries:

1. the importing file's directory
2. configured module search paths
3. the current working directory

For a bare or dotted specifier, FeX tries both:

- `name.fex`
- `name/index.fex`

Examples:

- `import settings;` -> `settings.fex` or `settings/index.fex`
- `import feature.helper;` -> `feature/helper.fex` or
  `feature/helper/index.fex`
- `import "./helper";` keeps the explicit relative path and resolves it from
  the importing file first

Security rule:

- import specifiers containing `..` path components are rejected

Important distinction:

- top-level `export` is valid in real module contexts
- directly executed scripts are not implicit module scopes

## 12. Bytes and JSON

### Bytes

Use `bytes` for binary-safe data:

```fex
let payload = tobytes("ABC");

println(typeof(payload));     // bytes
println(byteslen(payload));   // 3
println(byteat(payload, 1));  // 66
println(byteslice(payload, 1, 3));
```

Relevant `data`/`io` builtins:

- `makebytes`
- `tobytes`
- `byteslen`
- `byteat`
- `byteslice`
- `readbytes`
- `writebytes`

### JSON

FeX can convert between runtime values and JSON text:

```fex
let doc = makemap("name", "fex", "items", [1, 2, 3]);
let text = tojson(doc);
let parsed = parsejson(text);

println(text);
println(parsed.items.head);   // 1
```

File helpers:

- `readjson(path)`
- `writejson(path, value)`

Typical mappings:

- JSON objects -> maps
- JSON arrays -> lists
- JSON strings -> strings
- JSON numbers -> numbers
- JSON booleans -> booleans
- JSON `null` -> `nil`

## 13. Builtins and capability groups

### Always available

`fex_init()` installs FeX-specific I/O helpers:

- `print`
- `println`
- `readline`
- `readnumber`

The underlying Fe core also provides the standard primitives used throughout
the language, including:

- `list`, `cons`, `car`, `cdr`, `setcar`, `setcdr`
- `atom`, `is`, `not`
- `+`, `-`, `*`, `/`, `<`, `<=`

### Optional builtin groups

FeX keeps higher-level helpers behind explicit capability groups.

| Group | Functions |
| --- | --- |
| `math` | `sqrt`, `sin`, `cos`, `tan`, `abs`, `floor`, `ceil`, `round`, `min`, `max`, `pow`, `log`, `rand`, `seedrand`, `randint`, `randbytes` |
| `string` | `strlen`, `upper`, `lower`, `concat`, `substring`, `split`, `trim`, `contains`, `makestring` |
| `list` | `length`, `nth`, `append`, `reverse`, `map`, `filter`, `fold` |
| `io` | `pathjoin`, `dirname`, `basename`, `exists`, `listdir`, `mkdir`, `mkdirp`, `readfile`, `readbytes`, `writefile`, `writebytes`, `readjson`, `writejson` |
| `data` | `makemap`, `mapset`, `mapget`, `maphas`, `mapdelete`, `mapkeys`, `mapcount`, `makebytes`, `tobytes`, `byteslen`, `byteat`, `byteslice`, `parsejson`, `tojson` |
| `system` | `cwd`, `chdir`, `getenv`, `time`, `exit`, `system`, `runcommand`, `runprocess` |
| `type` | `typeof`, `tostring`, `tonumber`, `isnil`, `isnumber`, `isstring`, `isbytes`, `islist`, `ismap` |

Presets:

- `safe` = `math`, `string`, `list`, `type`, `data`
- `all` = every group

CLI examples:

```bash
fex --builtins script.fex
fex --builtin safe script.fex
fex --builtin string --builtin io script.fex
fex --builtin string,io,data script.fex
```

Embedding equivalents:

```c
fex_init_with_builtins(ctx, FEX_CONFIG_NONE, FEX_BUILTINS_SAFE);
fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS,
                       FEX_BUILTINS_STRING | FEX_BUILTINS_IO);
```

## 14. Filesystem and process helpers

The `io` and `system` groups make FeX useful for scripting and tooling.

### Filesystem example

```fex
mkdirp(pathjoin("tmp", "logs"));
writefile(pathjoin("tmp", "logs", "note.txt"), "hello");

println(exists("tmp"));
println(listdir(pathjoin("tmp", "logs")));
```

### `runcommand`

`runcommand(command)` executes through the platform shell and returns a map:

- `code`: exit code
- `ok`: `true` when `code == 0`
- `output`: merged stdout/stderr as `bytes`

```fex
let proc = runcommand("your-tool --version");
println(proc.code);
println(proc.ok);
println(proc.output);
```

### `runprocess`

`runprocess(exe, args, opts)` launches a program directly instead of going
through a shell.

Arguments:

- `exe`: string executable path or program name
- `args`: list of strings, or `nil`
- `opts`: map, or `nil`

Supported option keys:

- `stdin`
- `cwd`
- `env`
- `stdout`
- `stderr`
- `max_stdout`
- `max_stderr`

Result keys:

- `code`
- `ok`
- `stdout`
- `stderr`

Example:

```fex
let proc = runprocess(
  "python",
  ["-c", "import sys; sys.stdout.write('ok')"],
  makemap("stdout", "capture", "stderr", "capture")
);

println(proc.code);
println(proc.ok);
println(proc.stdout);
```

Use `runprocess` when you want structured argv handling. Use `runcommand` when
you specifically want shell parsing, shell syntax, or shell builtins.

## 15. REPL, files, and CLI behavior

The `fex` executable can:

- start a REPL with no arguments
- execute a script file
- evaluate inline source with `-e`
- read source from stdin

Helpful CLI flags:

- `--spans`: include richer source locations in diagnostics
- `--module-path PATH` or `-I PATH`: add import search directories
- `--max-steps N`: limit evaluator work
- `--timeout-ms N`: wall-clock timeout
- `--max-memory N`: cap tracked runtime memory
- `--max-eval-depth N`: limit evaluation recursion depth
- `--max-read-depth N`: limit reader nesting depth
- `--json-output`: emit structured JSON diagnostics
- `--stats`: print runtime statistics after non-REPL execution

Exit codes:

- `65`: compile error
- `70`: runtime error
- `74`: file I/O error

## 16. Common gotchas

- Only `false` and `nil` are falsey.
- Lists are linked lists, not arrays. `nth` is linear-time.
- Dot syntax uses a fixed identifier after `.`. For computed keys, use
  `mapget` / `mapset`.
- On pairs, only `.head`, `.first`, `.tail`, and `.rest` are valid selectors.
- `print` does not add a newline. `println` does.
- Use `bytes` for binary data. Strings are text-oriented.
- `export` only applies to `let` and `fn`, and only in module contexts.
- When mixing `::` with other operators, parenthesize.

## 17. Where to go next

- For the lower-level Fe core forms that FeX compiles into, read `doc/lang.md`.
- For embedding, structured error handling, and host integration, read
  `doc/capi.md`.
- For implementation details and runtime architecture, read
  `doc/FeX-implementation.md`.
