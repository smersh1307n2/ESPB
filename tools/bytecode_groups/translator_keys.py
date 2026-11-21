#!/usr/bin/env python3
"""Collect translator API keys from aggregate and numbered GitHub secrets."""
from __future__ import annotations

from collections.abc import Mapping


def collect_translator_keys(environ: Mapping[str, str]) -> list[str]:
    """Return unique keys in aggregate-first, numbered-slot order."""
    candidates = [
        value.strip()
        for value in environ.get("ESPB_TRANSLATOR_KEYS", "").splitlines()
        if value.strip()
    ]
    names = ("ESPB_API_KEY",) + tuple(
        f"ESPB_API_KEY_{index}" for index in range(1, 10)
    )
    candidates.extend(
        value.strip() for name in names
        if (value := environ.get(name, "").strip())
    )

    keys: list[str] = []
    seen: set[str] = set()
    for key in candidates:
        if key not in seen:
            seen.add(key)
            keys.append(key)
    return keys
