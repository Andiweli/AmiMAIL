#!/usr/bin/env python3
"""Run GCC's host analyzer on non-UI modules; inspect native paths separately."""
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ("common", "buffer", "codec", "crypto", "imap_parser", "mime", "mailfile",
           "attachment_export", "account", "oauth", "smtp", "storage", "fileio",
           "contacts", "contacts_import", "update", "tls", "transfer", "periodic",
           "herald", "mailto", "charset", "mail_notice", "i18n", "imap")


def main():
    directory = ROOT / "build/analyzer"
    directory.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get("HOST_CC", "gcc"))
    failed = False
    with (ROOT / "build/static-analysis.log").open("w") as log:
        for name in SOURCES:
            result = subprocess.run(compiler + ["-std=c99", "-O0", "-Wall", "-Wextra",
                "-fanalyzer", "-Iinclude", "-c", f"src/{name}.c", "-o",
                str(directory / f"{name}.o")], cwd=ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log.write(f"src/{name}.c\n{result.stdout}")
            if result.returncode:
                failed = True
            if result.stdout:
                print(result.stdout, end="")
    if failed:
        raise SystemExit(1)
    print(f"Static analyzer completed: {len(SOURCES)} host modules.")


if __name__ == "__main__":
    main()
