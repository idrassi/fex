from __future__ import annotations

import argparse
import sys
from pathlib import Path


def find_first_existing(base: Path, candidates: list[str]) -> Path | None:
    for rel in candidates:
        candidate = base / rel
        if candidate.exists():
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify FeX install/package layout")
    parser.add_argument("--prefix", required=True, help="Install prefix to inspect")
    parser.add_argument("--dist", help="Optional package output directory to inspect")
    args = parser.parse_args()

    prefix = Path(args.prefix)
    if not prefix.exists():
        print(f"missing install prefix: {prefix}", file=sys.stderr)
        return 1

    required_files = [
        "include/fex/fe.h",
        "include/fex/fex.h",
        "include/fex/fex_builtins.h",
        "share/fex/src/fe.c",
        "share/fex/src/fex.c",
        "share/fex/doc/capi.md",
        "share/fex/examples/fib.fex",
        "share/fex/README.md",
        "share/fex/LICENSE",
    ]

    missing = [rel for rel in required_files if not (prefix / rel).exists()]
    if missing:
        print("missing installed files:", file=sys.stderr)
        for rel in missing:
            print(f"  {rel}", file=sys.stderr)
        return 1

    exe = find_first_existing(prefix, ["bin/fex.exe", "bin/fex"])
    if exe is None:
        print("missing installed interpreter binary", file=sys.stderr)
        return 1

    lib = find_first_existing(
        prefix,
        [
            "lib/fex.lib",
            "lib/libfex.a",
            "lib/libfex.so",
            "lib/libfex.dylib",
        ],
    )
    if lib is None:
        print("missing installed embedding library", file=sys.stderr)
        return 1

    if args.dist:
        dist = Path(args.dist)
        if not dist.exists():
            print(f"missing package output directory: {dist}", file=sys.stderr)
            return 1
        packages = list(dist.glob("*.zip")) + list(dist.glob("*.tar.gz")) + list(dist.glob("*.tgz"))
        if not packages:
            print("no release archive produced in dist directory", file=sys.stderr)
            return 1

    print(f"install layout OK: {prefix}")
    if args.dist:
        print(f"package output OK: {args.dist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
