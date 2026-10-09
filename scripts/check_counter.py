# SPDX-License-Identifier: Apache-2.0

import pexpect

from project import ROOT, build, qemu_command
from qemu_config import QemuConfig


def main() -> None:
    config = QemuConfig(cpu="host")
    build("native-probe", config=config)
    command = qemu_command("native-probe", config=config)
    command[command.index("-accel") + 1] = "tcg"
    command[command.index("-cpu") + 1] = "cortex-a53,cntfrq=24000000"
    path = ROOT / "build/counter-rate.log"
    with path.open("w") as log:
        child = pexpect.spawn(command[0], command[1:], encoding="utf-8", timeout=60)
        child.logfile_read = log
        try:
            outcome = child.expect_exact(["QEMU_NATIVE_PROBE_OK", "FATAL ERROR", "counter origins differ"])
            if outcome:
                raise RuntimeError(f"Native counter validation failed; see {path}")
        finally:
            child.close(force=True)
    text = path.read_text()
    if "CNTPCT/CNTVCT same origin (freq=24000000 " not in text:
        raise RuntimeError(f"The native 24 MHz counter was not verified; see {path}")
    print("PASS: native and emulated counters agree at 24 MHz", flush=True)


if __name__ == "__main__":
    main()
