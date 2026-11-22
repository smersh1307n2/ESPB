#!/usr/bin/env python3
"""Build each compact opcode-group C file into its own LLVM bitcode module."""
from __future__ import annotations

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
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--groups",
        nargs="+",
        help="optional group stems to compile; omitted means the complete corpus",
    )
    args = parser.parse_args()

    available = {source.stem: source for source in sorted(GROUPS.glob("*.c"))}
    if not available:
        raise SystemExit(f"No opcode group sources found in {GROUPS}")
    if args.groups:
        unknown = sorted(set(args.groups) - set(available))
        if unknown:
            raise SystemExit(f"Unknown opcode group(s): {', '.join(unknown)}")
        sources = [available[name] for name in dict.fromkeys(args.groups)]
    else:
        sources = list(available.values())
    build_example = SUBMODULE / "examples" / EXAMPLE_NAME
    if build_example.exists():
        raise SystemExit(f"Refusing to overwrite existing directory: {build_example}")

    build_example.mkdir(parents=True)
    wrapper = ROOT / ".work" / "clang-group-wrapper"
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "tools" / "bytecode_groups" / "clang_group_wrapper.py", wrapper)
    wrapper.chmod(0o755)

    real_clang = os.environ.get("ESPB_CLANG") or shutil.which("clang")
    if not real_clang:
        raise SystemExit("ESPB_CLANG is not set and clang was not found on PATH")

    env = os.environ.copy()
    env["ESPB_REAL_CLANG"] = str(Path(real_clang).resolve())
    env["ESPB_CLANG"] = str(wrapper.resolve())

    try:
        for source in sources:
            shutil.copy2(source, build_example / source.name)

        build_script = SUBMODULE / "scripts" / "build_bitcode.py"
        subprocess.run(
            [sys.executable, str(build_script), f"examples/{EXAMPLE_NAME}"],
            cwd=SUBMODULE,
            env=env,
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
        wrapper.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
