#!/usr/bin/env python3
"""Fill each manifest asset's SHA-256 and byte size from a local asset folder."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> tuple[str, int]:
    sha = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            sha.update(block)
            size += len(block)
    return sha.hexdigest(), size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="Manifest JSON dosyası")
    parser.add_argument("--asset-dir", type=Path, required=True, help="ZIP dosyalarının bulunduğu klasör")
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    asset_dir = args.asset_dir.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assets = []
    for mod in manifest.get("mods", []):
        assets.append((mod, asset_dir / f"{mod['id']}.zip"))
    for patch in manifest.get("patches", []):
        from_version = patch["fromVersion"].replace(".", "-")
        to_version = patch["toVersion"].replace(".", "-")
        assets.append((patch, asset_dir / f"patch-{from_version}-to-{to_version}.zip"))

    missing = [str(path) for _, path in assets if not path.is_file()]
    if missing:
        raise SystemExit("Eksik varlık dosyaları:\n  " + "\n  ".join(missing))
    for record, path in assets:
        record["sha256"], record["size"] = digest(path)
        print(f"{path.name}: {record['size']} byte, sha256 {record['sha256']}")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Güncellendi: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
