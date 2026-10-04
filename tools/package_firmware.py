"""Empaqueta todos los perfiles compilados: binarios, manifiestos, catálogo y hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CHIPS = {"ESP32": "esp32", "ESP32-S3": "esp32s3"}


def package(version, output, root=ROOT):
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?", version):
        raise ValueError("Versión inválida")
    profiles = json.loads((root / "hardware-profiles.json").read_text(encoding="utf-8"))["profiles"]
    ids = [p["id"] for p in profiles]
    if len(ids) != len(set(ids)) or not all(re.fullmatch(r"[a-z0-9_]+", p) for p in ids):
        raise ValueError("IDs de perfil inválidos o duplicados")
    staged = []
    catalog = {"schemaVersion": 1, "version": version, "profiles": []}
    checksums = []
    for profile in profiles:
        profile = dict(profile)
        build = root / ".pio" / "build" / profile["id"]
        artifact = json.loads((build / "artifact.json").read_text(encoding="utf-8"))
        expected = {"environment": profile["id"], "version": version,
                    "chip": CHIPS[profile["chipFamily"]], "flashSize": profile["flashSize"], "offset": 0}
        if artifact != expected:
            raise ValueError(f"{profile['id']}: recompilá; metadatos incompatibles: {artifact}")
        binary = (build / "firmware-merged.bin").read_bytes()
        capacity = int(profile["flashSize"].removesuffix("MB")) * 1024 * 1024
        if not binary or len(binary) > capacity:
            raise ValueError(f"{profile['id']}: imagen vacía o mayor que la flash")
        asset = f"epaper-monitor-ar-v{version}-{profile['id']}-merged.bin"
        manifest_name = f"manifest-{profile['id']}.json"
        sha = hashlib.sha256(binary).hexdigest()
        profile.update({"version": version, "asset": asset, "manifest": manifest_name,
                        "sha256": sha, "bytes": len(binary), "offset": 0})
        manifest = {"name": f"ePaper Monitor AR · {profile['name']}", "version": version,
                    "new_install_prompt_erase": True,
                    "builds": [{"chipFamily": profile["chipFamily"],
                                "parts": [{"path": asset, "offset": 0}]}]}
        staged.extend([(asset, binary), (manifest_name, (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode())])
        catalog["profiles"].append(profile)
        checksums.append(f"{sha}  {asset}")
    # No producir un release parcial si falta algún entorno o hay metadatos viejos.
    output.mkdir(parents=True, exist_ok=True)
    for filename, data in staged:
        (output / filename).write_bytes(data)
    (output / "firmware-catalog.json").write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    return catalog


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    result = package(args.version, args.output)
    print(f"{len(result['profiles'])} perfiles empaquetados en {args.output}")
