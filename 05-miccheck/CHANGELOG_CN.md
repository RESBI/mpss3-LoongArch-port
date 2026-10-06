# CHANGELOG — 自检工具（05-miccheck）

　　本文件记本包**功能更新**。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-05 — 自检工具迁移到 Python 3 并在真机全绿

- **变更**　由 Python 2 迁到 Python 3，共四类改动：`except X, e:` 语法、shebang、`subprocess.communicate()` 返回 bytes（需要解码后再比对）、`create_string_buffer().value` 也是 bytes。原代码在 Python 3 下多处直接抛异常，自检根本跑不到结果。
- **修复**　外部命令的路径查找：原代码把 `/sbin/…` 一类路径写死，发行版差异下会误报失败；改为先试写死路径，不存在再回退查 `PATH`。
- **新增**　`MPSS_FLASH_VERSION`／`SMC_FW_VERSION` 两个构建期常量：`make` 时写入被测卡的 Flash 与 SMC 版本号（默认取实测那张卡的值）。换卡后若自检报版本不匹配，用新值重跑 `make install` 即可，不必改代码。
- **验证**　真机上 `miccheck` 全绿输出 `Status: OK`。其中「ras daemon 可用」一项走 SCIF，需要设备权限（或在 root 下运行）：普通用户会看到该项失败，这与 `/dev/mic/scif` 的默认权限一致，不是移植缺陷。

---
## 相关文档

- 工具端逐条移植改动与判据：`docs/E-porting-patches_CN.md`
- 设备权限与特权边界：`docs/E-porting-patches_CN.md` §E.7
- 验收测试（T2 卡访问；自检与设备权限）：`tests/`
