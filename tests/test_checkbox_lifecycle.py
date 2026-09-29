#!/usr/bin/env python3
"""Native checkbox library wiring, using the actual open/close source.

This is a host test with explicit OpenLibrary/CloseLibrary doubles, not an
Amiga SDK, ABI, Intuition event or checkbox-rendering test.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(source, result_type, name):
    match = re.search(r"static " + result_type + r" " + name +
                      r"\(void\)\n\{.*?\n\}", source, re.S)
    if match is None:
        raise AssertionError("Missing production function: " + name)
    return match.group(0)


def run():
    source = (ROOT / "src/gui.c").read_text(encoding="utf-8")
    dialogs = (ROOT / "src/gui_dialogs.c").read_text(encoding="utf-8")
    opened = function(source, "int", "open_classes")
    closed = function(source, "void", "close_classes")
    assert "CheckBoxBase" in opened.split("return ", 1)[1]
    assert "#include <proto/checkbox.h>" in source
    assert "#include <proto/checkbox.h>" in dialogs
    assert "#include <gadgets/checkbox.h>" in dialogs
    assert "BAG_CHECKBOX" not in dialogs
    assert "BUTTON_PushButton" not in dialogs
    ids = re.findall(r"= native_checkbox\(\s*(GID_\w+)", dialogs)
    assert len(ids) == len(set(ids)) == 9
    # Existing code still reads native selection, and only those creation
    # sites are migrated. No re-selection in any checkbox event handler.
    for field in ("enabled", "imap_starttls", "smtp_starttls",
                  "smtp_same_credentials", "fetch_on_start", "periodic_fetch",
                  "notification_sound", "herald_notifications"):
        assert "GetAttr(GA_Selected, (Object *)page->" + field in dialogs
        assert "SetGadgetAttrs(page->" + field in dialogs
    destroy = source.split("void amg_gui_destroy(AmgGui *gui)", 1)[1]
    assert destroy.index("DisposeObject(gui->window_object)") < destroy.index("close_classes();")
    declarations = "\n".join(re.findall(
        r"^struct (?:Library|GfxBase) \*\w+=NULL;", source, re.M))
    variables = re.findall(r"\*(\w+)=NULL;", declarations)
    assert "CheckBoxBase" in variables
    requests = re.findall(r'OpenLibrary\(\(CONST_STRPTR\)"([^"]+)", (\d+)\)', opened)
    assert ("gadgets/checkbox.gadget", "44") in requests
    count = len(requests)
    expected = ",\n".join('{"' + name + '", ' + version + 'UL}'
                            for name, version in requests)
    resets = "\n".join("        CHECK(" + name + " == NULL);" for name in variables)
    unit = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long ULONG;
typedef unsigned char UBYTE;
typedef const UBYTE *CONST_STRPTR;
struct Library { unsigned opened; };
struct GfxBase { struct Library library; };
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static struct Library libraries[32];
static const struct Expected { const char *name; ULONG version; } expected[] = {
''' + expected + r'''
};
static unsigned fail_at, calls, closes, live;
static struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    unsigned index = calls++;
    CHECK(index < sizeof(expected) / sizeof(expected[0]));
    CHECK(!strcmp((const char *)name, expected[index].name));
    CHECK(version == expected[index].version);
    if (calls == fail_at) return NULL;
    CHECK(!libraries[index].opened);
    libraries[index].opened = 1U;
    ++live;
    return &libraries[index];
}
static void CloseLibrary(struct Library *library)
{
    CHECK(library && library->opened && live);
    library->opened = 0U;
    --live;
    ++closes;
}
''' + declarations + "\n" + opened + "\n" + closed + r'''
int main(void)
{
    unsigned failure;
    for (failure = 0U; failure <= sizeof(expected) / sizeof(expected[0]); ++failure) {
        unsigned should_open, old_closes;
        const char *missing = failure ? expected[failure - 1U].name : "";
        fail_at = failure;
        calls = closes = live = 0U;
        should_open = !failure || !strcmp(missing, "gadgets/fuelgauge.gadget") ||
                      !strcmp(missing, "openurl.library") || !strcmp(missing, "icon.library");
        CHECK(!!open_classes() == (int)should_open);
        CHECK(calls == sizeof(expected) / sizeof(expected[0]));
        if (!strcmp(missing, "gadgets/checkbox.gadget")) CHECK(!CheckBoxBase);
        else CHECK(CheckBoxBase && CheckBoxBase->opened);
        close_classes();
        CHECK(live == 0U);
''' + resets + r'''
        old_closes = closes;
        close_classes();
        CHECK(closes == old_closes && !live);
    }
    printf("Checkbox library: all startup failures, normal shutdown and repeat cleanup passed (%u checks; API doubles).\n", checks);
    return 0;
}
'''
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Wshadow",
             "-Wpointer-arith", "-Wstrict-prototypes", "-Wmissing-prototypes",
             "-Wformat=2", "-Werror"]
    flags += shlex.split(os.environ.get("CHECKBOX_TEST_FLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="amimail-checkbox-lifecycle-") as temp:
        temp = Path(temp)
        unit_path = temp / "checkbox_lifecycle.c"
        binary = temp / "checkbox_lifecycle"
        unit_path.write_text(unit, encoding="utf-8")
        subprocess.run(cc + flags + [str(unit_path), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
    print(f"Native checkbox wiring: nine constructors; {count} class/library opens checked.")


if __name__ == "__main__":
    run()
