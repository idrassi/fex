#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import difflib
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BUDGET_LOOP_SOURCE = "while (true) { }\n"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="backslashreplace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(errors="backslashreplace")

def fex_string_literal(text: str) -> str:
    escaped = (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'

def runcommand_case_source() -> str:
    if sys.platform.startswith("win"):
        script = (
            "[Console]::OpenStandardOutput().Write([byte[]](111,117,116),0,3);"
            "[Console]::OpenStandardError().Write([byte[]](101,114,114),0,3);"
            "exit 3"
        )
        encoded = base64.b64encode(script.encode("utf-16le")).decode("ascii")
        command = f"powershell -NoProfile -EncodedCommand {encoded}"
    else:
        command = "sh -c 'printf out; printf err >&2; exit 3'"
    return (
        f"let proc = runcommand({fex_string_literal(command)});\n"
        "println(proc.code);\n"
        "println(proc.ok);\n"
        "println(proc.output);\n"
    )


def shell_sleep_command() -> str:
    if sys.platform.startswith("win"):
        script = "Start-Sleep -Seconds 5"
        encoded = base64.b64encode(script.encode("utf-16le")).decode("ascii")
        return f"powershell -NoProfile -EncodedCommand {encoded}"
    return "sleep 5"


def runcommand_timeout_case_source() -> str:
    return f"runcommand({fex_string_literal(shell_sleep_command())});\n"


def system_timeout_case_source() -> str:
    return f"system({fex_string_literal(shell_sleep_command())});\n"


def runprocess_case_source() -> str:
    python_exe = str(Path(sys.executable).resolve())
    cwd = str((ROOT / "scripts").resolve())
    script = (
        "import os, pathlib, sys; "
        "data = sys.stdin.buffer.read(); "
        "sys.stdout.buffer.write(data.upper()); "
        "sys.stderr.buffer.write(os.getenv('FEX_TEST', 'missing').encode('ascii')); "
        "sys.stderr.buffer.write(b'@'); "
        "sys.stderr.buffer.write(pathlib.Path().resolve().name.encode('ascii')); "
        "raise SystemExit(5)"
    )
    return (
        f"let proc = runprocess({fex_string_literal(python_exe)}, "
        f"[\"-c\", {fex_string_literal(script)}], "
        f"makemap(\"stdin\", tobytes(\"abc\"), \"cwd\", {fex_string_literal(cwd)}, "
        f"\"env\", makemap(\"FEX_TEST\", \"env\")));\n"
        "println(proc.code);\n"
        "println(proc.ok);\n"
        "println(proc.stdout);\n"
        "println(proc.stderr);\n"
    )

def runprocess_modes_case_source() -> str:
    python_exe = str(Path(sys.executable).resolve())
    script = (
        "import sys; "
        "print('inherit-out'); "
        "sys.stderr.write('discard-err\\n')"
    )
    return (
        f"let proc = runprocess({fex_string_literal(python_exe)}, "
        f"[\"-c\", {fex_string_literal(script)}], "
        'makemap("stdout", "inherit", "stderr", "discard"));\n'
        "println(proc.code);\n"
        "println(is(proc.stdout, nil));\n"
        "println(is(proc.stderr, nil));\n"
    )


def runprocess_limit_case_source() -> str:
    python_exe = str(Path(sys.executable).resolve())
    script = "import sys; sys.stdout.write('abcdef')"
    return (
        f"runprocess({fex_string_literal(python_exe)}, "
        f"[\"-c\", {fex_string_literal(script)}], "
        'makemap("max_stdout", 4));\n'
    )


def runprocess_timeout_case_source() -> str:
    python_exe = str(Path(sys.executable).resolve())
    script = "import time; time.sleep(5)"
    return (
        f"runprocess({fex_string_literal(python_exe)}, "
        f"[\"-c\", {fex_string_literal(script)}], "
        'makemap("stdout", "discard", "stderr", "discard"));\n'
    )


def fs_helpers_case_source() -> str:
    return (
        'println("--- FS Helpers Regression ---");\n'
        'println("mkdir.workspace:", mkdir("workspace"));\n'
        'println("chdir.workspace:", chdir("workspace"));\n'
        'println("mkdir.plain:", mkdir("plain"));\n'
        'println("mkdirp.nested:", mkdirp(pathjoin("sandbox", "inner")));\n'
        'writefile(pathjoin("sandbox", "note.txt"), "hi");\n'
        'println("exists.missing:", exists("missing"));\n'
        'println("exists.plain:", exists("plain"));\n'
        'println("exists.inner:", exists(pathjoin("sandbox", "inner")));\n'
        'println("exists.note:", exists(pathjoin("sandbox", "note.txt")));\n'
        'println("listdir.root:", listdir("."));\n'
        'println("listdir.sandbox:", listdir("sandbox"));\n'
        'println("env:", getenv("FEX_TEST_ENV"));\n'
        'println("chdir.inner:", chdir(pathjoin("sandbox", "inner")));\n'
        'println("cwd.base:", basename(cwd()));\n'
    )


def long_module_name_case_source() -> str:
    name = "mod_" + ("a" * 140)
    return (
        f'module ("{name}") {{ export let value = 7; }}\n'
        f"println({name}.value);\n"
    )


def long_module_name_collision_case_source() -> str:
    prefix = "mod_" + ("a" * 136)
    name_one = prefix + "x"
    name_two = prefix + "y"
    return (
        f'module ("{name_one}") {{ export let value = 1; }}\n'
        f'module ("{name_two}") {{ export let value = 2; }}\n'
        f"println({name_one}.value);\n"
        f"println({name_two}.value);\n"
    )

CASES = [
    {
        "name": "basic module",
        "script": ROOT / "scripts" / "test_basic_module.fex",
        "exit_code": 0,
        "stdout": (
            "--- Running Basic Module Test ---\n"
            "clamping 5.5 between 0 and 10: 5.5\n"
            "clamping -5 between 0 and 10: 0\n"
            "clamping 15 between 0 and 10: 10\n"
            "\n"
        ),
    },
    {
        "name": "module as value",
        "script": ROOT / "scripts" / "test_module_as_value.fex",
        "exit_code": 0,
        "stdout": (
            "--- Running Module as First-Class Value Test ---\n"
            "Accessing original config.version: 1.0\n"
            "Accessing alias app_config.version: 1.0\n"
            "Accessing alias app_config.author: Fex Team\n"
            "Is 'config' the same as 'app_config'? true\n"
            "\n"
        ),
    },
    {
        "name": "pair selectors",
        "script": ROOT / "scripts" / "test_pair_selectors.fex",
        "exit_code": 0,
        "stdout": (
            "--- Pair Selector Regression ---\n"
            "pair:(1 2 3)\n"
            "pair.head:1\n"
            "pair.first:1\n"
            "pair.tail:(2 3)\n"
            "pair.rest.head:2\n"
            "mutated pair:(10 20 30)\n"
            "mutated pair.head:10\n"
            "mutated pair.tail.head:20\n"
            "selector_names.head:module-head\n"
            "selector_names.tail:module-tail\n"
        ),
    },
    {
        "name": "module imports",
        "script": ROOT / "scripts" / "test_module_imports.fex",
        "args": ["--module-path", ROOT / "scripts" / "import_modules"],
        "exit_code": 0,
        "stdout": (
            "--- Import Runtime Regression ---\n"
            "loading app\n"
            "loading helper\n"
            "42\n"
            "41\n"
        ),
    },
    {
        "name": "module import aliasing",
        "source": (
            'println("--- Import Alias Regression ---");\n'
            "import app;\n"
            "import helper;\n"
            "println(app.value);\n"
            "println(helper.value);\n"
        ),
        "cwd": ROOT / "scripts" / "import_modules",
        "exit_code": 0,
        "stdout": (
            "--- Import Alias Regression ---\n"
            "loading app\n"
            "loading helper\n"
            "42\n"
            "41\n"
        ),
    },
    {
        "name": "long module name",
        "source": long_module_name_case_source(),
        "exit_code": 0,
        "stdout": "7\n",
    },
    {
        "name": "long module name collision",
        "source": long_module_name_collision_case_source(),
        "exit_code": 0,
        "stdout": (
            "1\n"
            "2\n"
        ),
    },
    {
        "name": "package imports",
        "script": ROOT / "scripts" / "test_module_packages.fex",
        "args": [
            "--module-path", ROOT / "scripts" / "import_packages",
            "--module-path", ROOT / "scripts" / "import_packages" / "feature" / ".." / "feature",
        ],
        "exit_code": 0,
        "stdout": (
            "--- Package Import Regression ---\n"
            "loading feature package\n"
            "loading feature helper\n"
            "42\n"
            "41\n"
        ),
    },
    {
        "name": "implicit file modules",
        "script": ROOT / "scripts" / "test_implicit_file_modules.fex",
        "args": ["--module-path", ROOT / "scripts" / "import_file_modules"],
        "exit_code": 0,
        "stdout": (
            "--- Implicit File Module Regression ---\n"
            "loading implicit app\n"
            "loading implicit helper\n"
            "loading implicit feature package\n"
            "loading implicit feature helper\n"
            "42\n"
            "41\n"
            "43\n"
            "41\n"
        ),
    },
    {
        "name": "imported literal diagnostics",
        "source": (
            'println("--- Imported Literal Diagnostic Regression ---");\n'
            "import error_literal;\n"
            "error_literal.boom();\n"
        ),
        "args": [
            "--module-path", ROOT / "scripts" / "import_file_modules",
            "--builtin", "type",
            "--spans",
        ],
        "exit_code": 70,
        "stdout": "--- Imported Literal Diagnostic Regression ---\n",
        "stderr_contains": [
            "runtime error: tonumber: invalid number format",
            '=> (tonumber "\\\\0")',
        ],
    },
    {
        "name": "package ergonomics",
        "script": ROOT / "scripts" / "test_module_package_ergonomics.fex",
        "args": ["--module-path", ROOT / "scripts" / "import_packages"],
        "exit_code": 0,
        "stdout": (
            "--- Package Ergonomics Regression ---\n"
            "loading feature package\n"
            "loading feature helper\n"
            "loading feature relative importer\n"
            "41\n"
            "43\n"
            "42\n"
        ),
    },
    {
        "name": "import binding by spec",
        "script": ROOT / "scripts" / "test_module_binding_by_spec.fex",
        "args": ["--module-path", ROOT / "scripts" / "import_packages"],
        "exit_code": 0,
        "stdout": (
            "--- Import Binding Regression ---\n"
            "loading feature package\n"
            "loading feature helper\n"
            "loading feature mismatch\n"
            "77\n"
            "77\n"
        ),
    },
    {
        "name": "maps",
        "script": ROOT / "scripts" / "test_maps.fex",
        "args": ["--builtins"],
        "exit_code": 0,
        "stdout": (
            "--- Map Regression ---\n"
            "cfg.env:prod\n"
            "cfg.port:8080\n"
            "cfg.host:localhost\n"
            "mapget host:localhost\n"
            "maphas missing:false\n"
            "cfg.env updated:stage\n"
            "mapcount(cfg):3\n"
            "typeof(cfg):map\n"
            "ismap(cfg):true\n"
            "typeof(settings):map\n"
            "settings.mode:debug\n"
            "mapget version:1.0\n"
            "settings.missing:nil\n"
        ),
    },
    {
        "name": "json and path helpers",
        "script": ROOT / "scripts" / "test_json_path_helpers.fex",
        "args": ["--builtins"],
        "exit_code": 0,
        "stdout": (
            "--- JSON Path Regression ---\n"
            "typeof(doc):map\n"
            "doc.name:fex\n"
            "doc.enabled:true\n"
            "doc.items.head:1\n"
            "doc.meta.env:prod\n"
            "array json:[1,2,3]\n"
            "pathjoin:config/app.json\n"
            "dirname:config\n"
            "basename:app.json\n"
            "loaded.meta.env:prod\n"
            "loaded.items.tail.head:2\n"
        ),
    },
    {
        "name": "filesystem helpers",
        "source": fs_helpers_case_source(),
        "args": ["--builtin", "io", "--builtin", "system"],
        "env": {"FEX_TEST_ENV": "from-env"},
        "use_temp_dir_as_cwd": True,
        "exit_code": 0,
        "stdout": (
            "--- FS Helpers Regression ---\n"
            "mkdir.workspace:true\n"
            "chdir.workspace:true\n"
            "mkdir.plain:true\n"
            "mkdirp.nested:true\n"
            "exists.missing:false\n"
            "exists.plain:true\n"
            "exists.inner:true\n"
            "exists.note:true\n"
            'listdir.root:("plain" "sandbox")\n'
            'listdir.sandbox:("inner" "note.txt")\n'
            "env:from-env\n"
            "chdir.inner:true\n"
            "cwd.base:inner\n"
        ),
    },
    {
        "name": "bytes",
        "script": ROOT / "scripts" / "test_bytes.fex",
        "args": ["--builtins"],
        "exit_code": 0,
        "stdout": (
            "--- Bytes Regression ---\n"
            "typeof(data):bytes\n"
            "isbytes(data):true\n"
            "byteslen(data):3\n"
            "byteat0:65\n"
            "byteat2:67\n"
            "makebytes:#bytes[ff ff ff]\n"
            "slice:#bytes[42 43]\n"
            "loaded eq:true\n"
            "loaded:#bytes[41 42 43]\n"
        ),
    },
    {
        "name": "embedded nul c-string rejection",
        "source": 'writefile("bad\\0path.txt", "x");\n',
        "args": ["--builtin", "io", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: writefile: strings containing NUL bytes are not allowed",
            '=> (writefile "bad\\0path.txt" "x")',
        ],
    },
    {
        "name": "embedded nul json serialization",
        "source": 'println(tojson(parsejson("\\"a\\\\u0000b\\"")));\n',
        "args": ["--builtin", "data"],
        "exit_code": 0,
        "stdout": '"a\\u0000b"\n',
    },
    {
        "name": "embedded nul tonumber rejection",
        "source": 'tonumber("42\\0junk");\n',
        "args": ["--builtin", "type", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: tonumber: invalid number format",
            '=> (tonumber "42\\0junk")',
        ],
    },
    {
        "name": "literal backslash diagnostics",
        "source": 'tonumber("\\\\0");\n',
        "args": ["--builtin", "type", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: tonumber: invalid number format",
            '=> (tonumber "\\\\0")',
        ],
    },
    {
        "name": "string escapes and embedded nul builtins",
        "source": (
            'let s = "a\\\"b";\n'
            'let parts = split("a\\0b,c", ",");\n'
            'println(strlen(s));\n'
            'println(contains("a\\0b", "\\0b"));\n'
            'println(strlen(concat("a\\0b", "c")));\n'
            'println(strlen(car(parts)));\n'
            'println(strlen(trim("\\t\\0 \\n")));\n'
        ),
        "args": ["--builtins"],
        "exit_code": 0,
        "stdout": (
            "3\n"
            "true\n"
            "4\n"
            "3\n"
            "1\n"
        ),
    },
    {
        "name": "readline long line",
        "source": 'println(strlen(readline()));\n',
        "args": ["--builtin", "string"],
        "stdin": ("a" * 5000) + "\n",
        "exit_code": 0,
        "stdout": "5000\n",
    },
    {
        "name": "runcommand",
        "source": runcommand_case_source(),
        "args": ["--builtin", "system"],
        "exit_code": 0,
        "stdout": (
            "3\n"
            "false\n"
            "#bytes[6f 75 74 65 72 72]\n"
        ),
    },
    {
        "name": "runcommand timeout budget",
        "source": runcommand_timeout_case_source(),
        "args": ["--builtin", "system", "--timeout-ms", "50", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: execution timeout exceeded",
        ],
    },
    {
        "name": "system timeout budget",
        "source": system_timeout_case_source(),
        "args": ["--builtin", "system", "--timeout-ms", "50", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: execution timeout exceeded",
        ],
    },
    {
        "name": "runprocess",
        "source": runprocess_case_source(),
        "args": ["--builtin", "system,data"],
        "exit_code": 0,
        "stdout": (
            "5\n"
            "false\n"
            "#bytes[41 42 43]\n"
            "#bytes[65 6e 76 40 73 63 72 69 70 74 73]\n"
        ),
    },
    {
        "name": "runprocess modes",
        "source": runprocess_modes_case_source(),
        "args": ["--builtin", "system,data"],
        "exit_code": 0,
        "stdout": (
            "inherit-out\n"
            "0\n"
            "true\n"
            "true\n"
        ),
        "stderr": "",
    },
    {
        "name": "runprocess capture limit",
        "source": runprocess_limit_case_source(),
        "args": ["--builtin", "system,data"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: runprocess stdout: file too large",
        ],
    },
    {
        "name": "runprocess timeout budget",
        "source": runprocess_timeout_case_source(),
        "args": ["--builtin", "system,data", "--timeout-ms", "50", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: execution timeout exceeded",
        ],
    },
    {
        "name": "cli -e",
        "skip_input_file": True,
        "args": ["-e", "println(40 + 2);"],
        "exit_code": 0,
        "stdout": "42\n",
    },
    {
        "name": "cli stdin",
        "skip_input_file": True,
        "stdin": "println(40 + 2);\n",
        "exit_code": 0,
        "stdout": "42\n",
    },
    {
        "name": "cli version",
        "skip_input_file": True,
        "args": ["--version"],
        "exit_code": 0,
        "stdout": "FeX 1.0\n",
    },
    {
        "name": "cli stats",
        "skip_input_file": True,
        "args": ["--stats", "-e", "println(40 + 2);"],
        "exit_code": 0,
        "stdout": "42\n",
        "stderr_contains": [
            "runtime stats:",
            "steps_executed:",
            "memory_used:",
            "gc_runs:",
        ],
    },
    {
        "name": "builtin categories",
        "source": "let q = substring(tojson(\"x\"), 0, 1);\nlet raw = concat(\"{\", q, \"name\", q, \":\", q, \"fex\", q, \"}\");\nprintln(parsejson(raw).name);\n",
        "args": ["--builtin", "string,data"],
        "exit_code": 0,
        "stdout": "fex\n",
    },
    {
        "name": "builtin safe blocks io",
        "source": "pathjoin(\"a\", \"b\");\n",
        "args": ["--builtin", "safe", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: tried to call non-callable value",
        ],
    },
    {
        "name": "builtin safe plus io",
        "source": "println(pathjoin(\"a\", \"b\"));\n",
        "args": ["--builtin", "safe", "--builtin", "io"],
        "exit_code": 0,
        "stdout": "a/b\n",
    },
    {
        "name": "max step budget",
        "source": BUDGET_LOOP_SOURCE,
        "args": ["--max-steps", "64", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: execution step limit exceeded",
        ],
    },
    {
        "name": "timeout budget",
        "source": BUDGET_LOOP_SOURCE,
        "args": ["--timeout-ms", "50", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: execution timeout exceeded",
        ],
    },
    {
        "name": "memory budget",
        "source": 'module("mem_limit_cli") {\n  export let x = 1;\n}\n',
        "args": ["--memory-pool-size", "1", "--max-memory", "1048576"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: memory limit exceeded",
        ],
    },
    {
        "name": "bad module syntax",
        "source": 'module(123) {\n    export let y = 20;\n}\n',
        "exit_code": 65,
        "stderr_contains": [
            "compile error: Expect module name string.",
            "at ",
        ],
    },
    {
        "name": "export outside module",
        "source": "export let x = 10;\n",
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: export outside of module",
            "[0] => (export (let x 10))",
        ],
    },
    {
        "name": "invalid pair property",
        "source": "let p = 1 :: 2 :: nil;\np.foo;\n",
        "args": ["--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: Only .head, .first, .tail, and .rest are valid on pairs",
            "=> (get p foo)",
        ],
    },
    {
        "name": "cyclic import",
        "source": "import cycle_a;\n",
        "args": ["--module-path", ROOT / "scripts" / "import_cycles", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "cyclic import detected for module 'cycle_a'",
        ],
    },
    {
        "name": "missing module diagnostics",
        "source": "import missing_pkg;\n",
        "args": ["--module-path", ROOT / "scripts" / "import_packages", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "could not resolve module 'missing_pkg'",
            "searched:",
            "missing_pkg.fex",
            "missing_pkg/index.fex",
        ],
    },
    {
        "name": "missing input file",
        "script": ROOT / "scripts" / "missing-does-not-exist.fex",
        "exit_code": 74,
        "stderr_contains": [
            "I/O error: could not open input file",
        ],
    },
    {
        "name": "import path traversal rejected",
        "source": 'import "../../etc/passwd";\n',
        "args": ["--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: import: '..' path components are not allowed in import specifiers",
        ],
    },
    {
        "name": "eval depth limit",
        "source": "fn f(n) { return 1 + f(n + 1); }\nf(0);\n",
        "args": ["--max-eval-depth", "64", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            "runtime error: eval recursion depth limit exceeded",
        ],
    },
    {
        "name": "json output success",
        "source": "println(42);\n",
        "args": ["--json-output"],
        "exit_code": 0,
        "stdout": "42\n",
        "stderr_contains": [
            '"status":"ok"',
            '"exit_code":0',
        ],
    },
    {
        "name": "json output with stats",
        "source": "println(42);\n",
        "args": ["--json-output", "--stats"],
        "exit_code": 0,
        "stdout": "42\n",
        "stderr_contains": [
            '"status":"ok"',
            '"exit_code":0',
            '"stats":{',
            '"steps_executed":',
            '"gc_runs":',
        ],
    },
    {
        "name": "json output runtime error",
        "source": "let x = 1/0;\n",
        "args": ["--json-output", "--spans"],
        "exit_code": 70,
        "stderr_contains": [
            '"status":"runtime_error"',
            '"exit_code":70',
            '"message":"division by zero"',
            '"trace":[',
        ],
    },
    {
        "name": "json output compile error",
        "source": "fn {\n",
        "args": ["--json-output", "--spans"],
        "exit_code": 65,
        "stderr_contains": [
            '"status":"compile_error"',
            '"exit_code":65',
            '"message":',
        ],
    },
    {
        "name": "tail-call optimization",
        "source": (
            "fn count(n) {\n"
            "  if (n <= 0) { 0; } else { count(n - 1); }\n"
            "}\n"
            "println(count(10000));\n"
        ),
        "args": ["--max-eval-depth", "0"],
        "exit_code": 0,
        "stdout": "0\n",
    },
    {
        "name": "tail-call optimization with return",
        "source": (
            "fn sum(n, acc) {\n"
            "  if (n <= 0) { return acc; }\n"
            "  return sum(n - 1, acc + n);\n"
            "}\n"
            "println(sum(10000, 0));\n"
        ),
        "args": ["--max-eval-depth", "0"],
        "exit_code": 0,
        "stdout": "50005000\n",
    },
    {
        "name": "mutual tail-call optimization",
        "source": (
            "fn even(n) {\n"
            "  if (n <= 0) { 1; } else { odd(n - 1); }\n"
            "}\n"
            "fn odd(n) {\n"
            "  if (n <= 0) { 0; } else { even(n - 1); }\n"
            "}\n"
            "println(even(10000));\n"
        ),
        "args": ["--max-eval-depth", "0"],
        "exit_code": 0,
        "stdout": "1\n",
    },
    {
        "name": "mutual tail-call optimization with return",
        "source": (
            "fn parity(n) {\n"
            "  fn even(x) {\n"
            "    if (x <= 0) { return 1; }\n"
            "    return odd(x - 1);\n"
            "  }\n"
            "  fn odd(x) {\n"
            "    if (x <= 0) { return 0; }\n"
            "    return even(x - 1);\n"
            "  }\n"
            "  return even(n);\n"
            "}\n"
            "println(parity(10000));\n"
        ),
        "args": ["--max-eval-depth", "0"],
        "exit_code": 0,
        "stdout": "1\n",
    },
    {
        "name": "while loop with return propagation",
        "source": (
            "fn find_first_over(lst, threshold) {\n"
            "  while (lst) {\n"
            "    let val = car(lst);\n"
            "    if (val > threshold) { return val; }\n"
            "    lst = cdr(lst);\n"
            "  }\n"
            "  return nil;\n"
            "}\n"
            "println(find_first_over([1, 5, 12, 3], 10));\n"
            "println(find_first_over([1, 2, 3], 10));\n"
        ),
        "exit_code": 0,
        "stdout": "12\nnil\n",
    },
]


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n")


