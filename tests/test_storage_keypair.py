#!/usr/bin/env python3
"""Exercise native key/config transaction functions against real host files.

Only randomness, encryption and final rename failures are doubled. This does
not validate AmiSSL encryption or AmigaDOS rename behavior.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    text = (ROOT / "src/storage.c").read_text()
    names = ["hex_encode", "hex_digit", "hex_decode", "storage_header_version",
             "write_hex_line", "replace_file", "discard_file", "read_all", "field",
             "storage_iterations_from_data", "read_account_salt", "read_account_iterations",
             "load_specific_storage_key", "load_cached_storage_key", "write_automatic_storage_key",
             "amg_storage_save_account_auto"]
    functions = []
    for name in names:
        pattern = rf"^(?:static )?[^;{{}}\n]*\b{name}\([^;{{}}]*\)\s*\{{.*?^\}}"
        match = re.search(pattern, text, re.M | re.S)
        if not match:
            raise ValueError(f"Production function not found: {name}")
        functions.append(match.group())
    definitions = "\n".join(line for line in text.splitlines()
                             if line.startswith("#define STORAGE_") or
                             line.startswith("#define SESSION_") or
                             line.startswith("static const char hex_digits"))
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    with tempfile.TemporaryDirectory(prefix="amimail-keypair-") as temp:
        sut = Path(temp) / "native_storage.inc"
        sut.write_text(definitions + "\n" + "\n".join(functions))
        executable = Path(temp) / "test"
        subprocess.run(cc + ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-D__amigaos__", "-Iinclude", f'-DSUT_FILE="{sut}"'] +
                       shlex.split(os.environ.get("STORAGE_TEST_FLAGS", "")) +
                       ["tests/test_storage_keypair.c", "src/common.c", "src/buffer.c",
                        "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=temp, check=True)
    print("Native transaction functions, real host files; crypto/OS behavior doubled.")


if __name__ == "__main__":
    main()
