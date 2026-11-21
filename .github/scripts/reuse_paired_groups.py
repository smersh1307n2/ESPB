#!/usr/bin/env python3
"""Reuse paired ESPB modules only when the exact input bitcode hash matches."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

SCHEMA = "espb-paired-corpus/v1"


class ReuseError(RuntimeError):
    """Raised when the previous paired artifact is unsafe or inconsistent."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_file(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as exc:
        raise ReuseError(f"Manifest path escapes previous artifact: {relative}") from exc
    if not path.is_file():
        raise ReuseError(f"Missing file in previous paired artifact: {relative}")
    return path


def reuse_unchanged_pairs(
    previous_root: Path,
    bitcode_dir: Path,
    paired_dir: Path,
    repository: str,
    branch: str,
    translator_commit: str,
) -> tuple[list[str], list[str]]:
    previous_root = previous_root.resolve()
    bitcode_dir = bitcode_dir.resolve()
    paired_dir = paired_dir.resolve()

    manifest_path = previous_root / "paired" / "manifest.json"
    if not manifest_path.is_file():
        raise ReuseError(f"Missing previous provenance manifest: {manifest_path}")
    try:
        manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReuseError(f"Cannot read previous provenance manifest: {exc}") from exc

    if manifest.get("schema") != SCHEMA:
        raise ReuseError(f"Unsupported previous manifest schema: {manifest.get('schema')!r}")
    if manifest.get("repository") != repository:
        raise ReuseError("Previous paired artifact repository mismatch")
    if manifest.get("branch") != branch:
        raise ReuseError("Previous paired artifact branch mismatch")
    if manifest.get("translator_commit") != translator_commit:
        raise ReuseError("Previous paired artifact translator submodule mismatch")

    groups = manifest.get("groups")
    if not isinstance(groups, list) or manifest.get("group_count") != len(groups):
        raise ReuseError("Previous paired manifest has an invalid groups list")
    previous = {}
    for entry in groups:
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
            raise ReuseError("Previous paired manifest has an invalid group entry")
        name = entry["name"]
        if name in previous:
            raise ReuseError(f"Duplicate group in previous manifest: {name}")
        previous[name] = entry

    current_bc = {
        path.stem: path for path in sorted(bitcode_dir.glob("*.bc")) if path.is_file()
    }
    if not current_bc:
        raise ReuseError(f"No current bitcode files found in {bitcode_dir}")

    paired_dir.mkdir(parents=True, exist_ok=True)
    reused: list[str] = []
    changed: list[str] = []
    for name, bitcode_path in sorted(current_bc.items()):
        old = previous.get(name)
        if old is None:
            changed.append(name)
            print(f"PAIRED_REUSE group={name} status=missing_previous_pair", flush=True)
            continue

        current_hash = sha256(bitcode_path)
        if current_hash != old.get("bitcode_sha256"):
            changed.append(name)
            print(f"PAIRED_REUSE group={name} status=bitcode_changed", flush=True)
            continue

        jit_rel = old.get("jit_path")
        interpreter_rel = old.get("interpreter_path")
        jit_hash = old.get("jit_sha256")
        interpreter_hash = old.get("interpreter_sha256")
        if not all(isinstance(v, str) and v for v in (
            jit_rel, interpreter_rel, jit_hash, interpreter_hash
        )):
            raise ReuseError(f"Incomplete paired module metadata for {name}")

        old_jit = safe_file(previous_root, jit_rel)
        old_interpreter = safe_file(previous_root, interpreter_rel)
        if sha256(old_jit) != jit_hash:
            raise ReuseError(f"Previous JIT module SHA-256 mismatch for {name}")
        if sha256(old_interpreter) != interpreter_hash:
            raise ReuseError(f"Previous Interpreter module SHA-256 mismatch for {name}")

        shutil.copy2(old_jit, paired_dir / f"{name}.jit.espb")
        shutil.copy2(old_interpreter, paired_dir / f"{name}.interpreter.espb")
        reused.append(name)
        print(f"PAIRED_REUSE group={name} status=reused", flush=True)

    extra_previous = sorted(set(previous) - set(current_bc))
    if extra_previous:
        raise ReuseError(
            f"Previous manifest has groups absent from current bitcode set: {extra_previous}"
        )

    print(
        f"PAIRED_REUSE_SUMMARY reused={len(reused)} pending_translation={len(changed)}",
        flush=True,
    )
    return reused, changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous-root", type=Path, required=True)
    parser.add_argument("--bitcode-dir", type=Path, required=True)
    parser.add_argument("--paired-dir", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--branch", required=True)
    parser.add_argument("--translator-commit", required=True)
    args = parser.parse_args()
    try:
        reuse_unchanged_pairs(
            args.previous_root,
            args.bitcode_dir,
            args.paired_dir,
            args.repository,
            args.branch,
            args.translator_commit,
        )
    except (ReuseError, OSError) as exc:
        print(f"PAIRED_REUSE_ERROR: {exc}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
