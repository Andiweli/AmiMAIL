#!/usr/bin/env python3
"""Exercise production native socket/timer/TLS functions with scripted APIs.

This checks deadline, retry and ownership logic, not AmiSSL ABI or a real stack.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    text = (ROOT / "src/tls.c").read_text()
    sections = [
        ("struct AmgTlsConnection {", "/* libnix errno"),
        ("static void tls_deadline_end(", "int amg_tls_starttls(AmgTlsConnection"),
        ("static long tls_read_internal(", "#else\n\nstruct AmgTlsConnection"),
        ("int amg_tls_write_all(", "int amg_https_decode_chunked("),
    ]
    source = "\n".join(text[text.index(a):text.index(b, text.index(a))]
                       for a, b in sections)
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    with tempfile.TemporaryDirectory(prefix="amimail-tls-") as temp:
        sut = Path(temp) / "native_tls.inc"
        sut.write_text(source)
        executable = Path(temp) / "test"
        subprocess.run(cc + ["-std=c99", "-O1", "-g", "-Wall", "-Wextra",
                            "-Werror", "-Iinclude", "-D__amigaos__",
                            f'-DSUT_FILE="{sut}"'] +
                       shlex.split(os.environ.get("TLS_TEST_FLAGS", "")) +
                       ["tests/test_tls_deadlines.c", "src/common.c", "src/buffer.c",
                        "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print("Production functions, host timer/socket/TLS doubles; no real Amiga runtime.")


if __name__ == "__main__":
    main()
