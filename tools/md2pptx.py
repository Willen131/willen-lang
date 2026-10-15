#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 Markdown 文档转换为 PowerPoint (.pptx)。

用法:
    python tools/md2pptx.py <输出.pptx> <输入.md>

分页规则:
    # 标题        → 封面页（标题 + 紧随其后的副标题段落）
    ## 标题       → 新的一页
    ### 标题      → 页内小标题
    ---           → 忽略（只用于分隔，不产生页面）
    表格          → 表格
    代码块        → 等宽文本框（深色背景）
    普通段落      → 正文要点
"""

import re
import sys
from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

# 幻灯片尺寸：16:9
SLIDE_W = Inches(13.333)
SLIDE_H = Inches(7.5)

CJK_FONT = "微软雅黑"
MONO_FONT = "Consolas"

COLOR_TITLE = RGBColor(0x1A, 0x1A, 0x1A)
COLOR_BODY = RGBColor(0x33, 0x33, 0x33)
COLOR_ACCENT = RGBColor(0x1F, 0x4E, 0x79)
COLOR_CODE_BG = RGBColor(0xF2, 0xF2, 0xF2)


def set_font(run, ascii_font, cjk_font, size=None, bold=None, color=None):
    run.font.name = ascii_font
    rPr = run._r.get_or_add_rPr()
    rFonts = rPr.find(qn("a:latin"))
    # python-pptx 对 pptx 的中文字体要单独设置 ea 属性
    ea = rPr.find(qn("a:ea"))
    if ea is None:
        ea = rPr.makeelement(qn("a:ea"), {})
        rPr.append(ea)
    ea.set("typeface", cjk_font)
    if size is not None:
        run.font.size = size
    if bold is not None:
        run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color


def add_textbox(slide, left, top, width, height):
    box = slide.shapes.add_textbox(left, top, width, height)
    tf = box.text_frame
    tf.word_wrap = True
    return tf


def fill_paragraph(p, text, size, bold=False, color=COLOR_BODY,
                   mono=False, indent=0):
    """把一行文本写进段落，处理 **粗体** 与 `行内代码`。"""
    p.level = indent
    p.space_after = Pt(6)
    if not text:
        return
    for piece in re.split(r"(\*\*.+?\*\*|`[^`]+`)", text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
            r = p.add_run(); r.text = piece[2:-2]
            set_font(r, MONO_FONT, CJK_FONT, size, True, color)
        elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
            r = p.add_run(); r.text = piece[1:-1]
            set_font(r, MONO_FONT, CJK_FONT, size, bold,
                     RGBColor(0xC0, 0x39, 0x2B))
        else:
            r = p.add_run(); r.text = piece
            set_font(r, MONO_FONT if mono else MONO_FONT, CJK_FONT,
                     size, bold, color)


def new_slide(prs):
    """用空白版式新建一页。"""
    return prs.slides.add_slide(prs.slide_layouts[6])


def add_title(slide, text, top=Inches(0.45), size=Pt(30)):
    tf = add_textbox(slide, Inches(0.7), top, SLIDE_W - Inches(1.4), Inches(0.9))
    p = tf.paragraphs[0]
    r = p.add_run(); r.text = text
    set_font(r, MONO_FONT, CJK_FONT, size, True, COLOR_ACCENT)
    # 标题下的横线
    line = slide.shapes.add_shape(1, Inches(0.7), top + Inches(0.82),
                                  SLIDE_W - Inches(1.4), Emu(12700))
    line.fill.solid()
    line.fill.fore_color.rgb = COLOR_ACCENT
    line.line.fill.background()
    line.shadow.inherit = False


def add_table(slide, rows, left, top, width, row_height):
    cols = max(len(r) for r in rows)
    shape = slide.shapes.add_table(len(rows), cols, left, top, width,
                                   row_height * len(rows))
    table = shape.table
    for i, row in enumerate(rows):
        for j in range(cols):
            cell = table.cell(i, j)
            cell.text = ""
            text = row[j] if j < len(row) else ""
            p = cell.text_frame.paragraphs[0]
            p.space_after = Pt(2)
            for piece in re.split(r"(\*\*.+?\*\*|`[^`]+`)", text):
                if not piece:
                    continue
                if piece.startswith("**") and piece.endswith("**"):
                    r = p.add_run(); r.text = piece[2:-2]
                    set_font(r, MONO_FONT, CJK_FONT, Pt(11), True)
                elif piece.startswith("`") and piece.endswith("`"):
                    r = p.add_run(); r.text = piece[1:-1]
                    set_font(r, MONO_FONT, CJK_FONT, Pt(11), False,
                             RGBColor(0xC0, 0x39, 0x2B))
                else:
                    r = p.add_run(); r.text = piece
                    set_font(r, MONO_FONT, CJK_FONT, Pt(11), i == 0)
    return table


def add_code_block(slide, lines, left, top, width):
    height = Inches(0.28) * len(lines) + Inches(0.2)
    box = slide.shapes.add_textbox(left, top, width, height)
    box.fill.solid()
    box.fill.fore_color.rgb = COLOR_CODE_BG
    box.line.fill.background()
    tf = box.text_frame
    tf.word_wrap = False
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.space_after = Pt(0)
        r = p.add_run()
        r.text = line if line else " "
        set_font(r, MONO_FONT, CJK_FONT, Pt(12), False, COLOR_BODY)
    return height


def convert(md_path, out_path):
    lines = Path(md_path).read_text(encoding="utf-8").splitlines()

    prs = Presentation()
    prs.slide_width = SLIDE_W
    prs.slide_height = SLIDE_H

    # ---- 封面页 ----
    cover_title = ""
    cover_sub = []
    i = 0
    while i < len(lines) and not lines[i].startswith("## "):
        line = lines[i].strip()
        if line.startswith("# ") and not cover_title:
            cover_title = line[2:].strip()
        elif line and not line.startswith("#") and line != "---":
            cover_sub.append(line)
        i += 1

    cover = new_slide(prs)
    tf = add_textbox(cover, Inches(1.2), Inches(2.4),
                     SLIDE_W - Inches(2.4), Inches(1.6))
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    r = p.add_run(); r.text = cover_title
    set_font(r, MONO_FONT, CJK_FONT, Pt(54), True, COLOR_ACCENT)

    if cover_sub:
        tf2 = add_textbox(cover, Inches(1.2), Inches(4.2),
                          SLIDE_W - Inches(2.4), Inches(1.2))
        for k, text in enumerate(cover_sub[:3]):
            p = tf2.paragraphs[0] if k == 0 else tf2.add_paragraph()
            p.alignment = PP_ALIGN.CENTER
            p.space_after = Pt(4)
            r = p.add_run(); r.text = text
            set_font(r, MONO_FONT, CJK_FONT, Pt(18), False, COLOR_BODY)

    # ---- 正文各页 ----
    slide = None
    y = Inches(1.6)
    body_w = SLIDE_W - Inches(1.4)
    in_code = False
    code_buf = []
    table_buf = []
    current_title = ""
    page_no = 0

    # 幻灯片底部留白：内容超过这条线就自动开新页
    BOTTOM = SLIDE_H - Inches(0.45)

    def start_page(title, continuation=False):
        nonlocal slide, y, current_title, page_no
        slide = new_slide(prs)
        page_no += 1
        shown = title + "（续）" if continuation else title
        add_title(slide, shown)
        y = Inches(1.6)
        if not continuation:
            current_title = title
        return slide

    def ensure_room(needed):
        """确保当前页还能放下 needed 高度，放不下就翻页。"""
        nonlocal slide
        if slide is None:
            start_page(current_title or "内容")
            return
        if y + needed > BOTTOM:
            start_page(current_title, continuation=True)

    def ensure_slide():
        nonlocal slide
        if slide is None:
            start_page(current_title or "内容")
        return slide

    def flush_code():
        nonlocal y, code_buf
        if not code_buf:
            return
        # 代码块不拆分：放不下就整块挪到下一页
        h = Inches(0.28) * len(code_buf) + Inches(0.2)
        if h < BOTTOM - Inches(1.6):
            ensure_room(h)
        s = ensure_slide()
        add_code_block(s, code_buf, Inches(0.7), y, body_w)
        y += h + Inches(0.2)
        code_buf = []

    def flush_table():
        nonlocal y, table_buf
        if not table_buf:
            return
        rows = [[c.strip() for c in l.strip().strip("|").split("|")]
                for l in table_buf
                if not re.match(r"^\s*\|[\s:|-]+\|\s*$", l)]
        table_buf = []
        if not rows:
            return
        h = Inches(0.34) * len(rows)
        if h < BOTTOM - Inches(1.6):
            ensure_room(h + Inches(0.25))
        s = ensure_slide()
        add_table(s, rows, Inches(0.7), y, body_w, Inches(0.34))
        y += h + Inches(0.25)

    for raw in lines[i:]:
        line = raw.rstrip()

        # 代码块
        if line.strip().startswith("```"):
            if in_code:
                flush_code()
                in_code = False
            else:
                flush_table()
                in_code = True
            continue
        if in_code:
            code_buf.append(line)
            continue

        # 表格
        if line.strip().startswith("|") and line.strip().endswith("|"):
            table_buf.append(line)
            continue
        flush_table()

        stripped = line.strip()

        if not stripped or stripped == "---":
            continue

        # 新页
        if stripped.startswith("## "):
            start_page(stripped[3:].strip())
            continue

        # 页内小标题
        if stripped.startswith("### "):
            ensure_room(Inches(0.5))
            s = ensure_slide()
            tf2 = add_textbox(s, Inches(0.7), y, body_w, Inches(0.45))
            p = tf2.paragraphs[0]
            p.space_after = Pt(2)
            r = p.add_run(); r.text = stripped[4:].strip()
            set_font(r, MONO_FONT, CJK_FONT, Pt(19), True, COLOR_ACCENT)
            y += Inches(0.5)
            continue

        if stripped.startswith("# "):
            continue

        # 列表与普通段落
        m = re.match(r"^[-*+]\s+(.*)$", stripped)
        text = m.group(1) if m else stripped
        indent = 1 if m else 0
        # 段落按字符数估算高度：每行约 34 个汉字
        est_lines = max(1, (len(text) + 33) // 34)
        need = Inches(0.32) * est_lines + (Inches(0.1) if indent else 0)

        ensure_room(need)
        s = ensure_slide()
        tf2 = add_textbox(s, Inches(0.9), y, body_w - Inches(0.2),
                          need + Inches(0.2))
        p = tf2.paragraphs[0]
        fill_paragraph(p, text, Pt(15), indent=indent)
        y += need + Inches(0.06)

    flush_table()
    flush_code()

    prs.save(out_path)
    return out_path


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    out, src = sys.argv[1], sys.argv[2]
    if not Path(src).exists():
        print(f"错误：找不到输入文件 {src}", file=sys.stderr)
        return 1
    convert(src, out)
    prs = Presentation(out)
    print(f"已生成 {out}（{len(prs.slides)} 页，"
          f"{Path(out).stat().st_size:,} 字节）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
