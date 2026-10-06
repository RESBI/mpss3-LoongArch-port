# CHANGELOG — 主机守护进程与管理 CLI（03-mpss-daemon）

　　本文件记本包**功能更新**，覆盖 `libmpssconfig.so`、主机守护进程 `mpssd`、管理 CLI `micctrl`，以及随包安装的 systemd 单元与设备权限规则。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-05 — 移植修补

- **修复**　发行版探测补 AOSC／LoongArch 分支。缺这个分支时环境初始化直接失败，后续所有管理命令都不可用。
- **修复**　`parse_shadow` 里原代码自带的未定义行为：`*lastd[1]` 应为 `(*lastd)[1]`（在 x86 上碰巧不炸，换架构后可能崩）。
- **修复**　`getcwd() < 0` 改为 `== NULL`（前者在成功路径上语义错误）。
- **变更**　卡端 SSH 主机密钥由 `rsa1`／`dsa` 改为 `ed25519`／`rsa`／`ecdsa` —— 前两者在当代 OpenSSH 上已被移除或默认禁用，生成的镜像若不改，卡端 `sshd` 起不来。

---
## 2026-10-05 — 管理路径在龙芯上跑通

- **新增**　`micctrl` 安装为 setuid root。这是一条有意的设计选择：读 sysfs 的功能普通用户即可，走 SCIF 的功能要先打开 `/dev/mic/scif`（默认 root 独占），而 `micctrl` 两类都要做。
- **验证**　`micctrl --status`、`mpssinfo`、`miccheck`、卡的 SSH 登录在真机上走通；`micctrl --useradd=<用户>` 在「主机 `mpssd` 于卡端启动时正在监听」这一前提下走通，卡端 `/var/log/mpssd` 出现 `[UserAdd] '…' Success`。
- **记录**　时序约束：卡端 `mpssd` 启动时先连主机 `mpssd` 的 160 端口发 `MONITOR_START`，**只有握手成功后才创建监听 164 端口的线程**。若卡先于主机 `mpssd` 启动，走 164 端口的操作会连到不存在的监听者；在主机 `mpssd` 起来后重启一次卡端 `mpssd` 即可补上。
## 2026-10-05 — 系统集成：让 MPSS 栈在当代发行版上自启动

- **新增**　`mpss.service`：以 `Type=simple` ＋ `mpssd -l` 运行（`mpssd` 默认会 fork，父进程随后调用 `pause()` 永不退出；用 `Type=forking` 会让 systemd 一直等 PIDFile 直到超时失败）。安装时自动 `daemon-reload` 与 `enable`，内核模块已加载时顺带把服务拉起来。
- **新增**　`mic0-net.service` 与 `/usr/libexec/mpss/mic0-up.sh`：网口 `mic0` 一出现就按 `/etc/mpss/mic0.conf` 的 `Network` 行配置地址与 MTU。这样做的原因是 `Network` 行的 `modhost=yes` 会去改**发行版**的网络配置（Debian 的 `/etc/network/interfaces` 或 Red Hat 的 `ifcfg-*`），而 AOSC 一类发行版没有这些文件；脚本改为自己配，且以 MPSS 配置为唯一来源。
- **新增**　安装现代 udev 规则把 `/dev/mic/scif`、`/dev/mic/ctrl` 放开为 0666（原规则的 `NAME=` 旧写法在 systemd-udev 下被忽略，设备会退回 root 独占）。规则文件由内核模块包一并安装。
- **验证**　真机上 `micctrl --status` 输出 `mic0: online`；`systemctl status mpss` 正常；`mic0` 自动 up 并带预期地址。
## 相关文档

- 工具端逐条移植改动与判据：`docs/E-porting-patches_CN.md`
- 安装与验证步骤：`docs/11-verification_CN.md`
- 验收测试（T1 安装与启动）：`tests/`
