# CHANGELOG — 用户态 SCIF 库（02-libscif）

　　本文件记本包**功能更新**。以「一项功能更新」为单位成条，条内写清改了什么、为什么、怎么验证。新增内容追加在最尾端。

---
## 2026-10-04 — 构建系统接入

- **新增**　包内两个 Makefile：`Makefile.mpss` 为上游原始文件，`Makefile` 为本发布版的包装（构建 ＋ 按 MPSS 期望的路径安装），支持 `DESTDIR` 与 `PREFIX` 暂存安装。
- **修复**　包装 Makefile 的递归自读：`build` 目标最初写成 `$(MAKE) …` 而未指定 `-f Makefile.mpss`，会不断读自己造成无限递归（实测一路递归到 `make[7]`、日志涨到 42 万行）。所有调用改为显式 `-f Makefile.mpss` 或 `-C 子目录`。

---
## 2026-10-05 — 在 loongarch64 上构建、安装并可用

- **新增**　`libscif.so` 与头文件（`scif.h`、`scif_ioctl.h`）在 loongarch64 上构建并安装到 `$(PREFIX)/lib64` 与 `$(PREFIX)/include`；`-lscif` 可直接链接。
- **新增**　公开 ABI 名保持与上游一致：导出符号带版本名（`SCIF_1.0` 一类），由 `00-build-tools` 的 `gen-symver-map` 生成（非 x86 宿主改走链接期别名）。这样在 x86 上编译的二进制、以及按版本名查找符号的既有代码，语义不变。
- **记录**　本库是**纯透传**：`scif_register` 只把调用方给的长度、偏移、保护位、标志原样填进 ioctl 结构体，库内不含任何页大小常量。因此「注册粒度」这件事由调用方与驱动决定，本库不需要改动（对写程序的人来说，意味着直接按 4096 字节的整数倍注册是正确做法）。
- **验证**　真机跑通 SCIF 建链、64 字节与 26496 字节消息往返（0 字节不符）、卡端主动消息与双向 fence；验收脚本 `tests/t3_scif_comm.sh` 覆盖。
## 相关文档

- 工具端逐条移植改动：`docs/E-porting-patches_CN.md`
- 用户态代码规模与结构：`docs/04-host-userspace_CN.md`
- 验收测试（T3 消息通道、T4 数据面）：`tests/`
