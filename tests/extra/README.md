# tests/extra — fine-grained functional test set (separate from run_tests.sh)

　　This set is **independent of `run_tests.sh`**: that one covers stage acceptance (T0-T8 — installation, messaging, data path, offload, bulk transfer), while these cases assert single-API behaviour item by item, turning the rules of Appendix L (per-API determinism) and Appendix K (memory-movement constraints) into executable checks.

## Running

```bash
bash tests/extra/run_extra.sh          # all cases
bash tests/extra/run_extra.sh --list   # list cases
bash tests/extra/run_extra.sh -o x03   # only cases whose name contains x03
bash tests/extra/run_extra.sh -q       # summary only
```

　　Exit code 0 means no failures; logs land in `tests/extra/logs/<timestamp>/`, one file per case.

## Cases

| Case | What it covers | Key criteria |
|---|---|---|
| `x01_env.sh` | Environment and prerequisites: host page-size class, the conversion factor, the **wire-pages-per-chunk invariant (512)**, card state, device permissions, and whether the installed module carries this port's probes (checked by decompressing the module and searching strings) | Page size is 4/16/64 KiB; invariant <= 4095; card `online`; `/dev/mic/scif` readable and writable by a normal user |
| `x02_window_direction.sh` | **Directionality of window sizing**: cross-compiles and deploys `probe_srv`, then registers 1/4/8/16 MiB windows on the host side with `probe_cli window` | Host-side registration succeeds; no `kernel BUG`, no SMPT refcount underflow |
| `x03_rma_ladder.sh` | **Single-RMA size ladder**: 26496 B up to 1 MiB, window 1 MiB, total 64 MiB | Checksums match on both sides at every step; no leftover process and no kernel exception |
| `x04_kernel_audit.sh` | Kernel log audit and wedge check (runnable after any case) | No `kernel BUG` / `ref_count < 0` / `Oops`; DMA channel timeouts <= 2; no process in `D` state; card still `online` |

## Relation to the other suites

- `run_tests.sh` (T0-T8): stage acceptance across installation, messaging, data path, offload and bulk transfer;
- `tests/extra` (x01...): per-item API assertions, finer grained, individually runnable, quick to regress after a change;
- `precheck.sh`: compiles only, never touches the card, so build problems stay separate from runtime ones.

## Writing a new case

1. Name it `xNN_description.sh` with `NN` increasing;
2. Start with `source tests/extra/lib/extra.sh` for the recording helpers (`extra_pass/extra_fail/extra_skip/extra_expect_*`), and additionally `source tests/lib/common.sh` when the card or SCIF is needed;
3. A missing prerequisite is `extra_skip`, not a failure;
4. End with `exit 0` (failures are already recorded) so the runner can tally them;
5. When a case starts a card-side program, clean it up (`pkill -x <name>`) so later cases are not disturbed.
