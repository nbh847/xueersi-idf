#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_font_pack.py — 生成可复现的 XMF1 中文字体包（节点 15B）。

产物：
    assets/fonts/xiaomiao-zh-cn.xmf

XMF1 契约（小端固定宽度，逐字节与固件 C 解析器一致）：

    [Header 64B][Codepoints 2*gc][pad 到 4 对齐][Bitmap12 gc*36][Bitmap16 gc*64]

Header（64 字节，未列出字段全部为 0）：
    0x00 magic[4]          = "XMF1"
    0x04 schema_version u16= 1
    0x06 header_size     u16= 64
    0x08 glyph_count       u32
    0x0C codepoint_offset  u32= 64
    0x10 bitmap_12_offset  u32= (64 + 2*gc + 3) & ~3   # 4 对齐，pad 字节为 0
    0x14 bitmap_16_offset  u32= bitmap_12_offset + gc*36
    0x18 file_size         u32= bitmap_16_offset + gc*64
    0x1C payload_crc32     u32= zlib.crc32 覆盖 [codepoint_offset, file_size)
    0x20 u8=12 0x21 line_height_12=14 0x22 base_12=12 0x23 adv_12=12
    0x24 u8=16 0x25 line_height_16=19 0x26 base_16=16 0x27 adv_16=16
    0x28..0x3F reserved = 0

位图：每字形固定 size×size 单元、A2（2bit 灰度，3=最黑），行主序；
每行 ceil(size*2/8) 字节（12px=3B/行，16px=4B/行），每字节最高 2 位是最左
像素（MSB-first）。12px 单元 36 字节，16px 单元 64 字节。glyph ID = 码点在升
序数组中的下标。

栅格化规则（必须逐字实现，见 README.md 复述）：
    - Pillow ImageFont.truetype(otf, size=s)，s ∈ {12, 16}。
    - 在 64×64 'L' 画布上 ImageDraw.text((32, 32), ch, font, fill=255, anchor='mm')。
    - 裁剪固定单元窗口 [32-s/2, 32-s/2, 32+s/2, 32+s/2]。
    - 量化阈值：v<43→0；43<=v<128→1；128<=v<213→2；v>=213→3。
    - 打包：行主序、4 像素/字节、MSB-first；窗口即全部像素。

