#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

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
        "name": "bad module syntax",
        "script": ROOT / "scripts" / "test_error_bad_syntax.fex",
        "exit_code": 65,
        "stderr": "[line 9] Error at '123': Expect module name string.\n",
    },
    {
        "name": "export outside module",
        "script": ROOT / "scripts" / "test_error_export_global.fex",
        "exit_code": 1,
        "stderr": "error: export outside of module\n[0] (export (let x 10))\n",
    },
    {
        "name": "invalid pair property",
        "source": "let p = 1 :: 2 :: nil;\np.foo;\n",
        "exit_code": 1,
        "stderr_contains": [
            "Only .head, .first, .tail, and .rest are valid on pairs",
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


def run_case(exe: Path, case: dict[str, object]) -> list[str]:
    command = [str(exe)]
    temp_path: Path | None = None
    temp_dir_obj = None

    try:
        if "source" in case:
            temp_dir_obj = tempfile.TemporaryDirectory()
            temp_path = Path(temp_dir_obj.name) / "inline_case.fex"
            temp_path.write_text(str(case["source"]), encoding="utf-8", newline="\n")
            command.append(str(temp_path))
        else:
            command.append(str(case["script"]))

        completed = subprocess.run(
            command,
            capture_output=True,
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
            errors.append(f"stdout missing expected text: {needle!r}")

    for needle in case.get("stderr_contains", []):
        if str(needle) not in stderr:
            errors.append(f"stderr missing expected text: {needle!r}")

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
