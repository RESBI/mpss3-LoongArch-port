# CHANGELOG — offload 运行时：宿主侧 COI（06-mpss-coi）

　　本文件记本包**功能更新**，主体是宿主侧 `libcoi_host.so`。末尾另有一节「配套项」，记的是本包能跑起来所必需的 k1om 工具链与卡端运行时（它们不是本包的代码，但没有它们本包用不起来）。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-05 — 构建与 ABI 处理

- **新增**　本包在 loongarch64 上构建并安装（库、头文件、`micnativeloadex` 一类宿主侧工具）。
- **记录**　卡端侧 `libcoi_device.so.0` 与 `coi_daemon` 由卡镜像自带，本包不含卡端二进制。

---
## 2026-10-06 — 宿主侧 COI 端到端打通

- **新增**　完整链路在真机上跑通：枚举引擎 → `COIProcessCreateFromFile` 返回 `COI_SUCCESS(0)` → 建管道 → 按名取函数句柄 → 提交调用 → 等完成事件 → 收结果 → 销毁管道与进程。
- **修复**　阻塞创建进程的三处缺陷：① 26496 字节的创建命令在 RMA 拷贝路径上按页步长算错，只有第一页正确；② 对端窗口长度按宿主页计算，未按 4 KiB 协议页换算；③ 卡端程序未加 `-rdynamic`，导出符号不在动态符号表，卡端 `dlsym` 取不到句柄（宿主看到 `COI_DOES_NOT_EXIST(5)`）。
- **变更**　公开 ABI 名的生成方式：非 x86 宿主不再发射 `.symver` 指令（新 binutils 对其限制变严），改由链接期别名产生同名符号。效果是 `dlvsym(handle, "…", "COI_1.0")` 一类按版本名查找的调用仍然命中 —— 这一点对 `liboffloadmic` 这类既有消费者很重要。
- **修复**　`cpuid`／`rdtsc` 两条 x86 专用指令换成等价实现（`sched_getcpu()` 与 `CLOCK_MONOTONIC`）。
- **新增**　构建分支补 `loongarch64`（原构建系统只认 x86 宿主，未知架构直接判为不支持）。
- **验证**　卡上 OpenMP 归约 2×10⁸ 项，240 个硬件线程，相对误差 −3.35e-16；N 体引力三档规模的校验和与宿主参考实现**逐位一致**（差 0.000e+00）。验收脚本 `tests/t5_offload.sh`（17 项）、`tests/t7_nbody.sh`（9 项）覆盖。
- **记录**　两条实测限制，已写进《offload 编程手册》：`COIBufferCreate` 在本移植链上返回 `COI_OUT_OF_MEMORY(13)`；`COIEngineGetInfo` 的结构体尺寸校验不通过。两者都不影响枚举、建进程、取句柄与提交调用。
## 配套项（非本包代码，但缺了它本包用不起来）

- **2026-10-05　k1om 交叉工具链在龙芯上可用**：让 MPSS 自带的 x86_64 版 k1om 编译器在龙芯上运行（经用户态模拟层），产物 `readelf -h` 报 `Intel K1OM`；调用方式固化成包装脚本。落地时解决三处障碍：RPM 解包只到内层载荷（改用 `bsdtar`）、编译器内部程序缺 `libmpc`／`libmpfr`／`libgmp`、SDK 内 `as`／`ld` 是指向绝对路径的符号链接（换目录即断）。
- **2026-10-05　自建卡端 `libgomp`**：MPSS 的编译器配置里写死了 `--disable-libgomp`，SDK 与卡镜像都没有它。以交叉模式单独构建，得到 720,422 字节的 `libgomp.so.1.0.0`（`Machine: Intel K1OM`），装进编译器运行时目录后 `-fopenmp` 开箱可用。
- **2026-10-05　卡端 worker `offload_target_main`**：用同一套工具链构建（90,073 字节，卡端 ELF），依赖全部命中，能在卡上加载并进入主流程。
- **2026-10-06　更正一条判据**：**不能**用「反汇编里有没有 `vpackstore`／`vscatter`」判断卡端程序会不会崩 —— 实测跑得通的 `-O0` 产物同样含 12 条，而崩掉的 `-O1`／`-O2` 各只有 11 条。规则收紧为「优化档逐源码实测」。

---
## 相关文档

- 编程手册（COI API 用法与参考）：`docs/OFFLOAD_GUIDE_CN.md`
- 打通过程的完整记录与实测数字：`docs/H-offload-field-notes_CN.md`
- 路线比较（编译器自动生成型 offload 为何走不通）：`docs/F-coi-and-openmp_CN.md`
- 验收测试：`tests/`