依赖：Pillow（精确 pin，见 requirements.txt）。
"""

import argparse
import hashlib
import os
import struct
import sys
import unicodedata
import zlib

# ---------------------------------------------------------------------------
# 冻结的格式常量
# ---------------------------------------------------------------------------
MAGIC = b"XMF1"
SCHEMA_VERSION = 1
HEADER_SIZE = 64

TIERS = (12, 16)
GLYPH_STRIDE = {12: 36, 16: 64}          # size * ceil(size*2/8)
ROWS_PER_BYTE = {12: 3, 16: 4}           # 每行字节数

# Header 度量字段（0x20..0x27）
METRICS_12 = (12, 14, 12, 12)            # size, line_height, base, advance
METRICS_16 = (16, 19, 16, 16)

PAD_START = 0x28                          # 0x28..0x3F 为保留 0

# ---------------------------------------------------------------------------
# 缺字/空白判定白名单
# ---------------------------------------------------------------------------
# 语义空白类（Unicode category Zs）允许全 0：U+0020 半角空格、U+3000 全角空格。
BLANK_Zs = True

# 固定窗口栅格化的已知边界样本：字符有真实墨迹，但在 16px 窗口内被裁到窗口外
# （anchor='mm' 居中 em-box，全角下划线落到低处于裁剪窗口之下）。这是冻结栅格
# 规则的确定性后果，不是字体缺字。逐项记录并给出原因，白名单不得扩张。
WINDOW_CLIPPED_WHITELIST = {
    0xFF3F: "U+FF3F FULLWIDTH LOW LINE: 16px 墨迹落在固定窗口下方（窗口外），12px 正常。",
}

GB2312_EXPECT_TOTAL = 7445


# ---------------------------------------------------------------------------
# 字符集
# ---------------------------------------------------------------------------
def gb2312_codepoints():
    """枚举 GB2312 双字节全集，返回有序去重的 Unicode 码点集合。"""
    cps = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                text = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            for ch in text:
                cps.add(ord(ch))
    return cps


def load_extra_chars(path):
    """读取扩展字符表（UTF-8，'#' 注释行）。返回按出现顺序去重的码点列表。"""
    extras = []
    seen = set()
    if not os.path.exists(path):
        return extras
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\r\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            for ch in line:
                cp = ord(ch)
                if cp in seen:
                    continue
                seen.add(cp)
                extras.append(cp)
    return extras


def is_blank(cp):
    ch = chr(cp)
    if BLANK_Zs and unicodedata.category(ch) == "Zs":
        return True
    return False


# ---------------------------------------------------------------------------
# 栅格化
# ---------------------------------------------------------------------------
def render_cell(img_font, ch, size):
    """按冻结规则渲染单字形，返回 size×size 的量化后二维像素（0..3）列表行主序。"""
    from PIL import Image, ImageDraw

    img = Image.new("L", (64, 64), 0)
    draw = ImageDraw.Draw(img)
    draw.text((32, 32), ch, font=img_font, fill=255, anchor="mm")
    half = size // 2
    box = (32 - half, 32 - half, 32 + half, 32 + half)
    cell = img.crop(box)
    return cell.load()  # 可直接 [x, y] 取灰度


def quantize(v):
    if v < 43:
        return 0
    if v < 128:
        return 1
    if v < 213:
        return 2
    return 3


def pack_cell(pixels, size):
    """把 size×size 量化像素打包为行主序、4 像素/字节、MSB-first 的字节串。"""
    bytes_per_row = (size * 2 + 7) // 8
    out = bytearray(size * bytes_per_row)
    for y in range(size):
        base = y * bytes_per_row
        for x in range(size):
            q = quantize(pixels[x, y])
            if q == 0:
                continue
            bitpos = 6 - ((x % 4) * 2)     # 最高 2 位是最左像素
            out[base + (x // 4)] |= (q << bitpos) & 0xFF
    return bytes(out)


def is_empty(pixels, size):
    for y in range(size):
        for x in range(size):
            if quantize(pixels[x, y]) != 0:
                return False
    return True


# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------
def build_header(glyph_count, codepoint_offset, bitmap12_offset, bitmap16_offset,
                 file_size, payload_crc32):
    hdr = bytearray(HEADER_SIZE)
    hdr[0x00:0x04] = MAGIC
    struct.pack_into("<H", hdr, 0x04, SCHEMA_VERSION)
    struct.pack_into("<H", hdr, 0x06, HEADER_SIZE)
    struct.pack_into("<I", hdr, 0x08, glyph_count)
    struct.pack_into("<I", hdr, 0x0C, codepoint_offset)
    struct.pack_into("<I", hdr, 0x10, bitmap12_offset)
    struct.pack_into("<I", hdr, 0x14, bitmap16_offset)
    struct.pack_into("<I", hdr, 0x18, file_size)
    struct.pack_into("<I", hdr, 0x1C, payload_crc32)
    hdr[0x20], hdr[0x21], hdr[0x22], hdr[0x23] = METRICS_12
    hdr[0x24], hdr[0x25], hdr[0x26], hdr[0x27] = METRICS_16
    # 0x28..0x3F 保持 0
    return bytes(hdr)


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------
def main(argv=None):
    parser = argparse.ArgumentParser(description="生成 XMF1 中文字体包")
    parser.add_argument("--font", required=True, help="源 OTF/TTF 路径")
    parser.add_argument("--out", required=True, help="输出 .xmf 路径")
    parser.add_argument("--extra", default=None,
                        help="扩展字符表路径（默认与本脚本同目录 required-extra-chars.txt）")
    args = parser.parse_args(argv)

    from PIL import ImageFont

    if not os.path.exists(args.font):
        print("ERROR: 源字体不存在: %s" % args.font, file=sys.stderr)
        return 2

    extra_path = args.extra or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                            "required-extra-chars.txt")

    # 1. GB2312 全集
    gb = gb2312_codepoints()
    if len(gb) != GB2312_EXPECT_TOTAL:
        print("ERROR: GB2312 码点数为 %d，期望 %d" % (len(gb), GB2312_EXPECT_TOTAL),
              file=sys.stderr)
        return 3
    han = sum(1 for c in gb if 0x4E00 <= c <= 0x9FFF)
    sym = len(gb) - han

    # 2. 扩展字符校验
    extras = load_extra_chars(extra_path)
    dup = [c for c in extras if c in gb]
    if dup:
        print("ERROR: 扩展字符与 GB2312 重复: %s" %
              ", ".join("U+%04X" % c for c in dup), file=sys.stderr)
        return 4
    non_bmp = [c for c in extras if c > 0xFFFF]
    if non_bmp:
        print("ERROR: 扩展字符超出 UTF-16 BMP: %s" %
              ", ".join("U+%04X" % c for c in non_bmp), file=sys.stderr)
        return 5

    # 3. 码点表：升序、严格唯一
    codepoints = sorted(gb | set(extras))
    glyph_count = len(codepoints)
    cp_index = {cp: i for i, cp in enumerate(codepoints)}
    assert glyph_count == len(set(codepoints)) == len(codepoints)

    # 4. 栅格化两档位图 + 缺字检测
    fonts = {s: ImageFont.truetype(args.font, size=s) for s in TIERS}
    blocks = {12: bytearray(), 16: bytearray()}
    missing = []
    empty_ok = []
    for cp in codepoints:
        ch = chr(cp)
        empty_this_cp = False
        for s in TIERS:
            pixels = render_cell(fonts[s], ch, s)
            if is_empty(pixels, s):
                empty_this_cp = True
                if not is_blank(cp) and cp not in WINDOW_CLIPPED_WHITELIST:
                    missing.append((cp, s))
            blocks[s] += pack_cell(pixels, s)
        if empty_this_cp and (is_blank(cp) or cp in WINDOW_CLIPPED_WHITELIST):
            empty_ok.append(cp)

    if missing:
        print("ERROR: 检测到缺字（渲染全 0 且非白名单）：", file=sys.stderr)
        for cp, s in missing:
            print("  U+%04X @ %dpx" % (cp, s), file=sys.stderr)
        return 6

    # 5. 组装 payload
    codepoint_bytes = bytearray()
    for cp in codepoints:
        codepoint_bytes += struct.pack("<H", cp)

    codepoint_offset = HEADER_SIZE
    bitmap12_offset = (HEADER_SIZE + len(codepoint_bytes) + 3) & ~3
    bitmap16_offset = bitmap12_offset + glyph_count * GLYPH_STRIDE[12]
    file_size = bitmap16_offset + glyph_count * GLYPH_STRIDE[16]

    pad_len = bitmap12_offset - (codepoint_offset + len(codepoint_bytes))
    assert pad_len >= 0 and (bitmap12_offset % 4) == 0
    pad = bytearray(pad_len)   # 全 0

    payload = bytearray()
    payload += codepoint_bytes
    payload += pad
    payload += blocks[12]
    payload += blocks[16]
    assert len(payload) == file_size - codepoint_offset

    payload_crc32 = zlib.crc32(bytes(payload)) & 0xFFFFFFFF

    header = build_header(glyph_count, codepoint_offset, bitmap12_offset,
                          bitmap16_offset, file_size, payload_crc32)
    image = bytearray(header) + payload
    assert len(image) == file_size

    # 6. 写文件
    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(image)

    digest = hashlib.sha256(bytes(image)).hexdigest()

    # 7. 统计
    ext_in_pack = [cp for cp in extras]
    print("XMF1 font pack generated")
    print("  source font      : %s" % args.font)
    print("  output           : %s" % args.out)
    print("  GB2312 codepoints: %d (han=%d, symbol=%d)" % (len(gb), han, sym))
    print("  extra codepoints : %d" % len(ext_in_pack))
    print("  glyph_count      : %d" % glyph_count)
    print("  file_size        : %d bytes" % file_size)
    print("  bitmap12_offset  : %d" % bitmap12_offset)
    print("  bitmap16_offset  : %d" % bitmap16_offset)
    print("  payload_crc32    : 0x%08X" % payload_crc32)
    print("  blank/window-clipped whitelisted (allowed empty): %s" %
          (", ".join("U+%04X" % c for c in empty_ok) or "none"))
    print("  sha256           : %s" % digest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
