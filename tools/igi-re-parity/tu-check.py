#!/usr/bin/env python3
"""Compile a single translation unit with the exact cross-build flags.

The shared `build-mingw/` Ninja tree is owned by the build-gate stream. Running
`cmake --build build-mingw` from several teammates at once would race, so this
helper reuses `build-mingw/compile_commands.json` to replay the *exact* compiler
invocation for one file, writing its object file and depfile to a private
temporary directory.

Usage:
    python3 tools/igi-re-parity/tu-check.py source/renderer/mef_native.cpp
    python3 tools/igi-re-parity/tu-check.py --all source/renderer/foo.cpp ...
    python3 tools/igi-re-parity/tu-check.py --list

Exit code 0 means every requested translation unit compiled.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COMPILE_DB = REPO / "build-mingw" / "compile_commands.json"


def load_db() -> list[dict]:
    if not COMPILE_DB.exists():
        sys.exit(
            f"error: {COMPILE_DB} not found.\n"
            "Configure once with:\n"
            "  cmake -S . -B build-mingw -G Ninja -DCMAKE_SYSTEM_NAME=Windows \\\n"
            "    -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \\\n"
            "    -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ \\\n"
            "    -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres \\\n"
            "    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        )
    return json.loads(COMPILE_DB.read_text())


def match(entry: dict, needle: str) -> bool:
    src = Path(entry["file"])
    needle_path = Path(needle)
    if needle_path.is_absolute():
        return src == needle_path
    return str(src).endswith("/" + str(needle_path).lstrip("./"))


def private_flags(args: list[str], workdir: Path) -> list[str]:
    """Redirect -o/-MF into a private directory so concurrent runs never collide."""
    out: list[str] = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-o" and i + 1 < len(args):
            target = workdir / (Path(args[i + 1]).name or "out.o")
            out += ["-o", str(target)]
            i += 2
            continue
        if a == "-MF" and i + 1 < len(args):
            target = workdir / (Path(args[i + 1]).name or "out.d")
            out += ["-MF", str(target)]
            i += 2
            continue
        out.append(a)
        i += 1
    return out


def find_pch(argv: list[str]) -> tuple[Path, Path, str] | None:
    """Return (build_dir, pch_header, target_dir) for a `-include cmake_pch.hxx` command."""
    for i, a in enumerate(argv):
        if a == "-include" and i + 1 < len(argv):
            include = Path(argv[i + 1])
            if include.name == "cmake_pch.hxx":
                target_dir = include.parent.name  # e.g. igi-editor.dir
                return include.parent.parent.parent, include, target_dir
            return None
    return None


def force_rebuild_pch(build_dir: Path, target_dir: str) -> None:
    """Delete and rebuild a PCH.

    A PCH can end up in a state where GCC accepts it but its `#pragma once`
    bookkeeping is wrong: every translation unit that includes one of the
    PCH's headers directly then fails with a bogus "redefinition of ..." error
    that hides the real diagnostics. The only reliable cure is to delete the
    .gch and rebuild it, which is what this does.
    """
    gch = build_dir / "CMakeFiles" / target_dir / "cmake_pch.hxx.gch"
    if gch.exists():
        gch.unlink()
    subprocess.run(
        ["ninja", "-C", str(build_dir), f"CMakeFiles/{target_dir}/cmake_pch.hxx.gch"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True, encoding="utf-8", errors="replace",
    )


_PCH_REFRESHED: set[tuple[str, str]] = set()


def refresh_pch(argv: list[str]) -> None:
    """Rebuild a stale precompiled header before compiling.

    Ninja rebuilds the PCH automatically for `cmake --build`; this helper must do
    the same or it reports false failures. A ninja invocation is cheap when the
    PCH is already current.
    """
    found = find_pch(argv)
    if found is None:
        return
    build_dir, _, target_dir = found
    key = (str(build_dir), target_dir)
    if key in _PCH_REFRESHED:
        return
    _PCH_REFRESHED.add(key)
    ninja = subprocess.run(
        ["ninja", "-C", str(build_dir), f"CMakeFiles/{target_dir}/cmake_pch.hxx.gch"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )
    if ninja.returncode != 0:
        print(ninja.stdout)
        print(f"warning: could not refresh {target_dir}/cmake_pch.hxx.gch")


def run_compile(argv: list[str], cwd: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        argv,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )


def compile_one(entry: dict, quiet: bool) -> bool:
    digest = hashlib.sha1(entry["command"].encode()).hexdigest()[:12]
    with tempfile.TemporaryDirectory(prefix=f"tu-check-{digest}-") as tmp:
        workdir = Path(tmp)
        argv = shlex.split(entry["command"])
        refresh_pch(argv)
        argv = private_flags(argv, workdir)
        rel = os.path.relpath(entry["file"], REPO)
        print(f"==> {rel}")
        proc = run_compile(argv, entry.get("directory", str(REPO)))
        if proc.returncode != 0 and "redefinition of" in proc.stdout:
            # Corrupt PCH symptom, not a real source error: rebuild and retry once.
            found = find_pch(shlex.split(entry["command"]))
            if found is not None:
                build_dir, _, target_dir = found
                print("    note: 'redefinition of' with a PCH in use — rebuilding the PCH and retrying")
                force_rebuild_pch(build_dir, target_dir)
                proc = run_compile(argv, entry.get("directory", str(REPO)))
        if proc.returncode != 0:
            print(proc.stdout)
            print(f"FAILED ({rel}) exit={proc.returncode}")
            return False
        if not quiet:
            for line in proc.stdout.splitlines():
                if "warning:" in line or "error:" in line:
                    print("   ", line)
        print(f"OK    ({rel})")
        return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--list", action="store_true", help="list database entries")
    ap.add_argument("--all", action="store_true", help="check every entry")
    ap.add_argument("--quiet", action="store_true", help="hide warnings")
    ns = ap.parse_args()

    db = load_db()
    if ns.list:
        for e in sorted(db, key=lambda e: e["file"]):
            print(os.path.relpath(e["file"], REPO))
        return 0

    if ns.all:
        entries = db
    else:
        if not ns.files:
            ap.error("give at least one file, or --all")
        entries = []
        for needle in ns.files:
            hits = [e for e in db if match(e, needle)]
            if not hits:
                print(f"error: no compile-command entry matches {needle!r}")
                return 2
            entries.extend(hits)

    failures = 0
    for e in entries:
        if not compile_one(e, ns.quiet):
            failures += 1
    print(f"\nchecked {len(entries)} translation unit(s), {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
