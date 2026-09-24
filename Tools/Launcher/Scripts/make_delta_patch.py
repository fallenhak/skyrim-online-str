#!/usr/bin/env python3
"""Build a block delta ZIP from two user-owned Skyrim folders."""
import argparse
import hashlib
import json
import os
import zipfile
from pathlib import Path

BLOCK_SIZE = 4096


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def files_under(root: Path) -> dict[str, Path]:
    found: dict[str, Path] = {}
    for current, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = [name for name in dirs if not (Path(current) / name).is_symlink()]
        for name in files:
            path = Path(current) / name
            if path.is_symlink():
                continue
            found[path.relative_to(root).as_posix()] = path
    return found


def make_operations(old: bytes, new: bytes) -> tuple[list[dict], bytes]:
    operations: list[dict] = []
    inserted = bytearray()

    def append(kind: str, offset: int, length: int) -> None:
        if not length:
            return
        if operations and operations[-1]["kind"] == kind:
            prior = operations[-1]
            contiguous = (kind == "copy" and prior["offset"] + prior["length"] == offset) or (
                kind == "insert" and prior["offset"] + prior["length"] == offset
            )
            if contiguous:
                prior["length"] += length
                return
        operations.append({"kind": kind, "offset": offset, "length": length})

    for start in range(0, len(new), BLOCK_SIZE):
        chunk = new[start : start + BLOCK_SIZE]
        old_chunk = old[start : start + len(chunk)]
        if len(old_chunk) == len(chunk) and old_chunk == chunk:
            append("copy", start, len(chunk))
        else:
            payload_offset = len(inserted)
            inserted.extend(chunk)
            append("insert", payload_offset, len(chunk))
    return operations, bytes(inserted)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("from_dir", type=Path, help="Kaynak sürüm: kullanıcının Steam kopyası")
    parser.add_argument("to_dir", type=Path, help="Hedef sürüm: kendi 1.6.1170 dosyalarınız")
    parser.add_argument("output", type=Path, help="Üretilecek .zip delta paketi")
    parser.add_argument("--from-version", required=True)
    parser.add_argument("--to-version", default="1.6.1170")
    args = parser.parse_args()
    source = files_under(args.from_dir.resolve())
    target = files_under(args.to_dir.resolve())
    document = {"format": "sostr-delta-v1", "fromVersion": args.from_version, "toVersion": args.to_version,
                "files": [], "delete": sorted(set(source) - set(target))}
    payloads: list[tuple[str, bytes]] = []
    for index, relative in enumerate(sorted(target)):
        target_bytes = target[relative].read_bytes()
        original = source[relative].read_bytes() if relative in source else b""
        operations, inserted = make_operations(original, target_bytes)
        has_insert = any(op["kind"] == "insert" for op in operations)
        payload_name = f"payload/{index}.bin" if has_insert else ""
        document["files"].append({
            "path": relative,
            "baseSha256": sha256(original) if relative in source else "",
            "sha256": sha256(target_bytes),
            "size": len(target_bytes),
            "payload": payload_name,
            "operations": operations,
        })
        if has_insert:
            payloads.append((payload_name, inserted))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("patch.json", json.dumps(document, ensure_ascii=False, separators=(",", ":")))
        for name, content in payloads:
            archive.writestr(name, content)
    print(f"Delta üretildi: {args.output} ({args.output.stat().st_size} byte, {len(document['files'])} dosya, {len(document['delete'])} silme)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
