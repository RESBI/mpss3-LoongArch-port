# CHANGELOG — 构建辅助工具（00-build-tools）

　　本文件记本包**功能更新**。以「一项功能更新」为单位成条，条内写清改了什么、为什么、怎么验证。新增内容追加在最尾端。

　　本包提供三个构建期工具：`gen-symver-map`（生成符号版本映射脚本）、`gen-defsym.py`（从共享库生成符号别名／defsym 文件）、`mpss-metadata`（版本与来源元数据）。

---
## 2026-10-04 — 工具随发布版打包

- **新增**　本包不安装任何文件，只被其它包的构建过程调用；包装 Makefile 与其余包保持同一套写法（`build`／`install`／`clean`，支持 `DESTDIR`．`PREFIX`）。
- **验证**　在龙芯机器（Python 3.14）上，各包构建过程中本包工具正常执行；`mpss-metadata` 里的版本与提交号与产物 `modinfo` 中的 `build_scmver` 一致。

---
## 2026-10-05 — 符号版本与别名的两条路径

- **新增**　`gen-symver-map` 改为 Python 3 实现。原实现依赖 Python 2，在当代发行版上不可用；它在 `libscif`、`libcoi_host`、`libmyo-client` 的构建里生成 `.symver` 脚本，决定这些库导出的公开 ABI 名（如 `SCIF_1.0`、`COI_1.0`、`MYO_1.0`）。用法与输出格式保持与原来一致，构建脚本无需改动。
- **新增**　`gen-defsym.py`：在新 binutils 收紧 `.symver` 之后，为「不发射 `.symver` 指令」的宿主提供等价的链接期别名生成方式（把公开 ABI 名做成链接期别名），使 `dlvsym(..., "COI_1.0")` 一类按版本名查找的调用仍然命中。非 x86 宿主的构建走这条路，x86 宿主仍可走 `.symver`。
- **验证**　三个库在新的实现下编成，`objdump -T` 能看到预期的版本化符号名；宿主程序按版本名 `dlvsym` 取符号成功。
## 相关文档

- 工具端逐条移植改动与判据：`docs/E-porting-patches_CN.md`
- 发布版整体说明：`README_CN.md`
