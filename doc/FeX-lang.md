# FeX Programming Language

*A small curly-brace language that compiles to the Fe core AST.*

## 1. Overview

FeX is a front-end for the `fe` runtime. Its job is to turn a modern surface syntax into ordinary `fe_Object *` lists that the core evaluator already understands.

Key properties:

- Everything is an expression.
- Lists and ASTs are built from pairs.
- Dot syntax compiles to `get`, with pair-specific selector sugar for `.head`, `.first`, `.tail`, and `.rest`.
- The `::` operator is right-associative sugar for `cons`.

## 2. Lexical Grammar

| Category | Examples | Notes |
| --- | --- | --- |
| Identifiers | `snake_case`, `Point3D`, `_private` | Letters, digits, and `_` after the first character. |
| Keywords | `module`, `export`, `import`, `let`, `fn`, `return`, `if`, `else`, `while`, `true`, `false`, `nil`, `and`, `or` | Reserved words. |
| Numbers | `42`, `-3`, `0xFF`, `3.14`, `6.022e23` | Hex uses `0x`; decimals support exponent notation. |
| Strings | `"hello\nworld"` | Double quotes with C-style escapes. |
| Comments | `// until end-of-line` | Ignored by the parser. |
| Punctuation | `()[]{},;.:` | `:` only appears as part of `::`. |

Whitespace is otherwise insignificant.

## 3. Values and Data Shapes

FeX has one mutable heap object: the pair `(car . cdr)`. Higher-level structures are layered on top of it.

| Value | Example | Notes |
| --- | --- | --- |
| `nil` | `nil` | Singleton value and empty list. |
| Boolean | `true`, `false` | Only `false` and `nil` are falsey. |
| Fixnum | `123` | Pointer-tagged integer when it fits. |
| Double | `3.14` | Boxed number when not representable as a fixnum. |
| String | `"FeX"` | Arena-backed string object; the default build stores string bytes in slabs. |
| Pair | `1 :: 2` | Equivalent to `cons(1, 2)`. |
| List | `[1, 2, 3]` | Equivalent to `list(1, 2, 3)`. |
| Symbol | implicit | Identifiers become interned symbols in compiled forms. |

## 4. Expressions and Operators

| Category | Example | Internal Form |
| --- | --- | --- |
| Primary | `x`, `42`, `"hi"` | literal or symbol |
| Grouping | `(expr)` | `expr` |
| List | `[a, b]` | `(list a b)` |
| Cons | `a :: b` | `(cons a b)` |
| Unary | `-n`, `!cond` | `(- n)`, `(not cond)` |
| Binary | `a + b * c` | nested arithmetic calls |
| Equality | `a == b`, `a != b` | `(is a b)`, `(not (is a b))` |
| Compare | `< <= > >=` | `>` and `>=` flip operands into `<` and `<=` |
| Logical | `and`, `or` | `(and ...)`, `(or ...)` |
| Assign | `x = y` | `(= x y)` |
| Call | `f(a, b)` | `(f a b)` |
| Member | `obj.field` | `(get obj field)` |

On pair values, the following selector names are special:

- `.head` and `.first` read the first element.
- `.tail` and `.rest` read the cdr.
- The same selectors may appear on the left side of `=`:

```fex
let pair = 1 :: 2 :: nil;
pair.head = 10;
pair.tail = 20 :: nil;
```

## 5. Statements

Statements are expressions terminated by `;`, except for blocks and declarations whose syntax already delimits them.

| Syntax | Meaning |
| --- | --- |
| `expr;` | Evaluate for side effects. |
| `let x = expr;` | Declare a local or top-level binding. |
| `fn name(params) { body }` | Declare a named function. |
| `return expr;` | Exit the current function with `expr`. |
| `if (cond) stmt else stmt` | Conditional expression. |
| `while (cond) stmt` | Loop while `cond` stays truthy. |
| `{ ... }` | Block expression whose value is the last inner expression. |

If `return` is omitted, a function returns `nil`.

## 6. Functions and Closures

Functions are first-class and capture lexical bindings by reference.

```fex
fn make_counter() {
  let count = 0;

  fn counter() {
    count = count + 1;
    return count;
  }

  return counter;
}
```

