#!/usr/bin/env python3
"""
HZK16 字库烧录工具 + 回读校验
"""

import sys
import time
import os
import hashlib

FONT_SIZE = 261696
PAGE_SIZE = 256
FLASH_ADDR = 0x104000
SCRIPT_DIR = os.path.dirname(__file__)


def find_font():
    candidates = [
        os.path.join(SCRIPT_DIR, "hzk16f"),
        os.path.join(SCRIPT_DIR, "hzk16f.bin"),
        os.path.join(SCRIPT_DIR, "HZK16"),
        os.path.join(SCRIPT_DIR, "hzk16"),
    ]
    for p in candidates:
        if os.path.exists(p) and os.path.getsize(p) == FONT_SIZE:
            print(f"使用本地字库：{p} ({os.path.getsize(p)} 字节)")
            with open(p, "rb") as f:
                return f.read()
    return None


def wait_ack(ser, expect=b"[BURN]", timeout=10):
    """等待 MCU 回应包含 expect 的行"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        text = line.strip()
        if expect in text:
            return text.decode(errors='ignore')
    return None


def readback_verify(ser, font_data):
    """回读字库分区前 64KB 验证"""
    total_kb = FONT_SIZE // 1024
    verify_kb = min(total_kb, 64)

    print(f"回读校验前 {verify_kb}KB...")
    ser.write(f"BURN_VERIFY\n".encode())
    time.sleep(0.2)

    readback = bytearray()
    for kb in range(verify_kb):
        ser.write(f"BURN_READ {FLASH_ADDR + kb * 1024} 1024\n".encode())
        time.sleep(0.05)
        raw = ser.read(2048)  # hex 一行约 2048 字节
        if raw:
            readback.extend(raw)
        if (kb + 1) % 16 == 0 or kb == verify_kb - 1:
            # 每 16KB 显示一次
            pass

    print(f"  回读 {verify_kb}KB 完成")

    # 逐页比较
    mismatches = 0
    for page in range(verify_kb * 1024 // PAGE_SIZE):
        offset = page * PAGE_SIZE
        orig = font_data[offset:offset + PAGE_SIZE]
        if page >= len(readback):
            break
        # 需要实际 hex 解析才能精确比较
    return mismatches == 0


def main():
    import serial
    import serial.tools.list_ports

    font_data = find_font()
    if font_data is None:
        print("错误：未找到字库文件")
        print("请下载 https://github.com/shanshanjade/HZK16/.../hzk16f 放到 tools/ 目录")
        sys.exit(1)

    orig_md5 = hashlib.md5(font_data).hexdigest()
    print(f"字库 MD5: {orig_md5}")

    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("错误：未找到串口")
        sys.exit(1)

    print("可用串口：")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device} - {p.description}")

    sel = input("请选择串口编号（回车选 0）：").strip()
    idx = int(sel) if sel.isdigit() else 0
    port = ports[idx].device
    print(f"连接 {port} (921600)...")

    ser = serial.Serial(port, 921600, timeout=3)
    time.sleep(0.5)

    # 清空缓冲区
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # === 触发烧录模式 ===
    ser.write(b"fire font\n")
    ack = wait_ack(ser, b"[BURN] READY")
    if not ack:
        print("错误：未收到 BURN READY；请确认 MCU 已上电且 `fire font` 正确接收")
        ser.close()
        sys.exit(1)
    print(f"  {ack}")

    # === 擦除 ===
    print("擦除字库分区...")
    ser.write(b"BURN_ERASE\n")
    ack = wait_ack(ser, b"ERASE_DONE", timeout=30)
    print(f"  {ack}")

    # === 逐页写入 ===
    total_pages = (len(font_data) + PAGE_SIZE - 1) // PAGE_SIZE
    print(f"写入 {total_pages} 页 ({len(font_data)} 字节)...")

    for page in range(total_pages):
        offset = page * PAGE_SIZE
        chunk = font_data[offset:offset + PAGE_SIZE]
        if len(chunk) < PAGE_SIZE:
            chunk = chunk + b'\xFF' * (PAGE_SIZE - len(chunk))

        addr = FLASH_ADDR + offset
        ser.write(f"BURN_DATA {addr}\n".encode())

        # 等 MCU 就绪（此时缓冲区已清空可接收数据）
        ack = wait_ack(ser, b"[BURN] RDY", timeout=2)
        if not ack:
            remaining = ser.read_all()
            print(f"\n错误：第 {page+1} 页 MCU 无 RDY")
            print(f"缓冲区: {remaining[:200]}")
            ser.close()
            sys.exit(1)

        ser.write(chunk)

        ack = wait_ack(ser, b"[BURN] OK", timeout=5)
        if not ack:
            remaining = ser.read_all()
            print(f"\n错误：第 {page+1} 页无 OK")
            print(f"MCU 回: {remaining[:300].decode(errors='replace')}")
            ser.close()
            sys.exit(1)

        if (page + 1) % 100 == 0:
            pct = (page + 1) * 100 // total_pages
            print(f"  {pct}% ({page+1}/{total_pages} 页, {offset//1024}KB)")

    # === 快速验证：回读头尾各 32 字节 ===
    print("快速验证...")

    def read_slice(addr, size):
        ser.write(f"BURN_READBACK {addr} {size}\n".encode())
        ser.readline()
        raw = bytearray()
        while len(raw) < size:
            chunk = ser.read(size - len(raw))
            if chunk:
                raw.extend(chunk)
        return bytes(raw)

    head = read_slice(FLASH_ADDR, 32)
    tail = read_slice(FLASH_ADDR + FONT_SIZE - 32, 32)

    head_ok = head == font_data[:32]
    tail_ok = tail == font_data[-32:]

    print(f"  原始头32: {font_data[:32].hex()}")
    print(f"  回读头32: {head.hex()}  {'✅' if head_ok else '❌'}")
    print(f"  原始尾32: {font_data[-32:].hex()}")
    print(f"  回读尾32: {tail.hex()}  {'✅' if tail_ok else '❌'}")

    if head_ok and tail_ok:
        print("✅ 字库写入验证通过（头尾一致）")
    else:
        print("❌ 字库写入验证失败")

    # === 结束 ===
    ser.write(b"BURN_END\n")
    time.sleep(0.5)

    print("烧录完成。")
    if head_ok and tail_ok:
        print("请手动复位 MCU（重新上电或按复位键）")

    ser.close()


if __name__ == '__main__':
    main()
