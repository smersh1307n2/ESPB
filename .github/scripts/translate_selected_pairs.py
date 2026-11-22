#!/usr/bin/env python3
"""Translate only the paired bitcode modules named in a selection file.

The selection file contains one group stem per line (for example
09_freertos_task). Blank lines and lines beginning with # are ignored.
For each selected <stem>.bc this creates matched .jit.espb and
.interpreter.espb files. Unselected groups are never submitted to the API.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def read_selection(path: Path) -> list[str]:
    names: list[str] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        value = raw.split("#", 1)[0].strip()
        if not value:
            continue
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", value) or value.endswith(".bc"):
            raise SystemExit(f"{path}:{line_no}: invalid group stem: {value!r}")
        if value in names:
            raise SystemExit(f"{path}:{line_no}: duplicate group: {value}")
        names.append(value)
    if not names:
        raise SystemExit(f"{path}: no groups selected")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", type=Path, default=Path(".github/paired_translation_groups.txt"))
    parser.add_argument("--bitcode-dir", type=Path, default=Path(".work/bytecode_groups"))
    parser.add_argument("--paired-dir", type=Path, default=Path(".work/paired"))
    parser.add_argument("--translator", type=Path, default=Path(".github/scripts/translate_bc.py"))
    parser.add_argument("--interpreter-converter", type=Path, default=Path("tools/bytecode_groups/make_interpreter_variant.py"))
    args = parser.parse_args()

    names = read_selection(args.selection)
    args.paired_dir.mkdir(parents=True, exist_ok=True)
    for stem in names:
        bitcode = args.bitcode_dir / f"{stem}.bc"
        if not bitcode.is_file() or bitcode.stat().st_size == 0:
            raise SystemExit(f"Selected bitcode is missing or empty: {bitcode}")
        jit = args.paired_dir / f"{stem}.jit.espb"
        interpreter = args.paired_dir / f"{stem}.interpreter.espb"
        print(f"TRANSLATION_REQUIRED group={stem}", flush=True)
        subprocess.run([sys.executable, str(args.translator), str(bitcode), str(jit)], check=True)
        if not jit.is_file() or jit.stat().st_size == 0:
            raise SystemExit(f"Translator did not create a nonempty module: {jit}")
        subprocess.run([sys.executable, str(args.interpreter_converter), str(jit), str(interpreter)], check=True)
        if not interpreter.is_file() or interpreter.stat().st_size == 0:
            raise SystemExit(f"Interpreter converter did not create a nonempty module: {interpreter}")
        print(f"PAIRED_TRANSLATION_DONE group={stem}", flush=True)

    expected_count = len(names)
    missing = [
        f"{stem} ({suffix})"
        for stem in names
        for suffix in ("jit.espb", "interpreter.espb")
        if not (args.paired_dir / f"{stem}.{suffix}").is_file()
        or (args.paired_dir / f"{stem}.{suffix}").stat().st_size == 0
    ]
    if missing:
        raise SystemExit("Selected paired modules missing or empty: " + ", ".join(missing))
    print(
        f"PAIRED_CORPUS_READY jit={len(list(args.paired_dir.glob('*.jit.espb')))} "
        f"interpreter={len(list(args.paired_dir.glob('*.interpreter.espb')))} "
        f"updated={expected_count}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
