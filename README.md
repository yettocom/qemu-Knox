# qemu-knox

在 QEMU 里启动三星内核镜像的离线实验台，用来观察 KNOX 保护层（RKP / KDP / DEFEX）在没有真机、
没有 EL3 的环境下会怎样表现。

## 这是做什么的

三星的内核在启动早期就会和 EL3 里的安全监控握手，之后把 text/rodata 映射成只读，并在运行时
校验关键数据。这些机制在真机上很难观察：一旦触发就是 `panic_on_oops=1` 直接重启，串口日志拿
不到，只能靠反复刷机去猜。

这个仓库把这条链路搬到 QEMU：

- 用原厂 `Image` 直接启动，看到内核在 `paging_init` 阶段向 EL3 发的那次调用；
- 用 DDK 里的 GKI `vmlinux` 起一个能跑到用户态的内核，在上面加载自己写的小模块，观察 kprobe
  落点、PAC/BTI 行为、符号解析结果；
- 所有输出都落到 `logs/`，一次失败不需要重启设备。

## 能做什么

| 能 | 说明 |
| --- | --- |
| 观察 RKP 握手 | 原厂镜像会在 `paging_init` 里无条件调用 EL3；QEMU 没有 EL3，能直接看到这次调用的位置和参数 |
| 复现 PAC 崩溃 | 在函数入口 `+4` 处下 kprobe 并改写返回路径，会复现真机上的 `IABT ESR 0x86000004` / `Oops - FPAC` |
| 验证符号解析 | 用 `tools/kallsyms.py` 离线解出内核符号表，或在小模块里用 kprobe 引导解析 |
| 判定死代码 | 在 `LTO_CLANG_FULL=y` 的内核上，判断某个函数是不是被内联后留下的死副本 |
| 观察模块加载 | 自己编的 ko 能不能装、装的时候报什么 |

## 不能做什么

- **没有 EL3/EL2**。RKP 的真正逻辑跑不起来；任何依赖安全监控返回值的路径都只能「假装成功」，
  而假装成功会让内核相信页表已经被接管，随后在 `__create_pgd_mapping` 里 BUG。
- **不是三星内核**。GKI 台子的符号表和原厂镜像不同，三星专有的导出符号一个都没有。
- 真机 vermagic、厂商补丁差异，都得回真机验。

## 依赖

- Linux（WSL 也可以），`qemu-system-aarch64` 10.x
- `aarch64-linux-gnu-gcc`（编 initramfs 里的 init）
- Docker（拉 `ghcr.io/ylarod/ddk-min:<kmi>`，里面有编内核模块用的头和 vmlinux）
- Python 3（`tools/kallsyms.py`、`patch_image.py`）

## 用法

### 1. 准备内核镜像

三个 KMI 的 GKI 内核：

```bash
tools/mkimage.sh android13-5.15 5.15
tools/mkimage.sh android14-6.1  6.1
tools/mkimage.sh android15-6.6  6.6
```

产物在 `image/Image-<tag>`。它们和 DDK 里编模块用的是同一份 vmlinux，所以 vermagic 对得上。

原厂镜像放到 `image/Image`（不随仓库分发）。

### 2. 编 bench 模块

```bash
tools/buildbench.sh android13-5.15
```

### 3. 做 initramfs

```bash
tools/mkinitramfs.sh 5.15 "target=__arm64_sys_newuname offset=4 skip=1"
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

## 已知限制

- 原厂镜像只能走到 `paging_init` 之后一点点，再往前需要真正的 EL3 模拟。
- GKI 台子上装不了依赖三星专有符号的模块（那些符号在 vmlinux 里有，但没导出）。
- `logs/` 和 `image/` 不进仓库。

## 许可

GPL-3.0
