#!/usr/bin/env python3
"""Archive explicitly tracked working-tree files, excluding local secrets."""
import argparse
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import tempfile
import zipfile


def create_archive(root, output, name):
    root = Path(root).resolve()
    output = Path(output).resolve()
    if not name or name in (".", "..") or any(c in name for c in "/\\:"):
        raise ValueError("Archive root must be a single directory name")
    entries = subprocess.run(["git", "ls-files", "--cached", "-z"], cwd=root,
                             stdout=subprocess.PIPE, check=True).stdout.split(b"\0")
    files = []
    for encoded in entries:
        if not encoded:
            continue
        relative = os.fsdecode(encoded)
        parts = PurePosixPath(relative).parts
        if (not parts or relative.startswith("/") or
                any(p in ("..", ".git") for p in parts)):
            raise ValueError(f"Unsafe tracked path: {relative}")
        if (parts[0] in ("build", "bin", "dist") or relative.endswith(".bak") or
                relative == "include/oauth_client_config.local.h"):
            continue
        path = root.joinpath(*parts)
        # Check every component; following a symlink can disclose files that
        # live outside the repository, including through a parent directory.
        cursor = root
        for part in parts:
            cursor = cursor / part
            if cursor.is_symlink():
                raise ValueError(f"Symlink in source archive: {relative}")
        if root not in path.resolve().parents:
            raise ValueError(f"Source entry resolves outside repository: {relative}")
        if not stat.S_ISREG(path.stat().st_mode):
            raise ValueError(f"Source entry is not a regular file: {relative}")
        files.append((relative, path))
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=output.name + ".", dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temp, "w", zipfile.ZIP_DEFLATED) as archive:
            for relative, path in sorted(set(files)):
                archive.write(path, f"{name}/{relative}")
        os.replace(temp, output)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".")
    parser.add_argument("--name", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    count = create_archive(args.root, args.output, args.name)
    print(f"Source archive: {count} tracked files -> {args.output}")


if __name__ == "__main__":
    main()
