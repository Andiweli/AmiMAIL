#!/usr/bin/env python3
"""Check immutable transfer origins in the real queue using explicit OS doubles.

Compiles network_task.c by inclusion with dead-section removal, so no network
worker/socket is run. This is not a real Amiga scheduler or m68k ABI test.
"""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("native_syntax", ROOT / "tests/check_native_syntax.py")
syntax = importlib.util.module_from_spec(spec)
spec.loader.exec_module(syntax)


def check_gui_integration():
    actions = (ROOT / "src/gui_actions.c").read_text()
    start = actions.index("void handle_network(")
    end = actions.index("if (event.type == AMG_NET_CHECK_INBOX)", start)
    guard = actions[start:end]
    assert "gui_transfer_message_matches(gui, event.uid, event.argument2)" in guard
    assert "gui_transfer_mailbox_matches(gui," in guard
    assert "amg_network_event_clear(&event);" in guard and "continue;" in guard
    assert "AMG_NET_FETCH_INBOX" in guard
    window = (ROOT / "src/gui_window.c").read_text()
    assert re.search(r"gui->preview_gadget =\s*\(struct Gadget \*\)TextEditorObject,\s*"
                     r"GA_ID, GID_PREVIEW,\s*GA_ReadOnly, TRUE,\s*"
                     r"GA_TEXTEDITOR_ReadOnly, TRUE,", window)
    assert "LAYOUT_AddChild, gui_transfer_create_status_row(gui)," in window
    transfer = (ROOT / "src/gui_transfer.c").read_text()
    assert "gui_transfer_create_gauge(gui)" in transfer
    assert "LayoutLimits((struct Gadget *)status" in transfer
    assert transfer.count("{ CHILD_MinHeight, height }, { CHILD_MaxHeight, height }") == 3
    compose = (ROOT / "src/gui_compose.c").read_text()
    assert "draft.progress_mailbox = gui_transfer_mailbox(gui);" in compose
    assert "draft.progress_uid = gui->active_message_uid;" in compose
    runtime = (ROOT / "src/gui_runtime.c").read_text()
    assert "gui_transfer_signal_mask" not in runtime
    assert "gui_transfer_process" not in runtime
    transfer = (ROOT / "src/gui_transfer.c").read_text()
    assert "RA_OpenWindow" not in transfer and "Request(&" not in transfer
    print("GUI wiring: early stale-result guards, read-only preview, no progress window.")


def main():
    check_gui_integration()
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Wshadow",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wformat=2",
             "-Werror", "-D__amigaos__", "-ffunction-sections", "-fdata-sections"]
    flags += shlex.split(os.environ.get("PROGRESS_TEST_FLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="amimail-progress-context-") as name:
        directory = Path(name)
        (directory / "native_api.h").write_text(syntax.API, encoding="ascii")
        for path in syntax.HEADERS:
            header = directory / path
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text("/* OS API double, not an Amiga SDK. */\n", encoding="ascii")
        binary = directory / "progress-context"
        subprocess.run(cc + flags + ["-include", str(directory / "native_api.h"),
                       "-I" + str(directory), "-Iinclude", "-Isrc",
                       "tests/test_progress_context.c", "src/account.c",
                       "src/common.c", "src/buffer.c", "-Wl,--gc-sections",
                       "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