Parameters are positional. Missing arguments evaluate as `nil`; extra arguments are ignored.

## 7. Modules

```fex
module ("math") {
  export fn square(n) { n * n }
  export let pi = 3.14159;
}

import math;

println(math.square(9));
println(math.pi);
```

Module behavior:

- `module("name") { ... }` executes the body and binds the resulting export table under `name`.
- `export` may decorate `let` and `fn` declarations inside a module.
- `import ident;` is a compile-time directive that makes the module name available in source.

At runtime, a module is still just a value. Dot access uses `get`.

## 8. Builtins

The base FeX environment always includes:

| Name | Meaning |
| --- | --- |
| `print` | Print values without a trailing newline. |
| `println` | Print values followed by a newline. |
| `list`, `car`, `cdr`, `cons` | Pair and list primitives. |
| `setcar`, `setcdr` | Pair mutation primitives. |
| `atom`, `is`, `not` | Basic predicates. |
| `+`, `-`, `*`, `/`, `<`, `<=` | Numeric primitives. |

Pair selectors such as `.head` and `.tail`, and the `::` operator, are syntax sugar over these primitives rather than separate runtime functions.

Optional helpers such as `sqrt`, `map`, `filter`, `parsejson`, `readjson`, `pathjoin`, `tobytes`, and `readbytes` are part of the extended builtins set. In the CLI, enable the full set with `--builtins`, or opt into specific capability groups with repeated `--builtin NAME` flags such as `--builtin safe` or `--builtin string,data`. For runaway-script protection, the CLI also supports `--max-steps N` and `--timeout-ms N`. In embedded use, call:

```c
fex_init_with_config(ctx, FEX_CONFIG_ENABLE_EXTENDED_BUILTINS);
```

For finer-grained embedding, use:

```c
fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS,
    FEX_BUILTINS_STRING | FEX_BUILTINS_DATA);
```

## 9. Execution and Memory

- Evaluation is eager and left-to-right, except for special forms such as `if`, `and`, `or`, `fn`, and `while`.
- Only `false` and `nil` are falsey.
- The collector is mark-and-sweep, and objects do not move after allocation.
- Ordinary function calls use the C stack. Prefer `while` for unbounded loops.

## 10. Embedding Cheat Sheet

| Goal | API |
| --- | --- |
| Open a VM | `ctx = fe_open(buf, size); fex_init(ctx);` |
| Run source once | `fex_do_string(ctx, "code");` |
| Compile then reuse | `fe_Object *ast = fex_compile(ctx, src); fe_eval(ctx, ast);` |
| Enable all optional builtins | `fex_init_with_config(ctx, FEX_CONFIG_ENABLE_EXTENDED_BUILTINS);` |
| Enable selected builtin groups | `fex_init_with_builtins(ctx, FEX_CONFIG_ENABLE_SPANS, FEX_BUILTINS_SAFE);` |
| Bound evaluation work | `fe_set_step_limit(ctx, 100000);` |
| Add a wall-clock timeout | `fe_set_timeout_ms(ctx, 250);` |
| Install custom error behavior | Replace `fe_handlers(ctx)->error` before running code. |

Compiled ASTs belong to the `fe_Context` that created them. Reuse them within that same context; do not pass them to another context or thread.

## 11. Examples

### Pair Selectors

```fex
let pair = 1 :: 2 :: 3 :: nil;
println(pair.head);
println(pair.tail.head);
pair.head = 10;
println(pair);
```

### Modules as Namespaces

```fex
module ("config") {
  export let host = "localhost";
  export let port = 8080;
}

println(config.host);
println(config.port);
```

### JSON And Config Files

```fex
let cfg = makemap("mode", "prod", "workers", 4);
let path = pathjoin("build", "app.json");

writejson(path, cfg);
println(readjson(path).workers);
```

### Bytes And Binary Files

```fex
let payload = tobytes("ABC");
println(payload);            // #bytes[41 42 43]
println(byteat(payload, 2)); // 67

writebytes("payload.bin", payload);
println(readbytes("payload.bin"));
```

### REPL Workflow

Every block returns its last value, so short experiments work naturally at the REPL without wrapping everything in `print`.
