#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""为 COI 生成「公开 ABI 名 -> 实现名」的链接期别名（移植版需要）。

背景：COI 的公开 ABI 名（COIEngineGetHostname@@COI_1.0 这类）原本靠
    __asm__(".symver COIEngineGetHostname1,COIEngineGetHostname@@COI_1.0");
建立，而被包含该头的每个目标文件只定义其中几个符号，其余在那个文件里是外部符号 ——
新 binutils 拒绝给外部符号声明默认版本：

    Error: invalid attempt to declare external version name as default in symbol ...

移植版的做法：非 x86 目标不再发射那段 .symver（补丁 9），改为在链接期建立别名。
两种模式：
    --mode defsym   输出 `-Wl,--defsym,公开名=实现名` 一串参数（COI 用这个，已验证）
    --mode asm      输出汇编文件（.globl/.set），适合版本脚本里没有 local: * 的库

版本节点仍由各包既有的版本脚本（coi_version_linker_script.map 等）指定。

用法（在包目录里执行）：
    gen-defsym.py --pkg coi --mode defsym --out build/coi_defsym.args
    gen-defsym.py --pkg myo --mode asm     --out src/myo_aliases.s
"""
import argparse
import pathlib
import re
import sys

PAT = re.compile(r'\.symver\s+(\w+)\s*,\s*(\w+)@@([\w.]+)"\s*\)\s*;')

HEADERS = {
    'coi': 'src/include/internal/coi_version_asm.h',
    'myo': 'src/include/myo_version_asm.h',
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg', choices=['coi', 'myo', 'auto'], default='auto')
    ap.add_argument('--mode', choices=['defsym', 'asm'], default='defsym')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    cwd = pathlib.Path.cwd()
    if a.pkg == 'auto':
        for name, rel in HEADERS.items():
            if (cwd / rel).exists():
                a.pkg = name
                break
        else:
            print('找不到 COI 或 MYO 的版本汇编头（请在对应包目录里运行）', file=sys.stderr)
            return 1

    header = cwd / HEADERS[a.pkg]
    if not header.exists():
        print('缺头文件：%s' % header, file=sys.stderr)
        return 1

    pairs = [(m.group(2), m.group(1))
             for m in PAT.finditer(header.read_text(encoding='utf-8', errors='replace'))]
    if not pairs:
        print('没解析出别名（%s）' % header, file=sys.stderr)
        return 1

    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if a.mode == 'defsym':
        out.write_text(' '.join('-Wl,--defsym,%s=%s' % (pub, impl) for pub, impl in pairs))
    else:
        with open(out, 'w') as fh:
            fh.write('/* 自动生成：%s 的公开 ABI 名别名（移植版，见 gen-defsym.py） */\n' % a.pkg)
            for pub, impl in pairs:
                fh.write('.globl %s\n.set %s, %s\n' % (pub, pub, impl))

    print('%s：%d 个别名（%s 模式）-> %s' % (a.pkg, len(pairs), a.mode, out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
