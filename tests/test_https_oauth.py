#!/usr/bin/env python3
"""Scripted HTTP responses and allocation failures in actual OAuth/HTTP code."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    source = (ROOT / "src/tls.c").read_text()
    with tempfile.TemporaryDirectory(prefix="amimail-https-") as temp:
        sut = Path(temp) / "https.inc"
        sut.write_text(source[source.index("int amg_https_decode_chunked("):])
        executable = Path(temp) / "test"
        subprocess.run(shlex.split(os.environ.get("HOST_CC", "gcc")) +
            ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Iinclude",
             f'-DSUT_FILE="{sut}"'] + shlex.split(os.environ.get("HTTPS_TEST_FLAGS", "")) +
            ["tests/test_https_oauth.c", "src/common.c", "src/codec.c", "src/crypto.c",
             "src/i18n.c", "src/charset.c", "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print("Host production HTTP/OAuth code; scripted TLS and allocation faults.")


if __name__ == "__main__":
    main()
