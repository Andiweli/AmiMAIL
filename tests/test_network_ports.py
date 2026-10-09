#!/usr/bin/env python3
"""Execute native worker port helpers against per-task signal-pool doubles."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("native_syntax", ROOT / "tests/check_native_syntax.py")
syntax = importlib.util.module_from_spec(spec)
spec.loader.exec_module(syntax)


def main():
    source = (ROOT / "src/network_task.c").read_text()
    parent = source.split("int amg_network_start(", 1)[1].split("static AmgNetMessage *new_message(", 1)[0]
    assert "network->commands = CreateMsgPort()" not in parent
    assert "return network_wait_for_worker(network, error);" in parent
    worker = source.split("static void network_worker(void)", 1)[1].split("AmgNetwork *amg_network_create(", 1)[0]
    assert "if (!network_worker_open_commands(network)) return;" in worker
    assert worker.index("amg_tls_global_cleanup();") < worker.index("network_worker_close_commands(network);")
    assert worker.index("network_worker_close_commands(network);") < worker.index("network_worker_finish(network, stop_message);")
    destroy = source.split("void amg_network_destroy(", 1)[1].split("int amg_network_start(", 1)[0]
    assert "DeleteMsgPort(network->commands)" not in destroy
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Wshadow",
             "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wformat=2",
             "-Werror", "-D__amigaos__", "-ffunction-sections", "-fdata-sections"]
    flags += shlex.split(os.environ.get("NETWORK_PORT_TEST_FLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="amimail-network-ports-") as name:
        directory = Path(name)
        (directory / "native_api.h").write_text(syntax.API, encoding="ascii")
        for header in syntax.HEADERS:
            path = directory / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("/* Explicit API double. */", encoding="ascii")
        binary = directory / "network-ports"
        subprocess.run(cc + flags + ["-include", str(directory / "native_api.h"),
                       "-I" + str(directory), "-Iinclude", "-Isrc",
                       "tests/test_network_ports.c", "src/common.c",
                       "-Wl,--gc-sections", "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)


if __name__ == "__main__":
    main()
