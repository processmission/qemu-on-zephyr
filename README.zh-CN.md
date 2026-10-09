<h1 align="center">QEMU on Zephyr</h1>

<p align="center">
  <strong>在 Zephyr 内运行 ARM64 Linux。</strong><br>
  Linux 桌面 · 固件 · 静态 Linux 程序
</p>

<p align="center">
  <a href="https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml"><img src="https://github.com/processmission/qemu-on-zephyr/actions/workflows/build.yml/badge.svg" alt="构建与启动 CI"></a>
  <a href="docs/backends.md"><img src="https://img.shields.io/badge/architecture-AArch64-315879?style=flat-square" alt="AArch64 架构"></a>
  <a href="docs/setup.md"><img src="https://img.shields.io/badge/Zephyr_SDK-1.0.1-7A51C2?style=flat-square" alt="Zephyr SDK 1.0.1"></a>
  <img src="https://img.shields.io/badge/status-experimental-d29922?style=flat-square" alt="实验性项目">
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

<p align="center">
  <a href="#运行演示">运行演示</a> ·
  <a href="docs/slides/qemu-on-zephyr.pptx">演示文稿</a> ·
  <a href="#快速开始">快速开始</a> ·
  <a href="#运行验证">运行验证</a> ·
  <a href="#文档">文档</a>
</p>

---

全系统模式通过 `zephyr` EL2 后端或 QEMU TCG 运行 Linux 内核和固件。
用户态模式使用 TCG，并将 Linux 系统调用转换为 Zephyr 操作。

> **实验性的 AArch64 集成。** 已在外层 QEMU `virt` 平台上验证。
> 物理开发板和生产级隔离保证仍未验证。

## 运行演示

### Alpine Linux 桌面

在 Zephyr 内运行 Alpine Linux、Xorg 和 JWM，使用 `zephyr` EL2 后端
和 256 MiB 客户机内存。动图展示终端命令、桌面菜单和窗口拖动。

<p align="center">
  <a href="docs/images/system-desktop.gif"><img src="docs/images/system-desktop.gif" width="800" alt="在 Zephyr 内运行 Alpine Linux 桌面，展示启动、终端命令、菜单和窗口拖动"></a><br>
  <sub>800 × 600 显示 · 实际运行录制 · 启动日志 12 倍速 · 桌面操作正常速度</sub>
</p>

<p align="center">
  <a href="#alpine-桌面">启动桌面</a> ·
  <a href="docs/desktop.md#record-the-demonstrations">重新录制动图</a>
</p>

### Linux 用户态程序

执行静态 AArch64 Linux 程序，传入参数和环境变量，程序退出后返回
Zephyr 命令行，继续执行其他程序。

<p align="center">
  <a href="docs/images/linux-user.gif"><img src="docs/images/linux-user.gif" width="800" alt="在 Zephyr 内通过 TCG 执行 AArch64 Linux 程序，展示参数和环境变量"></a><br>
  <sub>AArch64 TCG · 真实串口输出 · 启动日志 12 倍速 · 程序执行正常速度</sub>
</p>

### NanoJev CPU 迷宫

NanoJev 在 ZHV 内的 Debian ARM64 桌面中执行本地 CPU 推理。
完整的 8 × 8 迷宫录制经过 19 次移动尝试、3 次碰撞，最终到达目标。
线性层采用动态 INT8 量化，嵌入层和决策头使用 FP32。

<p align="center">
  <a href="docs/images/nanojev-cpu.gif"><img src="docs/images/nanojev-cpu.gif" width="800" alt="NanoJev 在 ZHV Linux 桌面中通过 CPU 完整执行迷宫并抵达目标"></a><br>
  <sub>外层 HVF · 内层 ZHV · 启动和初始化 12 倍速 · 完整迷宫过程保持原速</sub>
</p>

<p align="center">
  <a href="#nanojev-cpu-桌面">启动 NanoJev</a> ·
  <a href="docs/images/nanojev-cpu.mp4">观看完整视频</a>
</p>

## 快速开始

