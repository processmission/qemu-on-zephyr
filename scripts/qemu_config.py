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

    def validate_profile(self, profile: str) -> None:
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
    parser = _Parser(
        prog="QEMU_ARGS", allow_abbrev=False,
        description="Configure QEMU inside Zephyr for make build, run, check or native-probe. "
                    "Selections are applied when building the Zephyr image.",
        epilog="QEMU_ARGS overrides ACCEL and CPU for explicitly supplied options. "
               "The current guest has 256 MiB RAM and one vCPU.",
    )
    parser.add_argument("-help", action="help", help="show supported options and exit")
    parser.add_argument("-M", "-machine", dest="machine", action="append",
                        metavar="[type=]MACHINE[,accel=NAME]", help="machine name, or help")
    parser.add_argument("-accel", action="append", metavar="[accel=]NAME",
                        help="zephyr, tcg, or help")
    parser.add_argument("-cpu", action="append", metavar="MODEL",
                        help="cortex-a53, cortex-a57, cortex-a72, or help")
    try:
        arguments = shlex.split(env.get("QEMU_ARGS", ""))
    except ValueError as error:
        raise RuntimeError(f"Invalid QEMU_ARGS: {error}") from error
    options = parser.parse_args(arguments)
    machine_option = _single(options.machine, "-machine")
    accel_option = _single(options.accel, "-accel")
    cpu_option = _single(options.cpu, "-cpu")
    for value, title, choices in (
        (machine_option, "Supported machines", MACHINES),
        (accel_option, "Supported accelerators", ACCELERATORS),
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
    accel = accel_option if accel_option is not None else machine_accel or env.get("ACCEL", "zephyr")
    cpu = cpu_option or env.get("CPU", "cortex-a53")
    if machine not in MACHINES:
        raise RuntimeError(f"Unsupported machine: {machine!r}; use -M zephyr-virt or -M help")
    if accel not in ACCELERATORS:
        raise RuntimeError(f"Unsupported accelerator: {accel!r}; use -accel zephyr|tcg or ACCEL=zephyr|tcg")
    if cpu not in CPUS:
        raise RuntimeError(f"Unsupported CPU: {cpu!r}; use -cpu cortex-a53|cortex-a57|cortex-a72")
    return QemuConfig(machine=machine, accel=accel, cpu=cpu)
