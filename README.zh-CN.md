# QEMU on Zephyr

[![Build and boot](https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml/badge.svg)](https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml)

在 Zephyr 内运行 ARM64 Linux 内核、固件和静态 Linux 程序。
system 模式使用 `zephyr` EL2 后端或 QEMU TCG；user 模式使用 TCG，
并将 Linux syscall 转换为 Zephyr 操作。

当前项目是实验性的 AArch64 集成，已在外层 QEMU `virt` 平台上验证。
物理开发板和生产级隔离保证仍未验证。

[English](README.md) · [中文使用指南](docs/guidelines.zh-CN.md) · [English guidelines](docs/guidelines.md) · [英文论文](docs/paper.md)

## 快速开始

开发环境支持 Linux x86_64/AArch64 和 macOS Apple Silicon，需要 Python 3.12+。
macOS 需要先安装 Xcode Command Line Tools 和 Homebrew。

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
make run
```

安装流程准备固定版本的源码、Python 工具、Zephyr SDK 1.0.1 和 guest 镜像。
`make run` 默认自动启动 Linux。使用 `QEMU_SHELL=1` 时，Zephyr 等待手动
输入启动命令。

## Linux user 模式

在开发机执行：

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

默认 user 磁盘包含使用 SDK 编译的静态 Linux ELF。在 `zephyr>` 提示符下执行：

```text
qemu-aarch64 /images/hello arg1
```

需要自动执行时：

```sh
make run QEMU_MODE=user QEMU_ARGS='/images/hello arg1'
```

## 外部程序

通过 `GUEST_FILES` 将自己的程序放入 `/images`。文件通过只读磁盘快照提供，
更新后的文件需要重新生成磁盘并重启外层 QEMU 才能生效。
[外部程序与更新](docs/guidelines.zh-CN.md#外部程序与更新)包含默认磁盘和命名
磁盘的具体操作步骤。

## 运行与退出

- **Ctrl-]** 停止当前 guest 或程序，返回 Zephyr。
- **Ctrl-a，再按 x** 退出外层 QEMU。
- 再次启动 system VM 需要执行 `kernel reboot cold`；user 程序可以在同一次
  Zephyr 启动中连续运行。

使用 `make check` 验证 system 模式，使用 `make check-user` 验证 user 模式。
[使用指南](docs/guidelines.zh-CN.md)包含参数、固件示例、兼容范围和常见问题。

## 开发

源码和补丁维护见 [CONTRIBUTING.md](CONTRIBUTING.md)。
[英文论文](docs/paper.md)讨论 QEMU 的移植过程和 Zephyr POSIX 兼容性评估，
[validation.md](docs/validation.md)记录运行证据。
各组件保留原有许可证，详见 [LICENSE.md](LICENSE.md)。
