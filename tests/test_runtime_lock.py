#!/usr/bin/env python3
"""Stress native CRT wrappers using pthread-backed Exec doubles."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
API = """
#include <pthread.h>
struct SignalSemaphore { pthread_mutex_t mutex; };
void Forbid(void);
void Permit(void);
void InitSemaphore(struct SignalSemaphore *lock);
void ObtainSemaphore(struct SignalSemaphore *lock);
void ReleaseSemaphore(struct SignalSemaphore *lock);
"""


def main():
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Wshadow",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-Werror",
             "-D__amigaos__", "-pthread"]
    flags += shlex.split(os.environ.get("RUNTIME_TEST_FLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="amimail-runtime-") as name:
        directory = Path(name)
        for header, content in {"exec/semaphores.h": API, "proto/exec.h": "",
                                "sys/reent.h": "struct _reent { int unused; };"}.items():
            path = directory / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="ascii")
        for newlib in (False, True):
            binary = directory / ("newlib" if newlib else "libnix")
            variant = ["-D__NEWLIB__=3"] if newlib else []
            subprocess.run(cc + flags + variant + ["-I" + str(directory), "-Iinclude",
                           "tests/test_runtime_lock.c", "-o", str(binary)],
                           cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=60)


if __name__ == "__main__":
    main()
