#!/usr/bin/env python3
"""Exercise the actual native folder mapping with ASCII conversion/UI doubles.

Checks names, selection paths and visible hierarchy; no ListBrowser runtime.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def fragment(text, first, end):
    start = text.index(first)
    return text[start:text.index(end, start)]


def production():
    source = (ROOT / "src/gui_folders.c").read_text(encoding="utf-8")
    header = (ROOT / "src/gui_internal.h").read_text(encoding="utf-8")
    types = fragment(header, "typedef struct GuiLabel {", "typedef struct GuiReplyStamp {")
    definitions = fragment(source, "typedef struct SystemLabelDefinition {",
                           "static void load_label_expansion_state(")
    mapping = fragment(source, "static void initialize_system_label_map(",
                       " void rebuild_label_lists(")
    tree = fragment(source, "static int label_sort_compare(",
                    "static void load_label_expansion_state(AmgGui *gui)\n{")
    return types, definitions + mapping + tree


def main():
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Wshadow",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wformat=2", "-Werror",
             "-ffunction-sections", "-fdata-sections"]
    flags += shlex.split(os.environ.get("FOLDER_TEST_FLAGS", ""))
    types, code = production()
    with tempfile.TemporaryDirectory(prefix="amimail-folders-") as name:
        directory = Path(name)
        (directory / "folder_types.inc").write_text(types, encoding="utf-8")
        (directory / "folder_production.inc").write_text(code, encoding="utf-8")
        binary = directory / "folder-tests"
        subprocess.run(cc + flags + ["-I" + name, "-Iinclude",
                       "tests/test_folder_visibility.c", "src/account.c", "src/common.c",
                       "-Wl,--gc-sections", "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=20)


if __name__ == "__main__":
    main()
