#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
运行全部性能基准。

每个基准用 Willen 与 Python 各跑 3 次取中位数，输出对照表，
并写入 bench/results/ 下的 CSV 与 Markdown 文件。

用法：
    python bench/run_bench.py
"""

import subprocess
import sys
import time
from pathlib import Path

BENCHES = ["fib", "loop", "sort", "str", "maze"]
RUNS = 3

ROOT = Path(__file__).resolve().parent.parent
RESULT_DIR = ROOT / "bench" / "results"


def run_once(cmd):
    """跑一次，返回 (耗时秒, 标准输出)。失败返回 (None, 错误信息)。"""
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True)
    elapsed = time.perf_counter() - start

    # 比对前统一行尾：Python 在 Windows 上输出 CRLF，而 Willen 输出 LF。
    # 单行结果靠 strip() 就能掩盖差异，多行结果的中间 \r 却会留下，
    # 导致正确的输出被误判为「不一致」。
    out = proc.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n").strip()
    err = proc.stderr.decode("utf-8", errors="replace").replace("\r\n", "\n").strip()

    if proc.returncode != 0:
        return None, err or f"退出码 {proc.returncode}"
    return elapsed, out


def median(values):
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def main():
    willen = ROOT / "willen.exe"
    if not willen.exists():
        print("找不到 willen.exe，请先在项目根目录运行 bash build.sh", file=sys.stderr)
        return 1

    print(f"每个基准运行 {RUNS} 次，取中位数\n")
    print(f"{'基准':<8}{'Willen(秒)':>12}{'Python(秒)':>12}{'倍率':>9}   结果校验")
    print("-" * 62)

    rows = []

    for name in BENCHES:
        wl_times, py_times = [], []
        wl_out = py_out = None

        for _ in range(RUNS):
            t, out = run_once([str(willen), "run", f"bench/{name}.wl"])
            if t is None:
                print(f"\n{name}: Willen 运行失败 —— {out}", file=sys.stderr)
                return 1
            wl_times.append(t)
            wl_out = out

        for _ in range(RUNS):
            t, out = run_once([sys.executable, f"bench/{name}.py"])
            if t is None:
                print(f"\n{name}: Python 运行失败 —— {out}", file=sys.stderr)
                return 1
            py_times.append(t)
            py_out = out

        wl_med, py_med = median(wl_times), median(py_times)
        ratio = wl_med / py_med if py_med > 0 else float("inf")
        same = "一致" if wl_out == py_out else "!! 不一致 !!"

        print(f"{name:<8}{wl_med:>12.3f}{py_med:>12.3f}{ratio:>8.1f}×   {same}")
        rows.append((name, wl_med, py_med, ratio, same))

    RESULT_DIR.mkdir(parents=True, exist_ok=True)

    csv_path = RESULT_DIR / "bench.csv"
    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        f.write("基准,Willen秒,Python秒,倍率,结果一致\n")
        for name, wl, py, ratio, same in rows:
            f.write(f"{name},{wl:.3f},{py:.3f},{ratio:.1f},{same}\n")

    md_path = RESULT_DIR / "bench.md"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("| 基准 | Willen（秒） | Python（秒） | 倍率 | 结果一致 |\n")
        f.write("|---|---:|---:|---:|---|\n")
        for name, wl, py, ratio, same in rows:
            f.write(f"| `{name}` | {wl:.3f} | {py:.3f} | {ratio:.1f}× | {same} |\n")

    print(f"\n已写入 {csv_path.relative_to(ROOT)}")
    print(f"已写入 {md_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
