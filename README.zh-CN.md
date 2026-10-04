# QEMU on Zephyr

把 QEMU 的 ARM CPU、设备模型和 Linux loader 编进 Zephyr，通过新增的
`zephyr` accelerator 和底层 `zhv` 执行器运行 Linux guest。

```text
外层 QEMU（ARM virt，TCG）
└── Zephyr（EL2）
    ├── 宿主线程、调度器、定时器、串口驱动
    └── QEMU module + zephyr accelerator + zhv 执行器
        └── Linux 内核（EL1）和用户态（EL0）
```

外层 QEMU 提供开发测试平台；内层 QEMU 使用 ARM 虚拟化执行路径，没有内层
TCG 指令翻译器。当前实现支持一个 VM、一个 Cortex-A53 vCPU、256 MiB guest
RAM、PL011、软件 GICv3，以及 Linux 6.4.16 的 initramfs shell。

## 使用

Linux 开发机需要 Git、Make、Python 3、CMake 3.28 或更新版本、Ninja、GNU patch、
tar、C 编译器和 `qemu-system-aarch64`。安装 Zephyr SDK 1.0.1 及其 AArch64 工具链。
已验证的外层 QEMU 版本为 10.2.2。

克隆本仓库后执行：

```sh
cd qemu-on-zephyr
make init
python3 -m venv .venv
. .venv/bin/activate
pip install -r upstream/zephyr/scripts/requirements-base.txt
export ZEPHYR_SDK_INSTALL_DIR=/你的路径/zephyr-sdk-1.0.1
make run
```

无需递归初始化 QEMU 的固件等子模块。`make init` 只初始化四个直接依赖：QEMU、
Zephyr、dtc/libfdt 和 zlib。脚本不会全局安装软件包，也不要求另建 west workspace。

`make run` 自动准备源码、下载并校验 guest 镜像、编译宿主 ELF，然后进入 Linux
串口 shell。输入 `uname -a` 可以查看 guest；按 **Ctrl-a，再按 x** 退出外层 QEMU。
guest 执行 `poweroff -f` 后，Zephyr 宿主仍继续运行。

```sh
make build          # 只编译
make check          # Linux 完整验收，结束后自动停止 QEMU
make probe          # QOM/PL011 设备模型回归
make native-probe   # 原生 accelerator 探针
make test-payload   # 镜像只读文件系统回归
make test-glib      # GLib 差分测试，需要宿主 GLib 开发包和 pkg-config
make test-arch      # EL1/EL2、FPU、执行器测试，先安装 tests/requirements.txt
make clean          # 删除构建与生成源码，保留下载的镜像
```

可用 `JOBS=16` 设置并行编译数，用 `QEMU_SYSTEM_AARCH64` 选择外层 QEMU 程序。
完整验收日志保存在 `build/linux-validation.log`。

## 修改代码

根目录通过 `zephyr/module.yml` 注册为 Zephyr module。所有本项目改动都在主仓库中：

| 目录 | 内容 |
| --- | --- |
| `upstream/` | 固定版本的上游 Git submodule，保持干净 |
| `src/qemu/` | 新增的 QEMU accelerator、适配层、GLib、串口和文件系统代码 |
| `src/zephyr/` | 新增的 EL2 执行器、公共接口与测试 |
| `patches/` | 对上游已有文件的修改 |
| `zephyr/` | 模块 CMake、Kconfig 和元数据 |
| `apps/` | Linux 与 PL011 示例应用 |
| `tests/` | 独立回归测试 |
| `build/sources/` | 自动合成的带补丁源码，不作为编辑入口 |
| `downloads/` | 校验后的测试镜像，不进入 Git |

新增功能优先修改 `src/` 或应用代码；修改上游已有文件时维护 `patches/`。
再次执行 `make build` 会自动检测源码、补丁或版本变化并更新构建树。
修改 SDK 或 Python 环境后建议先 `make clean`。

其他 Zephyr 应用可以把本仓库根目录加入 `ZEPHYR_EXTRA_MODULES`，并使用
`build/sources/zephyr` 作为 `ZEPHYR_BASE`。当前 EL2 能力依赖这里的 Zephyr 补丁，
不能只把 module 加到完全未修改的上游 Zephyr 就运行 guest。

物理板、多 VM、多 vCPU、块设备/网络后端、迁移和 VM 重启管理尚不在已实现或
已验证范围。源码保留各组件原有许可证；仓库不包含可发布的 guest 二进制包。

详细说明见 [架构](docs/architecture.md)、[开发指南](CONTRIBUTING.md)、
[镜像来源](docs/guest-assets.md) 和 [许可证说明](LICENSE.md)。
本仓库的实际重建及测试结果见 [验证记录](docs/validation.md)。
