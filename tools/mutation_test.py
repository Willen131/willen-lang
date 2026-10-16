#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
变异测试：验证测试集的有效性。

做法：在解释器源码里逐个注入小缺陷（变异体），每次编译后跑一遍
测试集。若测试集失败，说明这个缺陷被抓住了（变异体「被杀死」）；
若测试集仍然全绿，说明测试集漏掉了这类缺陷（变异体「存活」）。

用法：
    python tools/mutation_test.py

结束后源码会被还原到运行前的状态。
"""

import subprocess
import sys
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 每个变异体：文件名、原文、改后的文本、说明
MUTATIONS = [
    {
        "file": "src/interp.cpp",
        "old": "case Tok::PLUS:  return Value::makeInt(a + b);",
        "new": "case Tok::PLUS:  return Value::makeInt(a - b);",
        "desc": "整数加法改成减法",
    },
    {
        "file": "src/interp.cpp",
        "old": "case Tok::SLASH:\n                if (b == 0) throw RuntimeError{\"除数不能为零\", line};\n                return Value::makeInt(a / b);",
        "new": "case Tok::SLASH:\n                if (b == 0) throw RuntimeError{\"除数不能为零\", line};\n                return Value::makeInt(a % b);",
        "desc": "整数除法改成取模",
    },
    {
        "file": "src/interp.cpp",
        "old": "case Tok::LT:  return Value::makeBool(compareValues(l, r, line) < 0);",
        "new": "case Tok::LT:  return Value::makeBool(compareValues(l, r, line) <= 0);",
        "desc": "小于改成小于等于（比较运算符边界）",
    },
    {
        "file": "src/interp.cpp",
        "old": "                if (i < 0 || i >= n) {\n                    throw RuntimeError{\"数组下标越界：\" + std::to_string(i) +",
        "new": "                if (i < 0 || i > n) {\n                    throw RuntimeError{\"数组下标越界：\" + std::to_string(i) +",
        "desc": "数组越界检查放宽一格（i >= n 改成 i > n）",
    },
    {
        "file": "src/builtins.cpp",
        "old": "        } else if (auto s = std::get_if<std::string>(&args[0].v)) {\n            result = Value::makeInt(static_cast<int64_t>(s->size()));",
        "new": "        } else if (auto s = std::get_if<std::string>(&args[0].v)) {\n            result = Value::makeInt(static_cast<int64_t>(s->size()) + 1);",
        "desc": "字符串长度多算 1",
    },
    {
        "file": "src/builtins.cpp",
        "old": "            for (int64_t i = a; i < b; i++) out.push_back(v[i]);",
        "new": "            for (int64_t i = a; i <= b; i++) out.push_back(v[i]);",
        "desc": "切片右边界由开区间改成闭区间",
    },
    {
        "file": "src/builtins.cpp",
        "old": "    if (a.isNumber() && b.isNumber()) return a.toDouble() < b.toDouble();",
        "new": "    if (a.isNumber() && b.isNumber()) return a.toDouble() > b.toDouble();",
        "desc": "排序比较反向（升序变降序）",
    },
    {
        "file": "src/value.cpp",
        "old": "bool Value::truthy() const {\n    if (auto p = std::get_if<bool>(&v)) return *p;\n    if (std::holds_alternative<std::monostate>(v)) return false;\n    return true;\n}",
        "new": "bool Value::truthy() const {\n    if (auto p = std::get_if<bool>(&v)) return *p;\n    if (std::holds_alternative<std::monostate>(v)) return false;\n    if (auto p = std::get_if<int64_t>(&v)) return *p != 0;\n    return true;\n}",
        "desc": "真值判断把整数 0 也算作假",
    },
    {
        "file": "src/interp_stmt.cpp",
        "old": "        if (globals_.isConst(v->name)) {\n            throw RuntimeError{\"不能给常量「\" + v->name + \"」重新赋值\", line};\n        }\n",
        "new": "",
        "desc": "删掉常量重复赋值的检查",
    },
    {
        "file": "src/interp.h",
        "old": "    static constexpr int kMaxCallDepth = 200;",
        "new": "    static constexpr int kMaxCallDepth = 100000;",
        "desc": "递归深度上限从 200 放大到 100000（等于取消保护）",
    },
]


# 测试集单次运行的超时（秒）。
#
# 某些变异会让程序**死循环**而不是给出错误结果——例如「加法改成减法」
# 之后，测试里 `当 (计数 < 5) { 计数 = 计数 + 1 }` 这类循环永远不会结束。
# 这类变异同样算被测试集「杀死」（测试没能正常通过），
# 但不能让它一直挂着，所以必须设超时。
TEST_TIMEOUT = 30


def run(cmd, timeout=300):
    return subprocess.run(cmd, cwd=str(ROOT), capture_output=True,
                          timeout=timeout, shell=isinstance(cmd, str))


def build():
    """返回 (是否成功, 错误信息)。"""
    r = run(["bash", "build.sh"])
    err = r.stderr.decode("utf-8", errors="replace").strip()
    return r.returncode == 0, err


def test_result():
    """返回 'pass' / 'fail' / 'timeout'。"""
    try:
        r = subprocess.run(["./willen.exe", "test", "tests/"],
                           cwd=str(ROOT), capture_output=True,
                           timeout=TEST_TIMEOUT)
        return "pass" if r.returncode == 0 else "fail"
    except subprocess.TimeoutExpired:
        return "timeout"


def ensure_clean():
    """运行前要求工作区干净——否则中断后无法用 git 恢复。"""
    r = run(["git", "status", "--porcelain", "src/"])
    dirty = r.stdout.decode("utf-8", errors="replace").strip()
    if dirty:
        print("错误：src/ 下有未提交的改动，请先提交或还原再运行变异测试。")
        print(dirty)
        return False
    return True


def main():
    if not ensure_clean():
        return 1

    # 先确认基线是绿的，否则变异测试没有意义
    print("检查基线……", end=" ", flush=True)
    ok, err = build()
    if not ok:
        print("构建失败")
        print(err[:500])
        return 1
    if test_result() != "pass":
        print("测试集本身就不是全绿，无法进行变异测试")
        return 1
    print("基线正常（构建通过，测试全绿）\n")

    # 备份所有会被改动的文件
    backups = {}
    for m in MUTATIONS:
        if m["file"] not in backups:
            backups[m["file"]] = (ROOT / m["file"]).read_text(encoding="utf-8")

    results = []
    try:
        for idx, m in enumerate(MUTATIONS, 1):
            p = ROOT / m["file"]
            original = backups[m["file"]]
            # 显式指定换行符，避免 write_text 把 LF 转成 CRLF
            mutated = original.replace(m["old"], m["new"], 1)

            if mutated == original:
                results.append((idx, m["desc"], "无法注入", "原文未匹配"))
                print(f"  [{idx:2}] {m['desc']}\n        ⚠ 无法注入：原文未匹配")
                continue

            p.write_text(mutated, encoding="utf-8", newline="\n")

            ok, err = build()
            if not ok:
                results.append((idx, m["desc"], "编译失败", err[:120]))
                print(f"  [{idx:2}] {m['desc']}\n        ✓ 被杀死（编译失败）")
            else:
                res = test_result()
                if res == "pass":
                    results.append((idx, m["desc"], "存活", "测试未能发现"))
                    print(f"  [{idx:2}] {m['desc']}\n        ✗ 存活！测试集没抓住")
                elif res == "timeout":
                    results.append((idx, m["desc"], "被杀死",
                                    f"测试超时（{TEST_TIMEOUT} 秒），疑似死循环"))
                    print(f"  [{idx:2}] {m['desc']}\n"
                          f"        ✓ 被杀死（测试超时，疑似死循环）")
                else:
                    results.append((idx, m["desc"], "被杀死", "测试失败"))
                    print(f"  [{idx:2}] {m['desc']}\n        ✓ 被杀死")

            # 立刻还原，避免影响下一个变异体
            p.write_text(original, encoding="utf-8", newline="\n")
    finally:
        # 无论如何都还原全部文件
        for f, content in backups.items():
            (ROOT / f).write_text(content, encoding="utf-8", newline="\n")
        build()

    killed = sum(1 for r in results if r[2] in ("被杀死", "编译失败"))
    total = len(results)
    print(f"\n{'='*52}")
    print(f"变异体总数：{total}")
    print(f"被杀死：{killed}")
    print(f"存活：{sum(1 for r in results if r[2] == '存活')}")
    print(f"注入失败：{sum(1 for r in results if r[2] == '无法注入')}")
    print(f"杀死率：{killed}/{total} = {killed/total*100:.0f}%")
    print(f"{'='*52}")

    # 写出报告数据
    out = ROOT / "docs" / "变异测试结果.csv"
    with open(out, "w", encoding="utf-8", newline="") as f:
        f.write("序号,变异体,结果,说明\n")
        for idx, desc, res, note in results:
            f.write(f"{idx},{desc},{res},{note}\n")
    print(f"\n明细已写入 {out.relative_to(ROOT)}")

    return 0 if killed == total else 1


if __name__ == "__main__":
    sys.exit(main())
