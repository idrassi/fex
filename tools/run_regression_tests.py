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