def diff_text(label: str, expected: str, actual: str) -> str:
    return "\n".join(
        difflib.unified_diff(
            expected.splitlines(),
            actual.splitlines(),
            fromfile=f"expected {label}",
            tofile=f"actual {label}",
            lineterm="",
        )
    )


def resolve_executable(cli_value: str | None) -> Path:
    if cli_value:
        exe = Path(cli_value)
        if not exe.is_absolute():
            exe = (ROOT / exe).resolve()
        if exe.is_file():
            return exe
        raise FileNotFoundError(f"executable not found: {exe}")

    for candidate in (ROOT / "build" / "fex.exe", ROOT / "build" / "fex_gcc.exe"):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("no executable found; pass --exe or build fex first")


def should_run(case: dict[str, object], filters: list[str]) -> bool:
    if not filters:
        return True
    name = str(case["name"]).lower()
    return any(token.lower() in name for token in filters)


def describe_stream(name: str, text: str) -> str:
    if text:
        return f"{name} was:\n{text}"
    return f"{name} was empty"


def run_case(exe: Path, case: dict[str, object]) -> list[str]:
    command = [str(exe)]
    temp_path: Path | None = None
    temp_dir_obj = None
    stdin_text = case.get("stdin")
    case_cwd = str(ROOT)
    case_env = None

    try:
        if not case.get("skip_input_file", False):
            if "source" in case:
                temp_dir_obj = tempfile.TemporaryDirectory()
                temp_path = Path(temp_dir_obj.name) / "inline_case.fex"
                temp_path.write_text(str(case["source"]), encoding="utf-8", newline="\n")
                command.append(str(temp_path))
            else:
                command.append(str(case["script"]))

        command.extend(str(arg) for arg in case.get("args", []))
        if case.get("use_temp_dir_as_cwd") and temp_dir_obj is not None:
            case_cwd = temp_dir_obj.name
        elif case.get("cwd") is not None:
            case_cwd = str(case["cwd"])

        if case.get("env") is not None:
            case_env = os.environ.copy()
            case_env.update({str(k): str(v) for k, v in dict(case["env"]).items()})

        completed = subprocess.run(
            command,
            input=str(stdin_text) if stdin_text is not None else None,
            capture_output=True,
            cwd=case_cwd,
            env=case_env,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    finally:
        if temp_dir_obj is not None:
            temp_dir_obj.cleanup()

    stdout = normalize(completed.stdout)
    stderr = normalize(completed.stderr)
    errors: list[str] = []

    expected_exit = int(case["exit_code"])
    if completed.returncode != expected_exit:
        errors.append(
            f"exit code mismatch: expected {expected_exit}, got {completed.returncode}"
        )

    expected_stdout = case.get("stdout")
    if expected_stdout is not None and stdout != expected_stdout:
        errors.append("stdout mismatch:\n" + diff_text("stdout", str(expected_stdout), stdout))

    expected_stderr = case.get("stderr")
    if expected_stderr is not None and stderr != expected_stderr:
        errors.append("stderr mismatch:\n" + diff_text("stderr", str(expected_stderr), stderr))

    for needle in case.get("stdout_contains", []):
        if str(needle) not in stdout:
            errors.append(
                f"stdout missing expected text: {needle!r}\n"
                f"{describe_stream('stdout', stdout)}"
            )

    for needle in case.get("stderr_contains", []):
        if str(needle) not in stderr:
            errors.append(
                f"stderr missing expected text: {needle!r}\n"
                f"{describe_stream('stderr', stderr)}"
            )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Run FeX regression scripts.")
    parser.add_argument("--exe", help="Path to the fex executable")
    parser.add_argument(
        "--case",
        action="append",
        default=[],
        help="Only run cases whose names contain this text; can be repeated",
    )
    args = parser.parse_args()

    try:
        exe = resolve_executable(args.exe)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    selected = [case for case in CASES if should_run(case, args.case)]
    if not selected:
        print("No regression cases matched the requested filters.", file=sys.stderr)
        return 2

    failures = 0
    for case in selected:
        errors = run_case(exe, case)
        if errors:
            failures += 1
            print(f"FAIL {case['name']}")
            for error in errors:
                print(error)
        else:
            print(f"PASS {case['name']}")

    if failures:
        print(f"{len(selected) - failures}/{len(selected)} cases passed")
        return 1

    print(f"All {len(selected)} regression cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
