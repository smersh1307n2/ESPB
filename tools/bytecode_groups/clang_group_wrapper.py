#!/usr/bin/env python3
"""Apply a narrowly-scoped target override to the ESPB atomic probe only."""
from __future__ import annotations

import os
import sys
from pathlib import Path

ATOMIC_SOURCE = "08_atomics.c"
ATOMIC_MARCH = "-march=rv32imac_zicsr_zifencei"


def adjust_clang_args(args: list[str]) -> tuple[list[str], bool]:
    """Override -march only for the one bitcode source needing RV32 A."""
    if not any(Path(arg).name == ATOMIC_SOURCE for arg in args):
        return list(args), False

    adjusted = [arg for arg in args if not arg.startswith("-march=")]
    adjusted.append(ATOMIC_MARCH)
    return adjusted, True


def main() -> int:
    real_clang = os.environ.get("ESPB_REAL_CLANG")
    if not real_clang:
        raise SystemExit("ESPB_REAL_CLANG must point to the real Espressif Clang executable")

    args, overridden = adjust_clang_args(sys.argv[1:])
    if overridden:
        print(
            "ESPB_GROUP_TARGET_OVERRIDE group=08_atomics "
            f"option={ATOMIC_MARCH}",
            file=sys.stderr,
            flush=True,
        )

    os.execv(real_clang, [real_clang, *args])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
