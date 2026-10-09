#!/usr/bin/env python3
"""Stage the runnable program and the complete Installer source layout."""
import argparse
from pathlib import Path
import shutil
import tempfile


def stage(root, destination):
    root = Path(root).resolve()
    if Path(destination).is_symlink():
        raise ValueError("Release destination must not be a symlink")
    destination = Path(destination).resolve()
    # Recursive replacement is restricted to one child of this repo's dist.
    if destination.parent != root / "dist" or not destination.name.startswith("AmiMAIL-"):
        raise ValueError("Release destination must be dist/AmiMAIL-<version>")
    destination.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".stage-", dir=destination.parent) as temp:
        staged = Path(temp) / "release"
        staged.mkdir()
        for name in ("README.md", "LICENSE", "CHANGELOG.md"):
            shutil.copy2(root / name, staged / name)
        shutil.copy2(root / "bin/AmiMAIL", staged / "AmiMAIL")
        shutil.copy2(root / "assets/Icons/AmiMAIL.info", staged / "AmiMAIL.info")
        for source, target in (("docs", "docs"), ("config", "config"),
                               ("_Guides", "Guides"), ("_Catalogs", "Catalogs"),
                               ("assets/Notifications", "Notifications")):
            shutil.copytree(root / source, staged / target)
        installer = staged / "Install"
        setup = installer / "Setup"
        setup.mkdir(parents=True)
        shutil.copy2(root / "_Installer/Install-AmiMAIL", installer / "Install-AmiMAIL")
        for name in ("AmiMAIL", "AmiMAIL.info", "README.md", "LICENSE", "CHANGELOG.md"):
            shutil.copy2(staged / name, setup / name)
        for name in ("docs", "config", "Guides", "Notifications"):
            shutil.copytree(staged / name, setup / name)
        shutil.copytree(staged / "Catalogs", setup / "Locale/Catalogs")
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(staged), destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".")
    parser.add_argument("--destination", required=True)
    args = parser.parse_args()
    stage(args.root, args.destination)
    print(f"Release staged: {args.destination}")


if __name__ == "__main__":
    main()
