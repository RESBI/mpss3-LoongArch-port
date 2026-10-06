# tests/extra — 细粒度功能测试集（与 run_tests.sh 分开）

　　这一套是**独立于 `run_tests.sh`** 的细粒度用例集：`run_tests.sh` 管"阶段式验收"（T0–T8，装机/通信/数据面/offload/大数据量），这里管**单个 API 行为的逐项断言**，用来把附录 L（逐 API 确定性规范）与附录 K（内存搬运约束）里的规则变成可执行的检查。

## 怎么跑

```bash
bash tests/extra/run_extra.sh          # 全部用例
bash tests/extra/run_extra.sh --list   # 列出用例
bash tests/extra/run_extra.sh -o x03   # 只跑名字含 x03 的用例
bash tests/extra/run_extra.sh -q       # 只打印统计
```

　　退出码：0 = 无失败项；1 = 有失败项。日志在 `tests/extra/logs/<时间戳>/`，每个用例一份。

## 用例一览

| 用例 | 覆盖什么 | 关键判据 |
|---|---|---|
| `x01_env.sh` | 环境与前提：宿主页大小档位、换算因子、**单段线上一页数不变量（512）**、卡状态、设备权限、已装模块是否含本次移植的探针（解压后查字符串） | 页大小属 4/16/64 KiB；不变量 ≤4095；卡 `online`；`/dev/mic/scif` 普通用户可读写 |
| `x02_window_direction.sh` | **窗口尺寸的方向性**：交叉编译并投送 `probe_srv`，用 `probe_cli window` 在宿主方向注册 1/4/8/16 MiB 窗口 | 宿主方向注册成功；无 `kernel BUG`、无引用计数下溢 |
| `x03_rma_ladder.sh` | **单次 RMA 尺寸阶梯**：26496 B → 1 MiB，窗口 1 MiB、总量 64 MiB | 每档两端 checksum 一致；无残留进程与内核异常 |
| `x04_kernel_audit.sh` | 内核日志审计与卡死体检（可在任何用例之后跑） | 无 `kernel BUG`/`ref_count < 0`/`Oops`；DMA 通道超时 ≤2；无 `D` 状态进程；卡仍 `online` |

## 与另两套的关系

- `run_tests.sh`（T0–T8）：装机、通信、数据面、offload、大数据量的**阶段验收**；
- `tests/extra`（x01…）：**逐项 API 断言**，粒度更细、可单跑、可在改动后快速回归；
- `precheck.sh`：只编译不接触卡，用来把编译问题与运行问题分开。

## 写新用例的约定

1. 文件名 `xNN_描述.sh`，`NN` 递增；
2. 开头 `source tests/extra/lib/extra.sh` 取记录助手（`extra_pass/extra_fail/extra_skip/extra_expect_*`），需要卡或 SCIF 时再 `source tests/lib/common.sh`；
3. 缺前提要 `extra_skip` 而不是失败；
4. 结尾用 `exit 0`（失败项已通过记录体现），runner 会按记录统计；
5. 涉及卡端程序时，务必在用例结束前清理（`pkill -x <名字>`），避免污染后续用例。
