import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from package_firmware import package


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path.cwd())
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "dist"
        self.profiles = [{"id": "mono", "name": "B/N", "chipFamily": "ESP32", "flashSize": "4MB"},
                         {"id": "color", "name": "4 colores", "chipFamily": "ESP32-S3", "flashSize": "8MB"}]
        (self.root / "hardware-profiles.json").write_text(json.dumps({"profiles": self.profiles}))
        for p in self.profiles:
            build = self.root / ".pio" / "build" / p["id"]
            build.mkdir(parents=True)
            (build / "firmware-merged.bin").write_bytes(p["id"].encode())
            self.write_artifact(p)

    def write_artifact(self, p, **overrides):
        artifact = {"environment": p["id"], "version": "1.4.0", "chip": "esp32" if p["id"] == "mono" else "esp32s3",
                    "flashSize": p["flashSize"], "offset": 0, **overrides}
        (self.root / ".pio" / "build" / p["id"] / "artifact.json").write_text(json.dumps(artifact))

    def test_manifest_hash_and_capacity_come_from_each_build(self):
        result = package("1.4.0", self.output, self.root)
        for p in result["profiles"]:
            binary = (self.output / p["asset"]).read_bytes()
            self.assertEqual(p["sha256"], hashlib.sha256(binary).hexdigest())
            self.assertEqual(p["bytes"], len(binary))
            manifest = json.loads((self.output / p["manifest"]).read_text())
            self.assertEqual(manifest["builds"], [{"chipFamily": p["chipFamily"], "parts": [{"path": p["asset"], "offset": 0}]}])

    def test_stale_version_wrong_chip_and_flash_are_rejected_atomically(self):
        for change in [{"version": "1.3.0"}, {"chip": "esp32"}, {"flashSize": "4MB"}]:
            self.write_artifact(self.profiles[1], **change)
            with self.assertRaises(ValueError):
                package("1.4.0", self.output, self.root)
            self.assertFalse(self.output.exists())

    def test_missing_profile_does_not_create_partial_release(self):
        (self.root / ".pio" / "build" / "color" / "firmware-merged.bin").unlink()
        with self.assertRaises(FileNotFoundError):
            package("1.4.0", self.output, self.root)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
