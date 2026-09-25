# font-pack — XMF1 中文字体包生成工具（节点 15B）

可复现地把一款开放许可证字体栅格化为项目私有、只读、版本化的 **XMF1** 字体包，
供固件 Font Service 从本机 Flash 流式读取。本工具只用于开发／更新资源；普通固件
构建直接消费已提交到 `assets/fonts/xiaomiao-zh-cn.xmf` 的产物，不联网、不下载字体、
不运行本工具。

## 目录内容

| 文件 | 作用 |
| --- | --- |
| `generate_font_pack.py` | 枚举字符集、栅格化两档位图、组装并写出 `.xmf`；缺字即非零退出。 |
| `verify_font_pack.py` | 独立校验器（不依赖生成代码）：Header/offset/对齐/码点递增/块长/CRC/全 glyph 非空；`--expect-chars`、`--self-check`。 |
| `requirements.txt` | 精确 pin 的 Pillow（产物可复现的唯一第三方依赖）。 |
| `required-extra-chars.txt` | GB2312 之外的扩展字符表（当前仅注释；15C 补真实缺字）。 |

## 依赖与环境

只在仓库内的隔离 venv 安装依赖，绝不全局安装：

```bash
python -m venv tools/font-pack/.venv
tools/font-pack/.venv/Scripts/python.exe -m pip install -r tools/font-pack/requirements.txt
```

- 已验证运行环境：Python 3.11.15，Pillow 11.3.0。
- **可复现性强依赖 Pillow 版本**（FreeType 灰度输出随版本变化）。升级 Pillow 必须
  重新生成、比对 SHA-256 并更新 `assets/LICENSES/font-source.txt`。

## 字体来源（决策 13：可再分发 + 固定版本 + SHA-256）

- 字体：Noto Sans CJK SC Regular，版本 2.004，许可证 SIL OFL 1.1。
- 下载 URL（唯一权威指纹为源文件 SHA-256）：
  `https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- 源文件 SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可证全文：`assets/LICENSES/noto-sans-cjk-ofl.txt`（同仓库 `Sans/LICENSE`）。
- 完整来源记录：`assets/LICENSES/font-source.txt`。
- 源 OTF 不提交进仓库，仅放 `.tmp/node-15-assets/`。

```bash
curl -L -o .tmp/node-15-assets/NotoSansCJKsc-Regular.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf
```

## 字符集

1. **GB2312 全集**：枚举双字节码 lead `0xA1..0xF7`、trail `0xA1..0xFE`，用 Python
   `gb2312` codec 解码并去重，结果为 **7445** 个 Unicode 码点（6763 汉字 + 682 符号）。
   数量不符工具立即报错退出。
2. **扩展字符**：读取 `required-extra-chars.txt`（UTF-8，`#` 注释行）。每个扩展字符
   必须能编码进 UTF-16 BMP（`<=0xFFFF`）且不与 GB2312 重复，否则非零退出。最终码点表
   = GB2312 ∪ 扩展，升序且严格唯一。
3. **缺字判定**：任何码点渲染为全 0 且不属于空白类即记为缺字，工具非零退出。

### 空白 / 裁剪白名单（诚实记录，不静默扩张）

- 语义空白：Unicode category `Zs` 允许全 0，含 **U+0020** 与 **U+3000**（GB2312 全角空格）。
- **U+FF3F ＿ FULLWIDTH LOW LINE**：12px 正常；16px 因固定窗口居中（`anchor='mm'`）后墨迹
  落在裁剪窗口下方而在 12/16 打包窗口内全 0。这是**冻结栅格规则的确定性后果**，非字体缺字，
  已显式登记为窗口裁剪白名单。

## 栅格化与量化规则（`generate_font_pack.py` 逐字实现）

- `ImageFont.truetype(<otf>, size=s)`，`s ∈ {12, 16}`。
- 在 64×64 `'L'` 画布上用 `ImageDraw.text((32, 32), ch, font=f, fill=255, anchor='mm')`
  以 (32,32) 为中心居中排布整幅字面。
- 裁剪固定单元窗口 `[32-s/2, 32-s/2, 32+s/2, 32+s/2]`（12px → 26..38；16px → 24..40）。
- 量化阈值固定：`v<43→0`；`43<=v<128→1`；`128<=v<213→2`；`v>=213→3`。
- 打包：行主序、4 像素/字节、MSB-first（每字节最高 2 位是最左像素）；窗口即全部像素。
- 12px 单元 36 字节（3B/行），16px 单元 64 字节（4B/行）。

