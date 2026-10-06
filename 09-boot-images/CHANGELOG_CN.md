# CHANGELOG — 卡端引导镜像（09-boot-images）

　　本文件记本包**功能更新**，覆盖卡端 `bzImage` 与 initramfs（含卡端网络配置、授权密钥、服务启动项）。以「一项功能更新」为单位成条。新增内容追加在最尾端。

---
## 2026-10-04 — 镜像清单核对

- **验证**　逐项核对镜像内容与 ELF 头：`coi_daemon`、`libcoi_device.so.0`、`libmyo-service.so.0`、`libscif.so.0` 齐备（均为 k1om ELF）；glibc 2.21 与 `ld-linux-k1om.so.2` 构成卡端 ABI。
- **记录**　镜像内**没有**的东西：任何 OpenMP 运行时（`libgomp` 需自行构建）、任何 COI 应用程序、任何编译器（卡上不能编译，只能送二进制上去）。
- **记录**　`coi_daemon` 随开机自启：镜像里有 `/etc/init.d/coi` 与 `rc?.d` 下的启动链接，卡进 `online` 后 `pidof coi_daemon` 有值。此前用手工改制的镜像做 COI 探针时报「0 引擎」，根因正是那份镜像缺这个启动脚本。
- **记录**　`System.map` 是占位空文件：没有卡内核的符号表，`mpssd` 会打一条 `mmap of System.map failed` 告警，不影响引导。
- **记录**　卡端根文件系统是内存盘（`mount` 可见 tmpfs），每次上电即清空。offload 不需要往里预置任何东西；只有「原生运行」（不经 COI）才需要每次重新投送二进制。

---
## 2026-10-05 — 卡端镜像可用化

- **新增**　initramfs 补 `auto mic0` 静态网口配置。原交付里没有这一段（MPSS 的正常流程是由主机侧 `mpssd` 在引导时下发），手工引导或主机侧下发失败时卡端就没有网络可达性，后续 `ssh`、`scp` 与 SCIF 之上的管理路径全部走不通。
- **新增**　initramfs 内预置授权密钥，使主机可以直接 `ssh` 进卡。
- **记录**　卡端 `sshd` 是 2019 年的 OpenSSH 7.4，与本机新版 `scp` 协商不上（报 `Connection closed`）；往卡上传文件用 `ssh 卡 'cat > /tmp/文件' < 本地文件`。offload 本身不需要这一步（COI 自己把程序与依赖送上去）。
- **验证**　真机上卡引导完成后 `ssh` 可登录、`uname -a` 显示卡端内核 `2.6.38.8+mpss3.8.6`、`/proc/cpuinfo` 显示 61 个核心；`tests/t2_ssh.sh` 覆盖部署与执行。
## 相关文档

- 卡侧运行时清单与镜像核对：`docs/F-coi-and-openmp_CN.md`
- 引导与卡状态检查：`docs/11-verification_CN.md`
- 验收测试（T1 安装镜像、T2 卡访问）：`tests/`
