#!/usr/bin/env python3
"""Create and verify provenance manifests for paired ESPB test corpora."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCHEMA = "espb-paired-corpus/v1"
SOURCE_DIR = Path("tests/bytecode_groups")
TRANSLATOR_DIR = Path("projects/ESP32_PRJ_TO_LLVM")


class ManifestError(RuntimeError):
    """Raised when a corpus manifest is incomplete, stale, or inconsistent."""


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _relative(root: Path, path: Path) -> str:
    resolved_root = root.resolve()
    resolved_path = path.resolve()
    try:
        return resolved_path.relative_to(resolved_root).as_posix()
    except ValueError as exc:
        raise ManifestError(f"Path escapes repository root: {path}") from exc


def _require_file(root: Path, relative_path: str) -> Path:
    candidate = (root / relative_path).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as exc:
        raise ManifestError(f"Manifest path escapes repository root: {relative_path}") from exc
    if not candidate.is_file():
        raise ManifestError(f"Missing corpus file: {relative_path}")
    return candidate


def _files_by_stem(directory: Path, pattern: str, suffix: str) -> dict[str, Path]:
    if not directory.is_dir():
        raise ManifestError(f"Missing directory: {directory}")
    result: dict[str, Path] = {}
    for path in sorted(directory.glob(pattern)):
        if not path.is_file() or not path.name.endswith(suffix):
            continue
        stem = path.name[: -len(suffix)]
        if stem in result:
            raise ManifestError(f"Duplicate corpus entry for {stem} in {directory}")
        result[stem] = path
    return result


def create_manifest(
    repo_root: Path,
    bitcode_dir: Path,
    paired_dir: Path,
    repository: str,
    branch: str,
    run_id: int,
) -> dict[str, Any]:
    root = repo_root.resolve()
    bitcode_root = bitcode_dir.resolve()
    paired_root = paired_dir.resolve()
    sources = {
        path.stem: path
        for path in sorted((root / SOURCE_DIR).glob("*.c"))
        if path.is_file()
    }
    bitcode = _files_by_stem(bitcode_root, "*.bc", ".bc")
    jit = _files_by_stem(paired_root, "*.jit.espb", ".jit.espb")
    interpreter = _files_by_stem(paired_root, "*.interpreter.espb", ".interpreter.espb")

    source_names = set(sources)
    if not source_names:
        raise ManifestError(f"No C corpus groups found under {SOURCE_DIR}")
    for label, mapping in (("bitcode", bitcode), ("JIT", jit), ("interpreter", interpreter)):
        if set(mapping) != source_names:
            missing = sorted(source_names - set(mapping))
            extra = sorted(set(mapping) - source_names)
            raise ManifestError(f"{label} corpus mismatch; missing={missing}, extra={extra}")

    source_commit = _git(root, "rev-parse", "HEAD")
    translator_root = root / TRANSLATOR_DIR
    if not translator_root.is_dir():
        raise ManifestError(f"Translator submodule is missing: {TRANSLATOR_DIR}")
    translator_commit = _git(translator_root, "rev-parse", "HEAD")

    groups = []
    for name in sorted(source_names):
        source = sources[name]
        bc = bitcode[name]
        jit_module = jit[name]
        interpreter_module = interpreter[name]
        groups.append(
            {
                "name": name,
                "source_path": _relative(root, source),
                "source_sha256": _sha256(source),
                "bitcode_path": _relative(root, bc),
                "bitcode_sha256": _sha256(bc),
                "jit_path": _relative(root, jit_module),
                "jit_sha256": _sha256(jit_module),
                "interpreter_path": _relative(root, interpreter_module),
                "interpreter_sha256": _sha256(interpreter_module),
            }
        )

    return {
        "schema": SCHEMA,
        "repository": repository,
        "branch": branch,
        "producer_run_id": run_id,
        "source_commit": source_commit,
        "translator_commit": translator_commit,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "group_count": len(groups),
        "groups": groups,
    }


def verify_manifest(
    repo_root: Path,
    bitcode_dir: Path,
    paired_dir: Path,
    manifest_path: Path,
    repository: str,
    branch: str,
    expected_run_id: int | None = None,
    expected_groups: int | None = None,
) -> dict[str, Any]:
    root = repo_root.resolve()
    manifest_file = manifest_path.resolve()
    try:
        manifest_file.relative_to(root)
    except ValueError as exc:
        raise ManifestError("Manifest must be inside the checked-out workspace") from exc
    if not manifest_file.is_file():
        raise ManifestError(f"Missing provenance manifest: {manifest_file}")

    try:
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"Cannot read provenance manifest: {exc}") from exc

    if manifest.get("schema") != SCHEMA:
        raise ManifestError(f"Unsupported manifest schema: {manifest.get('schema')!r}")
    if manifest.get("repository") != repository:
        raise ManifestError(
            f"Repository mismatch: artifact={manifest.get('repository')!r}, current={repository!r}"
        )
    if manifest.get("branch") != branch:
        raise ManifestError(
            f"Branch mismatch: artifact={manifest.get('branch')!r}, current={branch!r}"
        )

    run_id = manifest.get("producer_run_id")
    if not isinstance(run_id, int) or run_id <= 0:
        raise ManifestError("Manifest has an invalid producer_run_id")
    if expected_run_id is not None and run_id != expected_run_id:
        raise ManifestError(
            f"Workflow run mismatch: manifest={run_id}, selected artifact run={expected_run_id}"
        )

    source_commit = manifest.get("source_commit", "")
    translator_commit = manifest.get("translator_commit", "")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ManifestError("Manifest has an invalid source_commit")
    if not re.fullmatch(r"[0-9a-f]{40}", translator_commit):
        raise ManifestError("Manifest has an invalid translator_commit")

    current_commit = _git(root, "rev-parse", "HEAD")
    ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", source_commit, current_commit],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if ancestor.returncode != 0:
        raise ManifestError(
            "Corpus artifact was produced from a commit that is not an ancestor of this checkout"
        )

    current_translator_commit = _git(root / TRANSLATOR_DIR, "rev-parse", "HEAD")
    if current_translator_commit != translator_commit:
        raise ManifestError(
            "Translator submodule mismatch: "
            f"artifact={translator_commit}, current={current_translator_commit}"
        )

    groups = manifest.get("groups")
    if not isinstance(groups, list) or not groups:
        raise ManifestError("Manifest contains no groups")
    if manifest.get("group_count") != len(groups):
        raise ManifestError("Manifest group_count does not match groups array")
    if expected_groups is not None and len(groups) != expected_groups:
        raise ManifestError(
            f"Expected {expected_groups} groups, manifest contains {len(groups)}"
        )

    source_names = {
        path.stem
        for path in sorted((root / SOURCE_DIR).glob("*.c"))
        if path.is_file()
    }
    names = [entry.get("name") for entry in groups if isinstance(entry, dict)]
    if len(names) != len(groups) or len(set(names)) != len(names):
        raise ManifestError("Manifest group names are missing or duplicated")
    if set(names) != source_names:
        raise ManifestError(
            f"Source corpus changed since translation: manifest={sorted(names)}, "
            f"checkout={sorted(source_names)}"
        )

    for entry in groups:
        name = entry["name"]
        expected_paths = {
            "source_path": f"{SOURCE_DIR.as_posix()}/{name}.c",
            "bitcode_path": _relative(root, bitcode_dir / f"{name}.bc"),
            "jit_path": _relative(root, paired_dir / f"{name}.jit.espb"),
            "interpreter_path": _relative(root, paired_dir / f"{name}.interpreter.espb"),
        }
        for path_key, expected_path in expected_paths.items():
            if entry.get(path_key) != expected_path:
                raise ManifestError(
                    f"Unexpected {path_key} for {name}: {entry.get(path_key)!r}"
                )
        for path_key, hash_key in (
            ("source_path", "source_sha256"),
            ("bitcode_path", "bitcode_sha256"),
            ("jit_path", "jit_sha256"),
            ("interpreter_path", "interpreter_sha256"),
        ):
            relative_path = entry[path_key]
            expected_hash = entry.get(hash_key, "")
            if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
                raise ManifestError(f"Invalid {hash_key} for {name}")
            actual_hash = _sha256(_require_file(root, relative_path))
            if actual_hash != expected_hash:
                label = "Source" if path_key == "source_path" else "Artifact"
                raise ManifestError(
                    f"{label} SHA-256 mismatch for {relative_path}: "
                    f"manifest={expected_hash}, actual={actual_hash}"
                )

    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create a provenance manifest")
    create.add_argument("--repo-root", type=Path, required=True)
    create.add_argument("--bitcode-dir", type=Path, required=True)
    create.add_argument("--paired-dir", type=Path, required=True)
    create.add_argument("--repository", required=True)
    create.add_argument("--branch", required=True)
    create.add_argument("--run-id", type=int, required=True)

    verify = subparsers.add_parser("verify", help="verify an existing provenance manifest")
    verify.add_argument("--repo-root", type=Path, required=True)
    verify.add_argument("--bitcode-dir", type=Path, required=True)
    verify.add_argument("--paired-dir", type=Path, required=True)
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--repository", required=True)
    verify.add_argument("--branch", required=True)
    verify.add_argument("--expected-run-id", type=int)
    verify.add_argument("--expected-groups", type=int)

    args = parser.parse_args()
    try:
        if args.command == "create":
            manifest = create_manifest(
                args.repo_root,
                args.bitcode_dir,
                args.paired_dir,
                args.repository,
                args.branch,
                args.run_id,
            )
            output = args.paired_dir / "manifest.json"
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                f"CORPUS_MANIFEST_CREATED run={manifest['producer_run_id']} "
                f"source={manifest['source_commit']} groups={manifest['group_count']}",
                flush=True,
            )
        else:
            manifest = verify_manifest(
                args.repo_root,
                args.bitcode_dir,
                args.paired_dir,
                args.manifest,
                args.repository,
                args.branch,
                args.expected_run_id,
                args.expected_groups,
            )
            print(
                f"CORPUS_MANIFEST_VERIFIED run={manifest['producer_run_id']} "
                f"source={manifest['source_commit']} groups={manifest['group_count']}",
                flush=True,
            )
    except (ManifestError, OSError, subprocess.CalledProcessError) as exc:
        print(f"CORPUS_MANIFEST_ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
