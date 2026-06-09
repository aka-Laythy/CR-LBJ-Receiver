#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PDF 扫描件 OCR 识别工具
依赖: PyMuPDF, cnocr, Pillow
安装: pip install PyMuPDF cnocr Pillow
"""

import os
import sys
import time

def check_dependencies():
    missing = []
    try:
        import fitz
    except ImportError:
        missing.append("PyMuPDF")
    try:
        from cnocr import CnOcr
    except ImportError:
        missing.append("cnocr")
    try:
        from PIL import Image
    except ImportError:
        missing.append("Pillow")

    if missing:
        print(f"缺少依赖: {', '.join(missing)}")
        print(f"请运行: pip install {' '.join(missing)}")
        sys.exit(1)

def list_pdf_files():
    files = [f for f in os.listdir('.') if f.lower().endswith('.pdf')]
    files.sort()
    return files

def main():
    check_dependencies()

    import fitz
    from cnocr import CnOcr
    from PIL import Image
    import torch

    if torch.cuda.is_available():
        print(f"✅ 检测到 CUDA 设备: {torch.cuda.get_device_name(0)}")
    else:
        print("⚠️ 未检测到 CUDA，将使用 CPU 运行（识别速度较慢）")

    pdfs = list_pdf_files()
    if not pdfs:
        print("当前目录没有找到 PDF 文件。")
        return

    print("\n=== 当前目录 PDF 文件 ===")
    for i, f in enumerate(pdfs, 1):
        size = os.path.getsize(f) / 1024
        unit = "KB" if size < 1024 else "MB"
        size = size if size < 1024 else size / 1024
        print(f"  [{i}] {f}  ({size:.1f} {unit})")

    while True:
        try:
            choice = input("\n请输入要识别的编号（多个用逗号分隔，如 1,3；输入 0 退出）: ").strip()
            if choice == '0':
                return
            indices = [int(x.strip()) for x in choice.split(',') if x.strip()]
            selected = []
            for idx in indices:
                if 1 <= idx <= len(pdfs):
                    selected.append(pdfs[idx-1])
                else:
                    print(f"  ! 编号 {idx} 无效，已跳过")
            if not selected:
                print("没有有效选择，请重新输入")
                continue
            break
        except ValueError:
            print("输入格式错误，请输入数字编号，用逗号分隔")

    print("\n正在初始化 CnOcr 模型（首次运行会自动下载模型文件）...")
    start_init = time.time()
    ocr = CnOcr()
    print(f"模型加载完成，耗时 {time.time()-start_init:.1f} 秒")

    for pdf_name in selected:
        print(f"\n{'='*50}")
        print(f"开始处理: {pdf_name}")
        txt_name = os.path.splitext(pdf_name)[0] + '.txt'

        if os.path.exists(txt_name):
            overwrite = input(f"  文件 {txt_name} 已存在，是否覆盖? (y/n): ").strip().lower()
            if overwrite != 'y':
                print("  跳过")
                continue

        doc = fitz.open(pdf_name)
        total_pages = len(doc)
        print(f"  共 {total_pages} 页，使用 300 DPI 渲染...")

        all_text = []
        page_start = time.time()

        for page_num in range(total_pages):
            page = doc[page_num]
            pix = page.get_pixmap(dpi=300)
            img = Image.frombytes("RGB", [pix.width, pix.height], pix.samples)

            result = ocr.ocr(img)

            # 提取文本
            lines = []
            for line in result:
                if isinstance(line, dict):
                    lines.append(line.get('text', ''))
                elif isinstance(line, (list, tuple)):
                    lines.append(str(line[0]))
                else:
                    lines.append(str(line))

            page_text = '\n'.join(lines)

            # 每页加开始/结束标记
            all_text.append(f"第{page_num + 1}页开始\n{page_text}\n第{page_num + 1}页结束\n")

            # 调试输出：最多只打印前3行
            preview_lines = page_text.split('\n')[:3]
            preview = ' | '.join(preview_lines)
            if len(page_text.split('\n')) > 3:
                preview += " ..."
            print(f"  [{page_num + 1}/{total_pages}] {preview[:80]}")

            # 进度与预计时间
            elapsed = time.time() - page_start
            avg = elapsed / (page_num + 1)
            remain = avg * (total_pages - page_num - 1)
            if page_num < total_pages - 1:
                print(f"      已用 {elapsed:.0f}s | 预计剩余 {remain:.0f}s")

        doc.close()

        # 写入单个 TXT
        with open(txt_name, 'w', encoding='utf-8') as f:
            f.write(f"源文件: {pdf_name}\n")
            f.write(f"总页数: {total_pages}\n")
            f.write(f"识别时间: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write("=" * 40 + "\n\n")
            f.write('\n'.join(all_text))

        print(f"  ✅ 已保存: {txt_name}")

    print(f"\n{'='*50}")
    print("全部处理完成！")

if __name__ == '__main__':
    main()
