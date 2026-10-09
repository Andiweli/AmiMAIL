#!/usr/bin/env python3
"""Check real m68k HUNK disassembly for guarded CRT entry points.

Run on an unstripped debug executable. This verifies linker wrapping (including
same-object libc calls); it does not execute the program on an Amiga.
"""
import argparse
from collections import defaultdict
import re
import subprocess

PUBLIC = "malloc calloc realloc free fopen fclose".split()
NEWLIB = "_malloc_r _calloc_r _realloc_r _free_r _fopen_r _fclose_r __sfp __sinit".split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--objdump", default="m68k-amigaos-objdump")
    args = parser.parse_args()
    assembly = subprocess.check_output([args.objdump, "-d", args.binary], text=True)
    headers = re.compile(r"^[0-9a-f]+ [0-9a-f]+ (\S+):$")
    references = re.compile(r"\b[0-9a-f]+ [0-9a-f]+ (_\w+)(?:\(pc\))?(?:,.*)?$")
    names, edges = set(), defaultdict(set)
    caller = ""
    for line in assembly.splitlines():
        header = headers.match(line)
        if header:
            caller = header[1]
            names.add(caller)
        reference = references.search(line)
        if reference:
            edges[reference[1]].add(caller)
    assert "_malloc" in names, "Expected an unstripped m68k Amiga executable."
    symbols = PUBLIC + (NEWLIB if "__malloc_r" in names else [])
    for symbol in symbols:
        backend, wrapper = "_" + symbol, "___wrap_" + symbol
        assert backend in names and wrapper in names, "Missing " + symbol
        # HUNK omits local function names. findfp.o's private sinit helper is
        # consequently printed under __fp_unlock_all; its same-object calls
        # cannot be classified by the nearest global label. Check the live
        # guards below and inspect this helper in the libc object separately.
        if symbol in ("__sfp", "__sinit"):
            continue
        permitted = {wrapper}
        # newlib public fopen/fclose call _fopen_r/_fclose_r in the same object.
        # These bypass --wrap but are guarded by the public-entry wrapper.
        if symbol in ("_fopen_r", "_fclose_r"):
            permitted.add(symbol[:-2])
        assert edges[backend], "Backend has no callers: " + backend
        unexpected = edges[backend] - permitted
        assert not unexpected, (backend, "unguarded references", sorted(unexpected))
    # Verify live project/CRT references to the public guards and the internal
    # allocation/stdio-pool guards, rather than just their symbol definitions.
    required = PUBLIC + (["_malloc_r", "_free_r", "__sfp", "__sinit"]
                         if "__malloc_r" in names else [])
    for symbol in required:
        assert edges["___wrap_" + symbol], "Unused guard: " + symbol
    checked = len(symbols) - (2 if "__malloc_r" in names else 0)
    print(f"Native CRT link: {checked} guarded heap/open/close entries; stdio-pool guards linked and used.")


if __name__ == "__main__":
    main()
