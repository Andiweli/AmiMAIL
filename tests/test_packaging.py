"""Real zip/staging checks against isolated repositories and built binary."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackagingTests(unittest.TestCase):
    def test_source_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "include").mkdir()
            (root / "tracked space.txt").write_bytes(b"tracked\x00payload")
            (root / "ignored-private.txt").write_text("PRIVATE-MARKER")
            (root / "include/oauth_client_config.local.h").write_text("PRIVATE-MARKER")
            (root / "old.c.bak").write_text("backup")
            subprocess.run(["git", "add", "tracked space.txt", "old.c.bak",
                            "include/oauth_client_config.local.h"], cwd=root, check=True)
            output = root / "dist/source.zip"
            module = load("source_dist")
            self.assertEqual(module.create_archive(root, output, "AmiMAIL"), 1)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ["AmiMAIL/tracked space.txt"])
                self.assertEqual(archive.read(archive.namelist()[0]), b"tracked\x00payload")
            # A missing tracked file aborts before replacing an earlier archive.
            original = output.read_bytes()
            (root / "tracked space.txt").unlink()
            with self.assertRaises(FileNotFoundError):
                module.create_archive(root, output, "AmiMAIL")
            self.assertEqual(output.read_bytes(), original)
            (root / "tracked space.txt").symlink_to(root / "ignored-private.txt")
            with self.assertRaises(ValueError):
                module.create_archive(root, output, "AmiMAIL")
            self.assertEqual(output.read_bytes(), original)

    def test_release_layout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("README.md", "LICENSE", "CHANGELOG.md"):
                shutil.copy2(ROOT / name, root / name)
            for name in ("bin", "assets", "_Catalogs", "_Guides", "_Installer", "docs", "config"):
                shutil.copytree(ROOT / name, root / name)
            module = load("release_stage")
            destination = root / "dist/AmiMAIL-2.2.1"
            module.stage(root, destination)
            setup = destination / "Install/Setup"
            for path in ("AmiMAIL", "AmiMAIL.info", "LICENSE", "Guides/AmiMAIL_EN.guide",
                         "Locale/Catalogs/AmiMAIL.cd", "Locale/Catalogs/deutsch/AmiMAIL.ct",
                         "Locale/Catalogs/deutsch/AmiMAIL.catalog", "Notifications/newmail-Gong.8svx"):
                self.assertTrue((setup / path).is_file(), path)
            self.assertEqual((destination / "AmiMAIL").read_bytes(), (ROOT / "bin/AmiMAIL").read_bytes())
            self.assertEqual((setup / "AmiMAIL.info").read_bytes(), (ROOT / "assets/Icons/AmiMAIL.info").read_bytes())
            # Re-stage removes obsolete entries, and rejects destinations outside dist.
            (destination / "obsolete").write_text("old")
            module.stage(root, destination)
            self.assertFalse((destination / "obsolete").exists())
            with self.assertRaises(ValueError):
                module.stage(root, root / "AmiMAIL-2.2.1")


if __name__ == "__main__":
    unittest.main()
