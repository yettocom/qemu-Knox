# qemu-knox

在 QEMU 里启动三星内核镜像的离线实验台，用来观察 KNOX 保护层（RKP / KDP / DEFEX）。

## 用途

三星的内核在启动早期就会和 EL3 里的安全监控握手，之后把 text/rodata 映射成只读，并在运行时
校验关键数据。触发会 `panic_on_oops=1` 重启。

此 QEMU：

- 用原厂 `Image` 直接启动，看到内核在 `paging_init` 阶段向 EL3 发的那次调用；
- 用 DDK 里的 GKI `vmlinux` 起一个能跑到用户态的内核，加载模块，观察 kprobe落点、PAC/BTI 行为、符号解析结果；
- 所有输出 `logs/`，一次失败不需要重启设备。

## 依赖

- Linux，`qemu-system-aarch64` 10.x
- `aarch64-linux-gnu-gcc`（编 initramfs 里的 init）
- Docker（拉取 `ghcr.io/ylarod/ddk-min:<kmi>`，里面有编内核模块用的头和 vmlinux）
- Python 3（`tools/kallsyms.py`、`patch_image.py`）

## 使用

### 1. 准备内核镜像

三个 KMI 的 GKI 内核：

```bash
tools/mkimage.sh android13-5.15 5.15
tools/mkimage.sh android14-6.1  6.1
tools/mkimage.sh android15-6.6  6.6
```

产物在 `image/Image-<tag>`。原厂镜像放到 `image/Image`。

### 2. 编 bench 模块

```bash
tools/buildbench.sh android13-5.15/android14-6.1/android15-6.6
```

### 3. 做 initramfs

```bash
tools/mkinitramfs.sh 5.15/6.1/6.6 "target=__arm64_sys_newuname offset=4 skip=1"
```

第二个参数会写进 `bench.conf`，由 guest 里的 init 读出来传给模块。

### 4. 启动

```bash
KERNEL=image/Image-5.15 INITRD=build/initramfs-5.15.cpio.gz ./run.sh
```

日志自动写到 `logs/boot-<时间>.log`。

## bench 模块参数

| 参数 | 含义 |
| --- | --- |
| `target=` | 要探测的符号名，默认 `__arm64_sys_newuname` |
| `offset=` | kprobe 落在入口后第几个字节，`0` 或 `4` |
| `skip=` | `1` = 处理函数改写到 stub（复现崩溃），`0` = 只观察 |

`offset=0` 在 QEMU 里不会复现 BTI 崩溃（TCG 不强制），`offset=4` 会稳定复现 PAC 崩溃。

## 目录

```
run.sh              QEMU 启动脚本
init/init.c         freestanding 的 /init，裸 syscall
bench/knox_bench.c  探测模块
tools/mkimage.sh    DDK vmlinux -> Image
tools/mkinitramfs.sh
tools/buildbench.sh
tools/checksyms.sh  查某个 KMI 导出了哪些符号
tools/kallsyms.py   离线解析内核符号表
patch_image.py      改原厂镜像里的 smc，让启动再走一段
```

## 使用须知

- 原厂镜像只能走到 `paging_init` 之后一点点，进一步需要真正的 EL3 模拟。
- GKI 台子上装不了依赖三星专有符号的模块。
- `logs/` 和 `image/` 不进仓库。

## 许可

GPL-3.0
