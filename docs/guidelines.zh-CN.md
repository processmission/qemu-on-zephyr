# QEMU on Zephyr 使用指南

[English](guidelines.md) · [项目介绍](../README.zh-CN.md) · [英文论文](paper.md)

开发机命令均在仓库根目录执行。标注为 `zephyr>` 的命令在 Zephyr shell 中执行，
标注为 `~ #` 的命令在 Linux guest 中执行。

- [安装环境](#安装环境)
- [选择运行模式](#选择运行模式)
- [System 模式](#system-模式)
- [Linux 用户程序](#linux-用户程序)
- [外部程序与更新](#外部程序与更新)
- [磁盘镜像](#磁盘镜像)
- [退出与再次启动](#退出与再次启动)
- [Make 参数](#make-参数)
- [运行验证](#运行验证)
- [维护工作区](#维护工作区)
- [常见问题](#常见问题)

## 安装环境

开发环境支持 Linux x86_64/AArch64 和 macOS Apple Silicon，需要 Python 3.12
或更新版本。Linux 参考环境为 Ubuntu 24.04。macOS 需要先安装 Xcode Command
Line Tools 和 Homebrew，并将 Homebrew 加入 `PATH`。

```sh
git clone https://github.com/processmission/qemu-on-zephyr.git
cd qemu-on-zephyr
bash scripts/install-host-deps.sh
make setup
make doctor
```

依赖脚本支持 Ubuntu/Debian 的 apt、Arch 的 pacman、Fedora 的 dnf，以及 macOS
的 Homebrew，同时安装用于制作磁盘的 e2fsprogs。`make setup` 配置 `.venv`、
仓库内的 west 工作区、固定版本的 QEMU/Zephyr/dtc/zlib、Zephyr SDK 1.0.1，
并下载和验证 Linux guest 镜像。SDK 提供 AArch64 GNU 编译器和外层 QEMU。

已有 SDK 时，可以显式指定：

```sh
ZEPHYR_SDK_INSTALL_DIR=/absolute/path/to/zephyr-sdk-1.0.1 make setup
```

Make 会自动选择配置好的工具。安装、代理、离线构建和原生 west 操作见
[setup.md](setup.md)。

## 选择运行模式

| 配置 | 作用 |
| --- | --- |
| `QEMU_MODE=system` | 默认模式，通过 `qemu-system-aarch64` 运行 Linux 内核或固件 |
| `QEMU_MODE=user` | 通过 `qemu-aarch64` 运行静态 Linux AArch64 程序 |
| `QEMU_SHELL=0` | 默认自动启动，Zephyr 启动并挂载文件系统后执行配置的命令 |
| `QEMU_SHELL=1` | 停在 Zephyr shell，等待手动输入命令 |

`QEMU_SHELL` 控制启动方式。两种取值的固件都包含 Zephyr shell，guest 结束后
返回该 shell。system 和 user 分别编译固件；运行模式、启动方式、后端和 CPU
选择会反映在构建目录名称中。

外层 QEMU 固定使用 `virt` 板卡和 TCG。system 的内层机器是 `zephyr-virt`，
提供一个 Cortex-A53/A57/A72 vCPU 和 256 MiB 内存。`zephyr` 后端使用 Zephyr
EL2 执行器，`tcg` 后端在 EL1 Zephyr 宿主上进行软件翻译。user 模式使用 EL1
宿主和 TCG。原生后端已经在模拟的 Arm 平台上验证，物理开发板仍未验证。

## System 模式

### 自动启动 Linux

```sh
make run
```

该命令加载 `/images/Image` 和 `/images/initramfs.cpio.gz`。内层默认配置为
`zephyr-virt`、`zephyr` 后端和 Cortex-A53。出现 Linux 的 `~ #` 提示符后，
可以执行 guest 命令：

```text
uname -m
poweroff -f
```

使用 `QEMU_ARGS` 选择内层机器、后端和 CPU：

```sh
make run QEMU_ARGS='-M zephyr-virt -accel zephyr -cpu cortex-a57'
make run QEMU_ARGS='-M zephyr-virt,accel=tcg -cpu cortex-a72'
```

后端在编译时选择，shell 中的 `-accel` 必须与固件一致。原生后端的 CPU
也必须与配置的宿主型号一致；TCG system 固件可以在 shell 中选择 A53、A57
或 A72。

### 手动启动

在开发机执行：

```sh
make run QEMU_SHELL=1 QEMU_ARGS='-accel tcg -cpu cortex-a53'
```

在 `zephyr>` 提示符下执行：

```text
fs ls /images
qemu-system-aarch64 -help
qemu-system-aarch64 -M zephyr-virt -accel tcg -cpu cortex-a72 -kernel /images/Image -initrd /images/initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/bin/sh nokaslr panic=-1"
```

| shell 选项 | 支持的行为 |
| --- | --- |
| `-M`、`-machine` | 选择 `zephyr-virt`，支持 `type=` 和 `accel=` 属性 |
| `-accel`、`-cpu` | 按上述构建约束选择后端和 CPU |
| `-kernel PATH` | 加载 Linux AArch64 Image 或 AArch64 ELF 固件 |
| `-initrd PATH`、`-append STRING` | 指定 initramfs 和内核命令行 |
| `-bios PATH` | 将原始 EL1 固件加载到 `0x40000000` 并从该地址执行 |
| `-m 256M`、`-smp 1`、`-nographic` | 使用固定内存、一个 vCPU 和串口控制台 |
| `-help` | 查看用法；`-M help`、`-accel help`、`-cpu help` 列出支持的选项 |
| `-status` | 查看状态、已完成执行的计数和宿主调度计数 |

路径使用 Zephyr 文件系统的绝对路径，包含空格的参数值需要使用引号。
`-kernel` 和 `-bios` 只能选择一个，`-initrd` 和 `-append` 需要与 `-kernel`
一起使用。Make 的 system `QEMU_ARGS` 接受机器、后端和 CPU 选择；镜像路径
通过 Zephyr shell 命令指定。

### 固件

将自己的固件放入选定的[镜像目录](#磁盘镜像)。其中存在 `firmware.elf` 或
`firmware.bin` 时，使用对应命令：

```text
qemu-system-aarch64 -kernel /images/firmware.elf
qemu-system-aarch64 -bios /images/firmware.bin
```

每次 system 启动都需要处于一次新的 Zephyr 启动中。guest RAM 起点为
`0x40000000`，PL011 位于 `0x09000000`，中断控制器是软件 GICv3。
原始固件需要按加载地址链接；ELF 的各个段和入口地址由 QEMU loader 处理。
加载规则见 [shell.md](shell.md)。

## Linux 用户程序

### 运行默认示例

```sh
make run QEMU_MODE=user QEMU_SHELL=1
```

启动工具使用已经安装的 SDK 编译 [hello 示例](../samples/linux-user/hello/)。
生成的 `build/user-programs/hello` 是使用 Linux AArch64 syscall ABI 的静态
ELF，默认 user 磁盘将其放在 `/images/hello`。

在 `zephyr>` 提示符下执行：

```text
fs ls /images
qemu-aarch64 /images/hello arg1
qemu-aarch64 -E MESSAGE=hello /images/hello "two words"
qemu-aarch64 -strace /images/hello
```

`hello` 会打印参数和可选的 `MESSAGE` 值。命令支持 `-help`、与固件一致的
`-cpu`、`-strace`，以及最多八个 `-E NAME=VALUE` 设置。程序路径之后的参数
交给程序处理。默认工作目录为 `/images`。

### 自动执行

```sh
make run QEMU_MODE=user QEMU_ARGS='-E MESSAGE=hello /images/hello "two words"'
```

user 自动启动需要在 `QEMU_ARGS` 中提供程序路径。文件系统挂载后开始执行，
程序退出后返回 Zephyr shell。

### 兼容范围

当前配置一次执行一个静态链接、单线程的 Linux AArch64 程序，默认进程地址
空间为 64 MiB，`guest_stack_size` 默认为 1 MiB。兼容性取决于程序使用的系统
调用。已支持的接口包括文件、控制台 I/O、私有内存映射、时钟、随机数据和
部分进程信息，尚未支持的 syscall 返回 Linux `ENOSYS`。

当前范围不包含 `fork`、`clone`、`execve`、网络套接字、程序安装的信号处理
函数和动态库运行环境。程序退出时关闭其文件描述符，同一次 Zephyr 启动中
可以运行下一个程序。系统调用接口和内存限制见 [user-mode.md](user-mode.md)。

## 外部程序与更新

### 指定程序目录

将自己的静态 Linux AArch64 ELF 放到开发机上的 `guest-programs/myapp`，
然后在仓库根目录执行：

```sh
make run QEMU_MODE=user \
  GUEST_FILES="$PWD/guest-programs" \
  QEMU_ARGS='/images/myapp arg1'
```

磁盘内容由 `GUEST_FILES` 提供。子目录结构会保留，例如
`guest-programs/bin/myapp` 对应 `/images/bin/myapp`。需要手动选择程序时，
使用 `QEMU_SHELL=1` 启动，先执行 `fs ls /images`，再输入
`qemu-aarch64 /images/myapp arg1`。

自动创建的 Ext2 磁盘使用一个 128 MiB 块组，文件内容总量上限为 112 MiB。
源目录超过上限时，工具会在创建磁盘之前报告错误。

### 让更新后的程序生效

1. 按 **Ctrl-a，再按 x** 退出外层 QEMU。
2. 在开发机重新编译或替换 `guest-programs/myapp`。
3. 再次执行带有相同 `GUEST_FILES` 的 Make 命令。

Make 根据文件名和文件内容计算指纹。文件变化后，工具先创建临时磁盘镜像，
再通过原子重命名替换所选模式的磁盘。新的外层 QEMU 进程挂载该磁盘，
更新后的程序仍通过同一个 Zephyr 路径访问。

已经挂载的 Ext2 文件系统是只读快照。当前接口尚未提供运行期间文件上传和
实时目录共享。`kernel reboot cold` 在现有外层 QEMU 内重启 Zephyr，外层
进程继续使用已经打开的磁盘。因此，新生成的磁盘需要重启外层 QEMU 才能生效。

### 更新显式指定的磁盘

`make run` 会直接使用 `GUEST_DISK` 指定的现有磁盘。源文件变化后，重新生成
该磁盘，再启动新的外层 QEMU：

```sh
make guest-disk QEMU_MODE=user \
  GUEST_FILES="$PWD/guest-programs" GUEST_DISK="$PWD/build/myapp.img"
make run QEMU_MODE=user GUEST_DISK="$PWD/build/myapp.img" \
  QEMU_ARGS='/images/myapp arg1'
```

system 使用的外部 Linux 内核镜像和固件也采用相同的重新生成、退出和启动步骤。

## 磁盘镜像

| 模式 | 默认磁盘 | `/images` 中的默认文件 |
| --- | --- | --- |
| `system` | `build/guest-disk.img` | 已验证的 `Image` 和 `initramfs.cpio.gz` |
| `user` | `build/user-disk.img` | 使用 SDK 编译的 `hello` |

`make guest-disk` 仅准备所选模式的磁盘。准备自己的 system 镜像时，可以执行：

```sh
make guest-disk GUEST_FILES=/absolute/path/to/images GUEST_DISK="$PWD/build/custom.img"
make run QEMU_SHELL=1 GUEST_DISK="$PWD/build/custom.img"
```

开发机目录可以包含普通文件和子目录。工具会拒绝符号链接，输出磁盘需要放在
源目录之外。现有磁盘需要在扇区零开始存放 Ext2 文件系统，使用 4096 字节块、
128 字节 inode 和 `filetype` 特性。工具按输入大小选择容量，最小为 64 MiB。

外层 QEMU 通过 VirtIO Block 提供磁盘。Zephyr 将其只读挂载到 `/images`，
并禁用自动格式化。写入该挂载点会返回 `EROFS`。应用挂载的其他文件系统也
可以提供输入文件。镜像来源和校验值见 [guest-assets.md](guest-assets.md)。

## 退出与再次启动

| 操作 | 效果 |
| --- | --- |
| 执行期间按 Ctrl-] | 停止当前 guest 或程序，返回 Zephyr |
| Linux guest 中执行 `poweroff -f` | 停止该 system guest，返回 Zephyr |
| 在 `zephyr>` 执行 `kernel reboot cold` | 重启 Zephyr，重新初始化 QEMU 全局状态 |
| Ctrl-a，再按 x | 退出外层 QEMU 进程 |
| `qemu-system-aarch64 -status` | 查询已完成执行的计数和宿主调度计数 |

system 在一次 Zephyr 启动中初始化一个 VM。`QEMU guest exited: 0` 表示其
执行循环正常结束，机器和 CPU 对象仍然存在。再次启动 system guest 前，
执行 `kernel reboot cold`，等待提示符重新出现，再输入启动命令。
文件不存在和参数检查错误允许立即重试；QEMU 初始化后的加载错误需要重启。

user 为每次启动重新设置进程映射和 CPU 状态，可以连续执行命令。
Ctrl-] 返回状态 130；其他状态值表示程序退出码，或 128 加上终止信号编号。
执行计数和宿主调度计数仅在主动查询状态时输出。

## Make 参数

| 参数 | 用途 |
| --- | --- |
| `QEMU_MODE=system\|user` | 选择固件的执行模式 |
| `QEMU_SHELL=0\|1` | 选择自动或手动启动 |
| `QEMU_DESKTOP=0\|1` | 启用全系统帧缓冲显示和 Alpine 桌面镜像，操作步骤见 [desktop.md](desktop.md) |
| `QEMU_DISPLAY=none` | 运行桌面配置时关闭本地显示窗口 |
| `QEMU_ARGS='…'` | system 接受 `-M`/`-machine`、`-accel`、`-cpu`；user 接受 `-accel tcg`、`-cpu`、`-E`、`-strace`、程序及参数 |
| `ACCEL=zephyr\|tcg` | `QEMU_ARGS` 省略后端时使用的 system 默认值 |
| `CPU=cortex-a53\|cortex-a57\|cortex-a72` | 省略 `-cpu` 时使用的默认 CPU 型号 |
| `GUEST_FILES=/absolute/path` | 创建磁盘时使用的源目录 |
| `GUEST_DISK=/absolute/path/disk.img` | 启动时使用的现有磁盘，也用于指定 `make guest-disk` 的输出路径 |
| `JOBS=8` | 编译并行度 |
| `ZEPHYR_SDK_INSTALL_DIR=/absolute/path` | 显式指定 SDK |
| `QEMU_SYSTEM_AARCH64=/absolute/path` | 指定外层 QEMU 可执行文件 |

显式传入的 `QEMU_ARGS` 选项覆盖对应默认值，user 模式使用 TCG。
使用 `make help`、`make run QEMU_ARGS='-help'` 或
`make run QEMU_MODE=user QEMU_ARGS='-help'` 查看帮助。
后端细节和原生 CPU 约束见 [backends.md](backends.md)。

## 运行验证

```sh
make check
make check QEMU_SHELL=1 QEMU_ARGS='-accel tcg -cpu cortex-a53'
make check-firmware
make check-firmware QEMU_ARGS='-accel tcg'
make check-user
make test-tools
```

`make check` 检查 Linux 串口 I/O、定时器中断、guest EL0/MMU 执行、宿主
线程调度和 guest 关机。`make check-firmware` 加载 ELF、原始固件，并检查
加载错误、终止和重启。`make check-user` 先执行默认 `hello` 流程，再检查
Linux ABI 操作、内存错误和连续启动。

组件检查命令包括 `make probe`、`make native-probe`、`make test-payload`、
`make test-glib` 和 `make test-arch`。已经检查的配置和证据见
[validation.md](validation.md)。日志和生成的程序位于 `build/`，不纳入 Git。

## 维护工作区

`west/west.yml` 固定上游版本。`make prepare` 导出对应源码，依次应用补丁，
并将 `src/` 合并到 `build/sources/`。修改受版本管理的源码和补丁，保持上游
工作区干净。

`make update` 按清单同步上游源码。`make clean` 删除构建目录，保留 SDK、
环境和已下载的镜像。离线重新构建需要保留 `.venv/`、`.tools/`、`upstream/`
和 `downloads/`。SDK 与 Python 环境变更通过 `make setup` 处理。

源码修改见 [CONTRIBUTING.md](../CONTRIBUTING.md)，补丁维护见
[patches.md](patches.md)。

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| `Cannot open program` | 执行 `fs ls /images`，核对文件名以及所选 `GUEST_FILES` 或 `GUEST_DISK` |
| 默认 `hello` 不存在 | 使用默认输入启动 user 模式，并重启外层 QEMU，挂载 `build/user-disk.img` |
| 文件更新后仍执行旧程序 | 使用命名磁盘时先重新生成，然后退出并重启外层 QEMU |
| `QEMU already initialized` | 启动下一个 system guest 前重启 Zephyr |
| 后端或 CPU 不匹配 | 使用匹配的 `QEMU_ARGS` 重新构建，并遵守原生 CPU 约束 |
| ELF 加载失败 | user 使用静态 Linux AArch64 ELF，system 使用兼容的内核镜像或固件 |
| Linux syscall 返回 `ENOSYS` | 检查 user 模式支持的接口和程序所需的系统调用 |
| 写入返回 `EROFS` | `/images` 为只读挂载，在开发机更新其中的文件 |
| 缺少编译器、QEMU 或 `mke2fs` | 运行 `make doctor`，完成宿主依赖和环境配置 |

当前集成处于实验阶段。物理开发板、任意 Linux 应用兼容性和生产级隔离保证
尚未包含在验证记录中。