## XMF1 格式契约（与固件 C 解析器逐字节一致）

小端固定宽度。文件布局：
`[Header 64B][Codepoints 2*gc][pad 到 4 对齐][Bitmap12 gc*36][Bitmap16 gc*64]`

Header（64 字节，未列出字段全部为 0）：

```
0x00 magic[4]        = "XMF1"
0x04 schema_version u16 = 1
0x06 header_size   u16 = 64
0x08 glyph_count   u32
0x0C codepoint_offset  u32 = 64
0x10 bitmap_12_offset  u32 = (64 + 2*gc + 3) & ~3   # 4 对齐，pad 字节为 0
0x14 bitmap_16_offset  u32 = bitmap_12_offset + gc*36
0x18 file_size          u32 = bitmap_16_offset + gc*64
0x1C payload_crc32      u32 = zlib.crc32 覆盖 [codepoint_offset, file_size) 全部字节
0x20 u8=12 0x21 line_height_12=14 0x22 base_12=12 0x23 adv_12=12
0x24 u8=16 0x25 line_height_16=19 0x26 base_16=16 0x27 adv_16=16
0x28..0x3F reserved = 0
```

- 位图：每字形固定 `size×size` 单元、A2（2bit 灰度，`3`=最黑），行主序；每行
  `ceil(size*2/8)` 字节（12px=3B/行，16px=4B/行），每字节最高 2 位是最左像素。
- glyph ID = 码点在升序数组中的下标；两档位图共享同一码点索引，故集合天然一致。

## 复现步骤（含两次一致证明）

```bash
# 1. 生成完整包
python tools/font-pack/generate_font_pack.py \
  --font .tmp/node-15-assets/NotoSansCJKsc-Regular.otf \
  --out  assets/fonts/xiaomiao-zh-cn.xmf

# 2. 独立校验
python tools/font-pack/verify_font_pack.py assets/fonts/xiaomiao-zh-cn.xmf

# 3. 可复现性：换输出路径重跑一次，比对 SHA-256
python tools/font-pack/generate_font_pack.py \
  --font .tmp/node-15-assets/NotoSansCJKsc-Regular.otf \
  --out .tmp/node-15-assets/rerun.xmf
sha256sum assets/fonts/xiaomiao-zh-cn.xmf .tmp/node-15-assets/rerun.xmf   # 必须一致

# 4. 损坏样本自检：完好包通过 + 翻转 CRC 必被拒 + invalid 样本以 CRC 被拒
python tools/font-pack/verify_font_pack.py --self-check \
  assets/fonts/xiaomiao-zh-cn.xmf --invalid assets/fixtures/invalid-font.xmf
```

已确认产物（详见 `assets/LICENSES/font-source.txt`）：

- `assets/fonts/xiaomiao-zh-cn.xmf`：759456 字节（<1 MiB），
  SHA-256 `51daac7d585aaf4d752ee081b6c2eb5221f714e9e9a4f49ad4375588454e8aba`，
  payload_crc32 `0x7795D482`，7445 glyph；两次运行 SHA-256 一致。
- `assets/fixtures/invalid-font.xmf`：由 `make_test_fixture.py` 合成的 6,592 字节最小
  自洽包（64 码点、非空位图、结构与度量全部合法），仅 `payload_crc32` 低字节翻转，
  校验器与固件解析器都只以 CRC 不匹配拒绝。不再从完整包拷贝派生：两份 759 KB 文件
  同放会让 1.5 MB assets SPIFFS 镜像超出可用容量。

### 生成 invalid fixture

```bash
python tools/font-pack/make_test_fixture.py   # 确定性输出；SHA-256 94fc911ae94d8386cb3c93da2bc0ad831fc77a7384bbc5d3f8ebd181f41a270f
```

## 校验器命令速查

- `verify_font_pack.py <xmf>` — 全字段校验，打印 offset/CRC/size，任一失败非零退出。
- `verify_font_pack.py <xmf> --expect-chars <ui.txt>` — 扫描 UI 文案，凡无法经 `gb2312`
  codec 编码的字符必须在码点表中，否则非零退出（15C 补扩展表后使用）。
- `verify_font_pack.py --self-check <good.xmf> [--invalid <bad.xmf>]` — 正向 + CRC 注入拒绝自检。

## 15C 待办

节点 15C 全系统中文 UI 迁移后，把任何 GB2312 外、出现在中文文案里的字符补入
`required-extra-chars.txt`，重跑生成并用 `--expect-chars` 扫描全部中文文案确保零缺字。
