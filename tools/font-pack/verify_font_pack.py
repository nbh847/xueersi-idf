#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_font_pack.py — 独立校验 XMF1 字体包（节点 15B）。

不复用 generate_font_pack.py 的渲染代码；仅按冻结的格式契约独立解析与校验：
    - Header 各字段、offset 单调性与 4 对齐、glyph_count 边界。
    - 码点严格递增（唯一）。
    - 两档位图块长度精确、file_size 精确、reserved/pad 必须为 0。
    - payload_crc32 覆盖 [codepoint_offset, file_size)。
    - 全 glyph 非空（空白类/窗口裁剪白名单除外，与生成器同一白名单）。
    - 两档集合一致（同一码点索引共享）。

可选：
    --expect-chars <file>  扫描 UI 文案，凡无法经 gb2312 codec 编码的字符必须
                           出现在码点表中，否则非零退出。
    --self-check <xmf> [--invalid <xmf>]
                           自检：完好包必须通过；对包内存翻转 payload_crc32 一个
                           字节必须报告 CRC 损坏；若给定 --invalid 资产样本，必须
                           以 CRC 错误被拒绝。

退出码：0 全部通过；非 0 任一校验失败。
"""

import argparse
import hashlib
import struct
import sys
import unicodedata
import zlib

MAGIC = b"XMF1"
SCHEMA_VERSION = 1
HEADER_SIZE = 64
GLYPH_STRIDE = {12: 36, 16: 64}

# 与生成器一致的空白/裁剪白名单（独立声明，避免导入耦合）。
WINDOW_CLIPPED_WHITELIST = {0xFF3F, 0x3000, 0x0020}


def allowed_empty(cp):
    if cp in WINDOW_CLIPPED_WHITELIST:
        return True
    try:
        return unicodedata.category(chr(cp)) == "Zs"
    except ValueError:
        return False


class Report:
    def __init__(self):
        self.errors = []
        self.notes = []

    def err(self, msg):
        self.errors.append(msg)

    def note(self, msg):
        self.notes.append(msg)


def cell_empty(buf, off, size):
    """判断打包后的 size×size 单元是否全 0（任意量化位非 0 即非空）。"""
    return all(b == 0 for b in buf[off:off + size * ((size * 2 + 7) // 8)])


def verify_bytes(data, expect=None):
    """校验一个 XMF1 字节镜像；返回 Report。expect: 需覆盖的码点集合（可选）。"""
    r = Report()
    n = len(data)
    if n < HEADER_SIZE:
        r.err("文件短于 %d 字节 Header" % HEADER_SIZE)
        return r

    if data[0x00:0x04] != MAGIC:
        r.err("magic 不是 XMF1: %r" % data[0x00:0x04])
        return r
    schema = struct.unpack_from("<H", data, 0x04)[0]
    header_size = struct.unpack_from("<H", data, 0x06)[0]
    glyph_count = struct.unpack_from("<I", data, 0x08)[0]
    cp_off = struct.unpack_from("<I", data, 0x0C)[0]
    b12_off = struct.unpack_from("<I", data, 0x10)[0]
    b16_off = struct.unpack_from("<I", data, 0x14)[0]
    file_size = struct.unpack_from("<I", data, 0x18)[0]
    crc_field = struct.unpack_from("<I", data, 0x1C)[0]

    r.note("glyph_count=%d cp_off=%d b12=%d b16=%d size=%d crc=0x%08X" %
           (glyph_count, cp_off, b12_off, b16_off, file_size, crc_field))

    if schema != SCHEMA_VERSION:
        r.err("schema_version=%d 期望 %d" % (schema, SCHEMA_VERSION))
    if header_size != HEADER_SIZE:
        r.err("header_size=%d 期望 %d" % (header_size, HEADER_SIZE))
    if glyph_count == 0:
        r.err("glyph_count 为 0")
    if cp_off != HEADER_SIZE:
        r.err("codepoint_offset=%d 期望 %d" % (cp_off, HEADER_SIZE))

    # reserved 0x28..0x3F 必须为 0
    if any(data[0x28:0x40]):
        r.err("reserved 字段 (0x28..0x3F) 非 0")

    # 度量字段
    m12 = tuple(data[0x20:0x24])
    m16 = tuple(data[0x24:0x28])
    if m12 != (12, 14, 12, 12):
        r.err("12px 度量 %r 期望 (12,14,12,12)" % (m12,))
    if m16 != (16, 19, 16, 16):
        r.err("16px 度量 %r 期望 (16,19,16,16)" % (m16,))

    # offset 推导与单调/对齐
    exp_b12 = (cp_off + 2 * glyph_count + 3) & ~3
    exp_b16 = exp_b12 + glyph_count * GLYPH_STRIDE[12]
    exp_size = exp_b16 + glyph_count * GLYPH_STRIDE[16]
    if b12_off != exp_b12:
        r.err("bitmap_12_offset=%d 期望 %d（4 对齐/大小）" % (b12_off, exp_b12))
    if b16_off != exp_b16:
        r.err("bitmap_16_offset=%d 期望 %d" % (b16_off, exp_b16))
    if file_size != exp_size:
        r.err("file_size=%d 期望 %d" % (file_size, exp_size))
    if not (cp_off <= b12_off <= b16_off):
        r.err("offset 非单调: cp=%d b12=%d b16=%d" % (cp_off, b12_off, b16_off))
    if b12_off % 4 != 0:
        r.err("bitmap_12_offset 未 4 对齐: %d" % b12_off)
    if n != file_size:
        r.err("实际文件 %d 字节 != file_size %d" % (n, file_size))
        return r

    # CRC 覆盖 [cp_off, file_size)
    payload = data[cp_off:file_size]
    calc = zlib.crc32(payload) & 0xFFFFFFFF
    if calc != crc_field:
        r.err("payload_crc32 不匹配: 存储 0x%08X 计算 0x%08X" % (crc_field, calc))
        return r

    # pad 必须为 0
    pad_start = cp_off + 2 * glyph_count
    if pad_start < b12_off and any(data[pad_start:b12_off]):
        r.err("对齐 pad 字节非 0")

    # 码点严格递增
    cps = [struct.unpack_from("<H", data, cp_off + 2 * i)[0] for i in range(glyph_count)]
    for i in range(1, glyph_count):
        if cps[i] <= cps[i - 1]:
            r.err("码点非严格递增: 索引 %d U+%04X <= U+%04X" %
                  (i, cps[i], cps[i - 1]))
            break

    # 全 glyph 非空（两档共享同一码点索引 -> 集合天然一致）
    empty_cp = []
    for i, cp in enumerate(cps):
        o12 = b12_off + i * GLYPH_STRIDE[12]
        o16 = b16_off + i * GLYPH_STRIDE[16]
        e12 = cell_empty(data, o12, 12)
        e16 = cell_empty(data, o16, 16)
        if (e12 or e16) and not allowed_empty(cp):
            empty_cp.append((cp, e12, e16))
    if empty_cp:
        for cp, e12, e16 in empty_cp[:20]:
            r.err("glyph 全空且非白名单: U+%04X (12px_empty=%s 16px_empty=%s)" %
                  (cp, e12, e16))
        if len(empty_cp) > 20:
            r.err("  ... 另有 %d 个空 glyph" % (len(empty_cp) - 20))

    # --expect-chars 覆盖检查
    if expect is not None:
        cpset = set(cps)
        missing = sorted(c for c in expect if c not in cpset)
        if missing:
            r.err("期望字符未被覆盖 %d 个: %s" %
                  (len(missing), ", ".join("U+%04X" % c for c in missing[:40])))

    return r


def load_expect_chars(path):
    """从 UI 文案文件读取需覆盖字符：凡无法经 gb2312 编码的字符纳入期望集。"""
    need = set()
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    for ch in text:
        cp = ord(ch)
        if cp < 0x20 or ch in "\r\n\t":
            continue
        try:
            ch.encode("gb2312")
            encodable = True
        except UnicodeEncodeError:
            encodable = False
        if not encodable:
            need.add(cp)
    return need


def main(argv=None):
    ap = argparse.ArgumentParser(description="校验 XMF1 字体包")
    ap.add_argument("xmf", nargs="?", help="待校验的 .xmf 文件")
    ap.add_argument("--expect-chars", metavar="FILE",
                    help="UI 文案文件：其中无法经 gb2312 编码的字符必须被覆盖")
    ap.add_argument("--self-check", action="store_true",
                    help="自检：完好包通过 + 内存翻转 CRC 必被拒绝 (+ --invalid 样本)")
    ap.add_argument("--invalid", metavar="FILE",
                    help="自检时额外校验的损坏样本（须以 CRC 错误被拒绝）")
    args = ap.parse_args(argv)

    if args.self_check:
        return do_self_check(args)

    if not args.xmf:
        print("ERROR: 需要 .xmf 文件路径（或使用 --self-check）", file=sys.stderr)
        return 2

    with open(args.xmf, "rb") as fh:
        data = fh.read()
    expect = load_expect_chars(args.expect_chars) if args.expect_chars else None
    rep = verify_bytes(data, expect=expect)
    for note in rep.notes:
        print("  " + note)
    sha = hashlib.sha256(data).hexdigest()
    if rep.errors:
        print("VERIFY FAIL: %s" % args.xmf, file=sys.stderr)
        for e in rep.errors:
            print("  - " + e, file=sys.stderr)
        return 1
    print("VERIFY OK: %s" % args.xmf)
    print("  file_size=%d  sha256=%s" % (len(data), sha))
    return 0


def do_self_check(args):
    """自检：完好包通过 + 翻转 CRC 字节必被拒 + 损坏样本必以 CRC 被拒。"""
    if not args.xmf:
        print("ERROR: --self-check 需要一个完好的 .xmf 作为基准", file=sys.stderr)
        return 2
    with open(args.xmf, "rb") as fh:
        good = bytearray(fh.read())

    ok = True

    rep = verify_bytes(bytes(good))
    if rep.errors:
        ok = False
        print("SELF-CHECK FAIL: 完好包未通过校验:", file=sys.stderr)
        for e in rep.errors:
            print("  - " + e, file=sys.stderr)
    else:
        print("SELF-CHECK: 完好包通过 (verify)")

    # 内存翻转 payload_crc32 低字节 -> 必须报告 CRC 损坏
    corrupt = bytearray(good)
    corrupt[0x1C] ^= 0x01
    rep2 = verify_bytes(bytes(corrupt))
    crc_errs = [e for e in rep2.errors if "crc" in e.lower()]
    if crc_errs:
        print("SELF-CHECK: 内存翻转 CRC 字节被正确拒绝: %s" % crc_errs[0])
    else:
        ok = False
        print("SELF-CHECK FAIL: 翻转 CRC 字节后未被拒绝", file=sys.stderr)

    # 可选：磁盘上的损坏样本必须以 CRC 错误被拒
    if args.invalid:
        with open(args.invalid, "rb") as fh:
            inv = fh.read()
        # 损坏样本除 CRC 字段外全部合法；校验器应报 CRC 不匹配
        rep3 = verify_bytes(inv)
        crc_errs3 = [e for e in rep3.errors if "crc" in e.lower()]
        other_errs = [e for e in rep3.errors if "crc" not in e.lower()]
        if crc_errs3 and not other_errs:
            print("SELF-CHECK: 损坏样本 %s 仅因 CRC 被拒绝: %s" %
                  (args.invalid, crc_errs3[0]))
        else:
            ok = False
            print("SELF-CHECK FAIL: 损坏样本 %s 拒绝原因异常" % args.invalid,
                  file=sys.stderr)
            for e in rep3.errors:
                print("  - " + e, file=sys.stderr)

    if ok:
        print("SELF-CHECK PASS")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
