#!/usr/bin/env python3
"""Apply narrow per-source options for ESPB opcode bitcode probes."""
from __future__ import annotations

import os
import sys
from pathlib import Path

ATOMIC_SOURCE = "08_atomics.c"
ATOMIC_MARCH = "-march=rv32imac_zicsr_zifencei"
FLOAT_SOURCE = "03_float.c"
FLOAT_OPTIONS = ("-fno-math-errno",)


def adjust_clang_args(args: list[str]) -> tuple[list[str], tuple[str, ...]]:
    """Override options only for the exact probe source that requires them."""
    basenames = {Path(arg).name for arg in args}
    adjusted = list(args)
    applied: list[str] = []

    if ATOMIC_SOURCE in basenames:
        adjusted = [arg for arg in adjusted if not arg.startswith("-march=")]
        adjusted.append(ATOMIC_MARCH)
        applied.append(ATOMIC_MARCH)

    if FLOAT_SOURCE in basenames:
        for option in FLOAT_OPTIONS:
            if option not in adjusted:
                adjusted.append(option)
            applied.append(option)

    return adjusted, tuple(applied)


def main() -> int:
    real_clang = os.environ.get("ESPB_REAL_CLANG")
    if not real_clang:
        raise SystemExit("ESPB_REAL_CLANG must point to the real Espressif Clang executable")

    args, applied = adjust_clang_args(sys.argv[1:])
    if applied:
        group = next(
            name for name in (ATOMIC_SOURCE, FLOAT_SOURCE)
            if any(Path(arg).name == name for arg in sys.argv[1:])
        )
        print(
            f"ESPB_GROUP_TARGET_OPTIONS group={group} options={' '.join(applied)}",
            file=sys.stderr,
            flush=True,
        )

    os.execv(real_clang, [real_clang, *args])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
