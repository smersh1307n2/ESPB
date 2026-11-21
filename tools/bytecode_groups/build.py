#!/usr/bin/env python3
"""Build each compact opcode-group C file into its own LLVM bitcode module."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GROUPS = ROOT / "tests" / "bytecode_groups"
SUBMODULE = ROOT / "projects" / "ESP32_PRJ_TO_LLVM"
EXAMPLE_NAME = "opcode_groups_build"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    sources = sorted(GROUPS.glob("*.c"))
    if not sources:
        raise SystemExit(f"No opcode group sources found in {GROUPS}")
    build_example = SUBMODULE / "examples" / EXAMPLE_NAME
    if build_example.exists():
        raise SystemExit(f"Refusing to overwrite existing directory: {build_example}")

    build_example.mkdir(parents=True)
    try:
        for source in sources:
            shutil.copy2(source, build_example / source.name)

        build_script = SUBMODULE / "scripts" / "build_bitcode.py"
        subprocess.run(
            [sys.executable, str(build_script), f"examples/{EXAMPLE_NAME}"],
            cwd=SUBMODULE,
            env=os.environ.copy(),
            check=True,
        )

        output_dir = args.output_dir.resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        llvm_output = SUBMODULE / "llvm_output"
        for source in sources:
            generated = llvm_output / f"{EXAMPLE_NAME}_{source.stem}.bc"
            if not generated.is_file() or generated.stat().st_size == 0:
                raise RuntimeError(f"Missing or empty bitcode output: {generated}")
            destination = output_dir / f"{source.stem}.bc"
            shutil.copy2(generated, destination)
            print(f"BITCODE_GROUP={destination.name} SIZE={destination.stat().st_size}", flush=True)

        print(f"BUILT_GROUPS={len(sources)}", flush=True)
        return 0
    finally:
        shutil.rmtree(build_example, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
