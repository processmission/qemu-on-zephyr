# SPDX-License-Identifier: Apache-2.0

import argparse
from collections.abc import Mapping
from dataclasses import dataclass
import os
import shlex
from typing import NoReturn

MACHINES = {"zephyr-virt": "ARM virt profile with PL011, GICv3 and one vCPU"}
ACCELERATORS = {"zephyr": "Zephyr EL2 virtualization", "tcg": "AArch64 software translation"}
CPUS = ("cortex-a53", "cortex-a57", "cortex-a72")


@dataclass(frozen=True)
class QemuConfig:
    machine: str = "zephyr-virt"
    accel: str = "zephyr"
    cpu: str = "cortex-a53"
    manual_shell: bool = False
    mode: str = "system"
    program: tuple[str, ...] = ()
    user_env: tuple[str, ...] = ()
    strace: bool = False
    desktop: bool = False

    def validate_profile(self, profile: str) -> None:
        if self.desktop and (profile != "linux" or self.mode != "system"):
            raise RuntimeError("QEMU_DESKTOP=1 requires the system Linux profile")
        if self.mode == "user" and profile == "native-probe":
            raise RuntimeError("native-probe requires QEMU_MODE=system")
        if profile == "native-probe" and self.accel != "zephyr":
            raise RuntimeError("native-probe requires ACCEL=zephyr or -accel zephyr")


class _Parser(argparse.ArgumentParser):
    def error(self, message: str) -> NoReturn:
        raise RuntimeError(f"Invalid QEMU_ARGS: {message}; use QEMU_ARGS='-help' for options")


def _single(values: list[str] | None, option: str) -> str | None:
    if values is None:
        return None
    if len(values) != 1:
        raise RuntimeError(f"Specify {option} once")
    if not values[0]:
        raise RuntimeError(f"{option} requires a nonempty value")
    return values[0]


def _machine(value: str) -> tuple[str, str | None]:
    properties: dict[str, str] = {}
    for index, field in enumerate(value.split(",")):
        key, separator, setting = field.partition("=")
        if index == 0 and not separator:
            key, setting = "type", field
        elif not separator or key not in ("type", "accel"):
            raise RuntimeError(f"Unsupported -machine property: {field!r}; use type or accel")
        if key in properties or not setting:
            raise RuntimeError(f"Invalid or repeated -machine property: {field!r}")
        properties[key] = setting
    return properties.get("type", "zephyr-virt"), properties.get("accel")


def load_config(environ: Mapping[str, str] | None = None) -> QemuConfig:
    env = os.environ if environ is None else environ
    shell_mode = env.get("QEMU_SHELL", "0")
    if shell_mode not in ("0", "1"):
        raise RuntimeError("QEMU_SHELL must be 0 (automatic startup) or 1 (manual shell)")
    mode = env.get("QEMU_MODE", "system")
    desktop = env.get("QEMU_DESKTOP", "0")
    if desktop not in ("0", "1"):
        raise RuntimeError("QEMU_DESKTOP must be 0 or 1")
    if desktop == "1" and mode != "system":
        raise RuntimeError("QEMU_DESKTOP=1 requires QEMU_MODE=system")
    if mode not in ("system", "user"):
        raise RuntimeError("QEMU_MODE must be system or user")
    parser = _Parser(
        prog="QEMU_ARGS", allow_abbrev=False,
        description=("Configure qemu-aarch64 Linux process execution inside Zephyr."
                     if mode == "user" else
                     "Configure QEMU inside Zephyr for make build, run, check or native-probe. "
                     "Selections are applied when building the Zephyr image."),
        epilog=("User mode uses TCG with a 64 MiB process address space. "
                "Arguments after the program path are passed to the program."
                if mode == "user" else
                "QEMU_ARGS overrides ACCEL and CPU for explicitly supplied options. "
                "The current guest has 256 MiB RAM and one vCPU."),
    )
    parser.add_argument("-help", action="help", help="show supported options and exit")
    parser.add_argument("-M", "-machine", dest="machine", action="append",
                        metavar="[type=]MACHINE[,accel=NAME]",
                        help=argparse.SUPPRESS if mode == "user" else "machine name, or help")
    parser.add_argument("-accel", action="append", metavar="[accel=]NAME",
                        help="tcg or help" if mode == "user" else "zephyr, tcg, or help")
    parser.add_argument("-cpu", action="append", metavar="MODEL",
                        help="cortex-a53, cortex-a57, cortex-a72, or help")
    if mode == "user":
        parser.add_argument("-E", dest="user_env", action="append", default=[],
                            metavar="NAME=VALUE", help="process environment setting")
        parser.add_argument("-strace", action="store_true", help="print translated Linux system calls")
        parser.add_argument("program", nargs=argparse.REMAINDER,
                            help="absolute Zephyr program path followed by its arguments")
    try:
        arguments = shlex.split(env.get("QEMU_ARGS", ""))
    except ValueError as error:
        raise RuntimeError(f"Invalid QEMU_ARGS: {error}") from error
    options = parser.parse_args(arguments)
    machine_option = _single(options.machine, "-machine")
    accel_option = _single(options.accel, "-accel")
    cpu_option = _single(options.cpu, "-cpu")
    if mode == "user" and machine_option is not None:
        raise RuntimeError("User mode uses TCG and accepts no machine option")
    for value, title, choices in (
        (machine_option, "Supported machines", MACHINES),
        (accel_option, "Supported accelerators",
         {"tcg": ACCELERATORS["tcg"]} if mode == "user" else ACCELERATORS),
        (cpu_option, "Supported CPUs", dict.fromkeys(CPUS, "")),
    ):
        if value == "help":
            print(title + ":")
            for name, description in choices.items():
                print(f"  {name:<14} {description}".rstrip())
            raise SystemExit(0)

    machine, machine_accel = _machine(machine_option or "zephyr-virt")
    if accel_option is not None:
        accel_option = accel_option.removeprefix("accel=")
    if machine_accel is not None and accel_option is not None and machine_accel != accel_option:
        raise RuntimeError("Conflicting accelerators in -machine accel= and -accel")
    accel = accel_option if accel_option is not None else machine_accel or (
        "tcg" if mode == "user" else env.get("ACCEL", "zephyr"))
    cpu = cpu_option or env.get("CPU", "cortex-a53")
    if machine not in MACHINES:
        raise RuntimeError(f"Unsupported machine: {machine!r}; use -M zephyr-virt or -M help")
    if accel not in ACCELERATORS:
        raise RuntimeError(f"Unsupported accelerator: {accel!r}; use -accel zephyr|tcg or ACCEL=zephyr|tcg")
    if cpu not in CPUS:
        raise RuntimeError(f"Unsupported CPU: {cpu!r}; use -cpu cortex-a53|cortex-a57|cortex-a72")
    program = tuple(options.program) if mode == "user" else ()
    user_env = tuple(options.user_env) if mode == "user" else ()
    if mode == "user":
        if machine_option is not None or accel != "tcg":
            raise RuntimeError("User mode uses TCG and accepts no machine option")
        if program and not program[0].startswith("/"):
            raise RuntimeError("User program must have an absolute Zephyr filesystem path")
        if len(user_env) > 8 or any("=" not in value or value.startswith("=") for value in user_env):
            raise RuntimeError("Use up to eight -E NAME=VALUE options")
    return QemuConfig(machine=machine, accel=accel, cpu=cpu, manual_shell=shell_mode == "1",
                      mode=mode, program=program, user_env=user_env,
                      strace=options.strace if mode == "user" else False,
                      desktop=desktop == "1")
