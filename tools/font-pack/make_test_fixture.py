#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_test_fixture.py — 生成节点 15B 固件自测用的损坏字体样本
assets/fixtures/invalid-font.xmf。

历史样本是正式字体包的一字节 CRC 翻版（759 KB），与正式包同放会让
1.5 MB assets SPIFFS 镜像超出可用容量。改为合成最小包：结构、度量、
offset、码点与位图全部自洽（位图非空），仅 payload_crc32 字段低字节
翻转，使固件解析器在 CRC 校验处确定性返回 ESP_ERR_INVALID_CRC，
而不是在更早的长度/策略检查处返回其它错误。

生成是确定性的（固定码点与 0xAA 位图），可重复运行比对 SHA-256。
"""

import hashlib
import os
import struct
import sys
import zlib

HEADER_SIZE = 64
UNIT_12 = 36
UNIT_16 = 64
GLYPH_COUNT = 64


def build_pack():
    gc = GLYPH_COUNT
    cp_off = HEADER_SIZE
    bm12 = (cp_off + 2 * gc + 3) & ~3
    bm16 = bm12 + gc * UNIT_12
    file_size = bm16 + gc * UNIT_16

    codepoints = bytearray()
    for i in range(gc):
        codepoints += struct.pack("<H", 0x4E00 + i)  # 一、丁、丏…连续 CJK 区段
    pad = bytes((bm12 - (cp_off + len(codepoints))))

    bitmap12 = bytes([0xAA]) * (gc * UNIT_12)
    bitmap16 = bytes([0x55]) * (gc * UNIT_16)

    payload = codepoints + pad + bitmap12 + bitmap16
    assert len(payload) == file_size - cp_off
    crc = zlib.crc32(bytes(payload)) & 0xFFFFFFFF

    hdr = bytearray(HEADER_SIZE)
    hdr[0x00:0x04] = b"XMF1"
    struct.pack_into("<HHI", hdr, 0x04, 1, HEADER_SIZE, gc)
    struct.pack_into("<IIIII", hdr, 0x0C, cp_off, bm12, bm16, file_size, crc)
    hdr[0x20:0x28] = bytes([12, 14, 12, 12, 16, 19, 16, 16])
    # 0x28..0x3F reserved 保持 0

    data = bytearray(hdr + payload)
    data[0x1C] ^= 0x01  # 唯一的损坏点：payload_crc32 低字节
    return bytes(data)


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "assets", "fixtures", "invalid-font.xmf")
    data = build_pack()
    with open(out, "wb") as fh:
        fh.write(data)
    print("wrote %s (%d bytes) sha256=%s" %
          (os.path.normpath(out), len(data), hashlib.sha256(data).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
