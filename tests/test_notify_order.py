#!/usr/bin/env python3
"""Native sound API doubles + production account-order logic, not an SDK test."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = """datatypes/datatypes.h datatypes/datatypesclass.h
 datatypes/soundclass.h devices/timer.h dos/dos.h dos/dosextens.h
 dos/dostags.h exec/io.h exec/libraries.h exec/tasks.h
 proto/datatypes.h proto/dos.h proto/exec.h""".split()


def function(source, name):
    match = re.search(r"static\s+(?:int|void|ULONG)\s+" + re.escape(name) + r"\s*\(", source)
    if not match:
        raise AssertionError(name)
    begin = source.index("{", match.start())
    depth = 1
    pos = begin + 1
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    return source[match.start():pos] + "\n"


def check_wiring(source):
    account = source.split(" int account_dialog(", 1)[1].split(
        "static UWORD about_header_fill_pattern", 1)[0]
    assert "IDCMP_GADGETDOWN" in account
    assert "case WMHI_GADGETDOWN:" not in account
    assert "WINDOW_IDCMPHookBits, IDCMP_GADGETDOWN | IDCMP_INACTIVEWINDOW" in account
    assert "order_hook_data.active_slot = &active_tab;" in account
    assert "account_order_idcmp_subentry" in account
    assert re.search(r"result = WMHI_GADGETUP \| GID_ACCOUNT_SAVE;\s*case WMHI_GADGETUP:", account)
    for key in ("GID_ACCOUNT_MOVE_LEFT", "GID_ACCOUNT_MOVE_RIGHT"):
        assert re.search(r"GA_ID, " + key + r",\s*GA_Immediate, TRUE,\s*GA_RelVerify, TRUE", account)
    assert "if (!account_order_click_take(" in account
    assert "active_tab, direction)" in account
    move_case = account.split("case GID_ACCOUNT_MOVE_LEFT:", 1)[1].split("case GID_ACCOUNT_CANCEL:", 1)[0]
    assert "account_config_move_tabs(" in move_case
    assert "account_config_rebuild_tabs(" not in move_case
    assert "FreeClickTabNode" not in function(source, "account_config_reorder_nodes")
    assert "labels[slot]" in function(source, "account_config_build_tab_nodes")
    gui = (ROOT / "src/gui.c").read_text(encoding="utf-8")
    destroy = gui.split("void amg_gui_destroy(AmgGui *gui)", 1)[1]
    assert destroy.index("gui_notify_cleanup();") < destroy.index("DisposeObject(gui->window_object)")
    notify = (ROOT / "src/gui_notify.c").read_text(encoding="utf-8")
    assert "SystemTags(" not in notify
    assert "if (DoDTMethodA(" not in notify
    assert "GetDTTriggerMethods(sound)" in notify
    assert "SDTA_Sync," not in notify  # Not an actual classic Sound DataType tag.
    end = notify.split("sound_job.finished = 1;", 1)[1].split("}", 1)[0]
    assert "Permit()" not in end and "Wait(" not in end


def main():
    source = (ROOT / "src/gui_dialogs.c").read_text(encoding="utf-8")
    check_wiring(source)
    enum = re.search(r"enum AccountGadgetId\s*\{.*?\};", source, re.S).group()
    struct = re.search(r"typedef struct AccountOrderClick\s*\{.*?\} AccountOrderClick;", source, re.S).group()
    hook_struct = re.search(r"typedef struct AccountOrderHookData\s*\{.*?\} AccountOrderHookData;", source, re.S).group()
    production = enum + "\n" + struct + "\n" + hook_struct + "\n" + "\n".join(function(source, name) for name in (
        "account_order_click_begin", "account_order_click_take", "account_order_button_contains", "account_order_idcmp_subentry",
        "account_order_move_configured_slot", "account_config_reorder_nodes"))
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wshadow", "-Wpointer-arith",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wformat=2", "-Werror"]
    flags += shlex.split(os.environ.get("NOTIFY_TEST_FLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="amimail-notify-order-") as tmp:
        directory = Path(tmp)
        for name in HEADERS:
            header = directory / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text("/* Explicit API double; see notify_native_double.h. */\n")
        (directory / "account_order_production.inc").write_text(production, encoding="utf-8")
        for test, extra in (("test_account_order.c", []), ("test_notify_sound.c", ["-pthread"])):
            binary = directory / test.removesuffix(".c")
            subprocess.run(cc + flags + extra + ["-I" + str(directory), "-Iinclude", "-Itests",
                           "tests/" + test, "-o", str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
    print("Sound/account-order wiring checks passed.")


if __name__ == "__main__":
    main()
