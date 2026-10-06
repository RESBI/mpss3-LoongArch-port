# CHANGELOG — 卡管理库与命令行工具（04-mpss-micmgmt）

　　本文件记本包**功能更新**，覆盖 `libmicmgmt.so` 与 `mpssinfo`、`mpssflash`、`micsmc`。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-05 — 库与工具在 loongarch64 上构建、安装并可用

- **新增**　`libmicmgmt.so` 与三个命令行工具在 loongarch64 上构建并安装（库到 `$(PREFIX)/lib64`、工具到 `$(PREFIX)/bin`、头文件到 `$(PREFIX)/include`）。
- **新增**　`mpssinfo` 能读出卡的 SKU、序列号、核数、温度与 Flash 版本。这些值分两类来源：设备枚举、SKU、POST 码一类经 sysfs 读取（普通用户即可）；序列号、UUID、显存／核心信息、温度、RAS 一类经 SCIF 读取，需要先能打开 `/dev/mic/scif`（默认 root 独占，装 udev 规则后普通用户也可用）。
- **修复**　去掉 `inline` 伪声明：原代码有若干处只写 `inline` 声明而没有定义，GCC 14 起这是错误。
- **修复**　覆盖包内 `EXTRA_CFLAGS` 里的 `-Werror`，避免新工具链的告警把构建打断（告警本身仍会打印，便于后续收敛）。
- **验证**　`mpssinfo` 在真机上输出完整的卡信息；`mpssflash` 能读到 Flash 版本并用于与 `miccheck` 的版本常量比对。

---
## 相关文档

- 工具端逐条移植改动与判据：`docs/E-porting-patches_CN.md`
- 主机用户态代码规模与结构：`docs/04-host-userspace_CN.md`
- 验收测试（T2 卡访问与设备权限）：`tests/`