开发环境支持 Linux x86_64/AArch64 和 macOS Apple Silicon，需要 Python 3.12+。
macOS 需要先安装 Xcode Command Line Tools 和 Homebrew。

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
```

安装流程准备固定版本的源码、Python 工具、Zephyr SDK 1.0.1 和客户机镜像。
完成后，可以启动桌面、Linux 控制台或 Linux 用户态程序：

### Alpine 桌面

启动能够执行 `linux/arm64` 容器的 Docker 服务，然后运行：

```sh
make desktop
```

QEMU 窗口显示桌面，通过 Linux 串口命令行中的 `xdotool` 操作桌面。
操作步骤见[桌面配置与控制](docs/desktop.md)。

### Linux 控制台

```sh
make run
```

Linux 自动启动。使用 `make run QEMU_SHELL=1` 可以进入 Zephyr 命令行，
手动输入启动命令。

### NanoJev CPU 桌面

额外的 Debian ARM64 桌面镜像在 Linux 内执行 NanoJev 迷宫决策，
使用 ZHV EL2 后端和 3 GiB 客户机内存：

```sh
make nanojev
```

此镜像需要支持 Linux ARM64 的 Docker，以及至少 16 GiB 主机内存。
操作步骤见 [NanoJev 镜像与桌面控制](docs/nanojev.md)。

支持嵌套虚拟化的 Apple Silicon 主机可以使用新版 Homebrew QEMU：

```sh
make nanojev CPU=host HOST_ACCEL=hvf QEMU_SYSTEM_AARCH64="$(command -v qemu-system-aarch64)"
```

### 静态 Linux 程序

默认用户态磁盘包含使用 SDK 编译的静态 Linux ELF：

```sh
make run QEMU_MODE=user QEMU_ARGS='/images/hello arg1'
```

<details>
<summary>在 Zephyr 命令行中启动用户态程序</summary>

在开发机执行：

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

在 `zephyr>` 提示符下执行：

```text
qemu-aarch64 /images/hello arg1
```

</details>

## 外部程序

通过 `GUEST_FILES` 将自己的程序放入 `/images`。文件通过只读磁盘快照提供，
更新后的文件需要重新生成磁盘并重启外层 QEMU 才能生效。
[外部程序与更新](docs/guidelines.zh-CN.md#外部程序与更新)包含默认磁盘和命名
磁盘的具体操作步骤。

## 运行与退出

| 操作 | 结果 |
| --- | --- |
| <kbd>Ctrl</kbd> + <kbd>]</kbd> | 停止当前客户机或程序，返回 Zephyr |
| 在 Linux 中执行 `poweroff -f` | 关闭 Linux，返回 Zephyr |
| 在 `zephyr>` 执行 `kernel reboot cold` | 重启 Zephyr，准备启动另一个系统虚拟机 |
| <kbd>Ctrl</kbd> + <kbd>a</kbd>，再按 <kbd>x</kbd> | 退出外层 QEMU |

用户态程序可以在同一次 Zephyr 启动中连续执行。

## 运行验证

| 命令 | 验证内容 |
| --- | --- |
| `make check` | Linux 启动、控制台、定时器中断和主机线程调度 |
| `make check-user` | Linux 程序执行、系统调用、内存和连续启动 |
| `make check-desktop` | Alpine 桌面启动、交互、显示画面和客户机关机 |
| `make check-nanojev` | ZHV Linux 内的 NanoJev CPU 推理和桌面交互 |

桌面验证需要 Docker 和 `requirements-demo.txt` 中的依赖。

## 文档

| 文档 | 内容 |
| --- | --- |
| [中文使用指南](docs/guidelines.zh-CN.md) · [English guidelines](docs/guidelines.md) | 参数、固件、外部程序与常见问题 |
| [环境配置](docs/setup.md) | 依赖、SDK、代理和离线配置 |
| [桌面与动图录制](docs/desktop.md) | Alpine 镜像、桌面控制和 GIF 录制 |
| [演示文稿](docs/slides/qemu-on-zephyr.pptx) | 12 页 PPT，包含 Alpine、Linux 用户态和 NanoJev CPU 演示 |
| [架构说明](docs/architecture.md) · [执行后端](docs/backends.md) | QEMU 集成、EL2 执行和 TCG |
| [Linux 用户态接口](docs/user-mode.md) | 系统调用与进程内存 |
| [英文论文](docs/paper.md) | 移植分析与运行证据 |
| [参与开发](CONTRIBUTING.md) | 源码修改、补丁维护和开发检查 |

各组件保留原有许可证，详见[许可证与源码来源](LICENSE.md)。
