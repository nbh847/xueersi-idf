# 节点 15：Assets、文件系统与 Flash 完整中文字库

## 元信息

- 对应节点：节点 15“Assets 与文件系统”
- 状态：**节点 15 完成**。15A～15D 的构建、自测、无 SD 中文显示、有 SD 对照、全 UI 回归和 merged bin 烧录均已按子 Goal 取得人工验收；CP5 字体损坏注入经负责人决定跳过，不作通过项。收口时发现的配网回归已在独立 Goal 修复并完成连续两次换网验证。
- 创建时间：2026-09-22 17:01（北京时间）
- 前置条件：节点 14“Storage Service”已完成并通过实机验收；节点 12“Audio Service”与节点 13“首个正式游戏”继续推迟，不阻塞本节点
- 依赖接口：内置 `assets` 分区（1.5 MB，data/spiffs）与 Storage Service 固定挂载点 `/sdcard`
- 人工验证分工：Agent 负责实现、资源生成检查、源码与 diff 静态复核；项目负责人负责 ESP-IDF 6.1 构建、烧录、串口监视、屏幕目视和无 SD／有 SD 实机验证

## 执行拆分与依赖顺序

本文件只维护节点 15 的总体架构、共同边界和最终完成口径，不作为一次平台 Goal 整体执行。实际施工严格按以下四个独立 Goal 顺序推进，每次只执行一个：

1. `goals/20260922-1711-assets-filesystem-foundation.md`：15A，Assets SPIFFS、逻辑路径和只读文件接口。
2. `goals/20260922-1711-flash-chinese-font-runtime.md`：15B，完整 GB2312 字体包、XMF1 运行时与字体自测；依赖 15A。
3. `goals/20260922-1711-ui-chinese-localization.md`：15C，全系统中文化和 160 × 128 布局适配；依赖 15B。
4. `goals/20260922-1711-node15-integration-validation.md`：15D，资源、字体、中文 UI、merged bin 和完整实机收口；依赖 15A～15C。

前一子 Goal 未满足自身完成标准时，不启动下一项；发现需要修改已冻结接口时，回到对应子 Goal 记录、修复并重新验收，不在后续 Goal 中静默改写。

## 目标与预期行为

建立独立的 Assets Service 和 Flash 中文字体能力，把固件自带字体、Launcher 图标及后续可复用资源打包到 1.5 MB `assets` SPIFFS 分区。完整中文字库必须随固件烧入本机 Flash，不从 SD 卡读取，也不允许 SD 卡静默覆盖；设备不插 SD 卡、SD 卡挂载失败或运行中安全卸载后，Launcher、全部业务 App、Hardware Test 15 页和设备端状态提示仍能正常显示中文。

本节点同时完成当前 UI 的简体中文化，默认使用中文。字体分区缺失、损坏或校验失败时不得阻塞启动，固件必须切回现有英文文案和 Montserrat 字体，保持 Launcher、设置、配网和硬件测试可操作。音乐播放、正式游戏、动态 App 安装、文件管理器和网络资源下载不在本节点实现。

## “完整中文字体”的确定口径

1. “完整”按项目既有容量规划定义为 **GB2312 完整字符集**：6,763 个汉字和 682 个符号／标点，共 7,445 个双字节字符；不是只收集当前 UI 文案使用字符的子集。
2. 字体生成阶段必须额外扫描仓库中本节点引入的全部中文 UI 文案；如出现 GB2312 之外的字符，必须显式加入扩展字符表并记录，不得用方框占位或静默丢弃。
3. ASCII、数字、CPU／RAM／GPU／SSID／IP 等英文和技术字段继续由现有 Montserrat 字体回退链显示，不复制进中文位图包。
4. GB18030 或 Unicode CJK 全集不在本节点承诺范围。若后续要求覆盖该范围，必须先重新核算 Flash、字体档位和位图质量，不能把本 Goal 的“完整 GB2312”表述外推为全 Unicode。

## 当前基线与证据

- 根分区表已预留 `assets, data, spiffs, 0x220000, 0x180000`，容量 1.5 MB；当前尚未挂载和写入，factory app 仍为 2 MB，Flash 末尾约 384 KB 未分配。
- 当前可用 PSRAM 以运行时 4 MiB 计算；内部 RAM 已因 Wi-Fi 与 LCD 双全屏 DMA 缓冲偏紧，字体索引和缓存不得默认占用内部 DMA RAM。
- `sdkconfig` 当前 `CONFIG_LV_MEM_SIZE_KILOBYTES=64`，LVGL 私有 heap 只有 64 KB。
- LVGL 9.5 的 `lv_binfont_create()` 会通过 `lv_malloc()` 分配并载入完整 `glyph_bitmap` 与字形描述；约 0.8～1.0 MB 的完整中文 `.bin` 字体不能直接使用该路径。
- LVGL 9.5 的 `lv_font_t` 支持自定义 `get_glyph_dsc`／`get_glyph_bitmap` 回调和递归 `fallback` 字体，可用自定义流式字体驱动接入。
- 当前 UI 显式使用 Montserrat 10／12／14 px，所有页面仍是英文；中文本地化此前未立项，本 Goal 取代“以后再做”的旧建议。
- Storage Service 已固定 `/sdcard` 并拥有全部 SD 生命周期。节点 15 只能读取其快照和挂载点，不得接触 `sdmmc_card_t`、SDSPI、FATFS 挂载 API 或 GPIO22。

## 已确认实现决策

1. **中文字库只在本机 Flash。** 生产字体路径固定为 `/assets/fonts/xiaomiao-zh-cn.xmf`，禁止回退到 `/sdcard`，也禁止使用 SD 同名文件覆盖。
2. **保留现有 1.5 MB SPIFFS 分区。** 不拆分或移动 NVS、phy_init、factory 与 assets，不占用末尾 384 KB 后备空间；只有资源镜像内容和挂载逻辑发生变化。
3. **采用自定义流式字体格式。** 不使用会整体载入字体位图的 `lv_binfont_create()`，也不引入 FreeType。字体位图保留在 Flash 文件，按 glyph 读取到小型 PSRAM 缓存。
4. **提供 12 px 和 16 px 两档完整字库。** 12 px 用于紧凑列表、提示和状态，16 px 用于标题、主要内容与按钮。两档均使用 A2（2 bit 灰度）抗锯齿格式。
5. **完整字符表只存一份索引。** 两档字体共享排序 Unicode 索引；glyph ID 为索引位置，不保存每字形可变偏移，位图采用固定单元，便于边界验证和 O(1) 偏移计算。
6. **缓存只进入 PSRAM。** Unicode 索引、字体缓存和校验工作缓冲使用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`；分配失败时整套中文字体判定为不可用并切回英文，不挤占 LCD DMA 内存。
7. **LVGL 单线程消费。** 字体回调只在 LVGL UI 任务调用，不为字体新增 Task、队列、timer 或互斥锁；Assets Service 的通用文件句柄仍保持独立、可确定释放。
8. **默认中文，失败英文降级。** 字体校验成功后才选择中文文案；挂载、格式、CRC、内存或读取失败时选择英文资源，不能让中文字符串配 Montserrat 后显示方框。
9. **中文化是节点验收内容。** Launcher、Games、PC Monitor、Tools、Settings、Hardware Test 15 页、设备端 Wi-Fi 配网状态与全局提示必须逐页中文化并重排版；串口日志与 CPU／RAM 等技术缩写保留英文。
10. **SD 资源使用显式命名空间。** 固件内置资源使用 `asset:/...`，SD 用户资源使用 `sd:/...`；SD 只能显式读取 `/sdcard/xiaomiao/`，不得覆盖字体或基础 UI 资源。
11. **资源分区只读。** `format_if_mount_failed=false`，运行时不提供对 `/assets` 的写入、删除、格式化或修复接口；资源更新只随完整固件／资源镜像烧录发生。
12. **构建不依赖字体生成工具。** 生成后的 `.xmf` 和许可证提交仓库；字体生成工具放在项目内并固定输入版本，但普通固件构建直接消费已生成文件，不联网、不临时下载字体。
13. **字体来源必须可再分发。** 首选思源黑体／Noto Sans CJK SC；实施时必须固定具体版本、上游地址、许可证和源文件 SHA-256，并在首次生成前完成许可核对。
14. **资源缺失安全降级。** 图标缺失回退现有 LVGL Symbol／占位条；字体不可用回退英文；SD 不可用只让显式 `sd:/` 请求失败，三类故障均不得阻塞 Launcher。

## 字体包格式与容量预算

字体包使用项目私有、版本化、只读格式 `XMF1`。所有整数使用固定宽度小端编码；保留字段生成时清零，解析时校验边界。

```text
Header
  magic[4]             = "XMF1"
  schema_version       = 1
  header_size
  glyph_count          >= 7445
  codepoint_offset
  bitmap_12_offset
  bitmap_16_offset
  file_size
  payload_crc32
  reserved[...]        = 0

Codepoint index
  uint16_t codepoints[glyph_count]  // 严格递增，glyph ID 等于数组下标

12 px bitmap block
  glyph_count × 36 bytes            // 12 × 12 × 2 bit

16 px bitmap block
  glyph_count × 64 bytes            // 16 × 16 × 2 bit
```

GB2312 7,445 个字符的未压缩位图上限计算：

- 12 px：`7445 × 12 × 12 × 2 / 8 = 268020` 字节。
- 16 px：`7445 × 16 × 16 × 2 / 8 = 476480` 字节。
- 两档位图合计：`744500` 字节；再加约 15 KB Unicode 索引、Header 与扩展字符，目标字体包必须控制在 1 MiB 以内。
- 完整 SPIFFS 镜像必须小于 1.5 MB 分区并保留文件系统元数据空间；生成阶段不得依赖“理论刚好装下”。

解析器必须检查 magic、schema、文件实际长度、所有 offset 的单调性与对齐、`glyph_count`、码点严格递增、乘法／加法溢出、位图块完整性和 payload CRC32。任一校验失败即关闭文件并进入英文降级，不允许使用部分字库。

## 字体运行时与公开接口

建议新增：

```text
main/services/xiaomiao_assets_service.h
main/services/xiaomiao_assets_service.c
main/services/xiaomiao_font_service.h
main/services/xiaomiao_font_service.c
main/framework/xiaomiao_fonts.h
main/framework/xiaomiao_i18n.h
main/framework/xiaomiao_i18n.c
```

建议公开接口：

```c
esp_err_t xiaomiao_assets_service_init(void);
void xiaomiao_assets_get_snapshot(xiaomiao_assets_snapshot_t *snapshot);

esp_err_t xiaomiao_font_service_init(void);
bool xiaomiao_font_service_ready(void);
const lv_font_t *xiaomiao_font_zh_12(void);
const lv_font_t *xiaomiao_font_zh_16(void);

const lv_font_t *xiaomiao_font_small(void);
const lv_font_t *xiaomiao_font_body(void);
const lv_font_t *xiaomiao_font_title(void);
const char *xiaomiao_text(xiaomiao_text_id_t id);
```

`xiaomiao_font_zh_12()` 和 `xiaomiao_font_zh_16()` 在中文字体不可用时不得返回悬空对象；字体令牌函数负责返回相应 Montserrat 回退。中文字体对象分别把 `fallback` 指向相近字号的 Montserrat，保证混排的 ASCII、数字和 LVGL Symbol 可用。

字体服务启动时把排序码点表复制到 PSRAM。`get_glyph_dsc` 通过二分查找定位 glyph ID；`get_glyph_bitmap` 先查 PSRAM LRU，未命中时按固定偏移从 Flash 读取 36／64 字节。建议缓存 128 个 glyph，两档位图与元数据合计控制在约 16 KB，索引约 15 KB；实际占用必须通过运行时日志测量，不以估算代替验收。

## Assets 与文件系统接口边界

Assets Service 挂载 `/assets` 并提供只读资源状态、路径解析和通用读取能力。业务 App 不直接调用 `esp_vfs_spiffs_register()`、不直接拼接 `/assets` 或 `/sdcard` 物理路径，也不取得 Storage Service 内部对象。

逻辑路径规则：

```text
asset:/fonts/xiaomiao-zh-cn.xmf  -> /assets/fonts/xiaomiao-zh-cn.xmf
asset:/icons/tools.bin           -> /assets/icons/tools.bin
sd:/music/example.raw            -> /sdcard/xiaomiao/music/example.raw
```

路径解析必须拒绝空路径、绝对路径、`..`、反斜杠、盘符、重复根前缀、NUL 截断、超长路径和越出 `/sdcard/xiaomiao/` 的请求。`sd:/` 打开前读取 Storage Service 最新快照；未挂载时返回确定性错误，不触发自动挂载。首版通用资源 API 只提供 open／read／seek／size／close 和有界目录遍历，不提供写入。

## 构建期资源生成

建议目录：

```text
assets/
  fonts/
    xiaomiao-zh-cn.xmf
  icons/
    games.bin
    pc-monitor.bin
    tools.bin
    settings.bin
    hardware-test.bin
  fixtures/
    invalid-font.xmf
  LICENSES/
    noto-sans-cjk-ofl.txt
    font-source.txt

tools/font-pack/
  README.md
  requirements.txt
  generate_font_pack.py
  verify_font_pack.py
  required-extra-chars.txt
```

字体生成脚本只用于开发／更新资源。若使用 Python，执行前按项目规则确认 `.gitignore` 已排除 `.venv/`，在 `tools/font-pack/.venv/` 或 Goal 专用 `.tmp/` 环境中安装固定依赖；不得安装全局依赖。生成产物进入 `assets/fonts/`，临时文件进入 `.tmp/node-15-assets/`。

构建系统使用 ESP-IDF 的 SPIFFS 镜像生成能力并加入正常 flash 参数；普通 `idf.py flash` 必须同时写入 `assets`。发布 merged bin 时必须依据实际 flash args 合并资源镜像，不能只合并 bootloader、partition table 和 factory app。

`.gitignore` 当前全局忽略 `*.bin`；实施时只能为确定的 `assets/**/*.bin` 源资源增加精确例外，不能放开所有构建 `.bin`。

## 中文文案与布局

新增固定 `zh-CN` 文案表和英文降级表。默认语言不增加 Settings 开关：字体服务 READY 时选择中文，否则选择英文。格式化文本采用文案 ID + 明确参数，不把任意格式字符串暴露给业务输入。

必须覆盖：

- Launcher 标题、页码、操作提示和五个 App 名称。
- Games 占位页。
- PC Monitor 两页名称、状态和温度标签；CPU／RAM／GPU 缩写保持英文。
- Tools 菜单、Wi-Fi、System Info、About，以及新增的 Assets 诊断页。
- Settings 菜单、Wi-Fi 配网／自动连接／忘记网络、Display、Sound、System。
- Hardware Test 15 页的标题、值说明、操作提示和错误状态。
- 设备端 Wi-Fi 配网状态；手机网页可同步中文化，但其字体由手机浏览器提供，不计入设备字库验收。

中文 12／16 px 会改变现有几何。每页必须逐项检查宽度、换行、截断、右上角 Wi-Fi 图标、Hardware Test 页码和焦点卡片，不允许仅替换字符串后宣称完成。动态 SSID、IP、卡名和版本号必须设置确定的长文本截断或滚动策略。

## 启动链

```text
Settings / NVS
  -> Assets Service 挂载 /assets（失败只记错误）
  -> Storage Service 尝试挂载 /sdcard（与字体无关）
  -> LVGL 初始化
  -> Font Service 校验并打开 Flash 字库
  -> 选择中文或英文降级文案
  -> 注册 App 并进入 Launcher
```

字体服务必须在任何中文 LVGL 对象创建前完成初始化。SD 卡缺失、SD 初始化失败或 `/sdcard` 卸载不得改变字体服务状态。

## 范围边界

### 本节点必须完成

- 挂载并只读管理 1.5 MB `assets` SPIFFS 分区。
- 生成、提交并随固件烧录完整 GB2312 12／16 px A2 字体包。
- 实现 XMF1 校验、Unicode 查找、Flash 流式读取和 PSRAM glyph 缓存。
- 建立统一字体令牌与中／英文文案选择。
- 完成现有 Launcher、四个业务 App、Hardware Test 15 页和设备状态提示的中文化与布局复核。
- 提供内置图标加载与现有 LVGL Symbol／占位条降级。
- 提供只读 `asset:/` 与显式 `sd:/` 资源接口及路径安全检查。
- 增加 `Tools → Assets` 诊断页，显示内置资源、字体和 SD 状态，但不得显示任意文件内容或敏感数据。
- 提供字体包主机验证脚本和固件自测入口。
- 同步 README、项目概览、ROADMAP、Goal 历史与受影响注释。

### 本节点禁止实现或修改

- 不从 SD 卡加载生产中文字库，不允许 SD 覆盖内置字体。
- 不实现 Audio Service、音乐播放、正式游戏、ROM 加载或动态 App 安装。
- 不实现资源下载、在线更新、OTA、文件管理器或 Assets 运行时写入。
- 不重新实现 SD 挂载、卸载、SDSPI、FATFS、共享 SPI2 或 GPIO22 清理。
- 不自动格式化、修复或擦除 SPIFFS／SD 卡。
- 不修改 NVS 与 phy_init 的大小和偏移，不缩小 2 MB factory app，不占用末尾后备空间。
- 不引入 FreeType，不把完整字体复制到 LVGL 64 KB heap，不为字体创建后台任务。
- 不顺手建立 BSP 分层、重构全部 App 框架或改动无关外设逻辑。

## 检查点、验收方式与预期结果

### CP1：字体来源、字符集与生成工具

实施内容：

- 固定开放字体来源、版本、许可证和 SHA-256。
- 实现 XMF1 生成与验证脚本。
- 枚举完整 GB2312 7,445 字符和扩展字符。
- 生成 12／16 px A2 字体并提交许可证。

验证方式：

- 主机脚本分别统计汉字、符号、扩展字符和总 glyph 数。
- 对所有码点验证非空 glyph、固定长度、排序唯一性和两档集合一致。
- 重复生成两次比较 SHA-256。
- 扫描全部中文 UI 文案，缺字时脚本非零退出。

预期结果：字体包可复现、完整、许可清晰且不超过 1 MiB；无当前文案缺字。

### CP2：Assets Service 与 SPIFFS 镜像

实施内容：

- 挂载 `/assets`，接入构建和 flash 参数。
- 实现状态快照、逻辑路径解析、只读文件／目录 API。
- 接入 Storage Service 快照，提供显式 `sd:/` 读取。

验证方式：

- 静态检查只有 Assets Service 调用 SPIFFS 注册接口。
- 检查业务 App 不直接出现 `/assets`、`/sdcard`、SPIFFS、FATFS 或 SDSPI 调用。
- 用主机／固件自测覆盖合法路径、`..`、绝对路径、反斜杠、超长路径和无 SD 状态。
- 检查生成镜像大小和 flash args 中 `assets` 的偏移 `0x220000`。

预期结果：内置资源可读，非法路径被拒绝，SD 缺失不影响内置资源和启动。

### CP3：流式字体驱动

实施内容：

- 实现 Header／offset／CRC 校验、PSRAM 索引、二分查找和 128 glyph LRU。
- 创建 12／16 px `lv_font_t`，设置 Montserrat fallback。
- 增加字体服务快照和确定性错误日志。

验证方式：

- 固件自测遍历完整字符表，对每个码点调用 glyph 描述与位图读取。
- 使用 `invalid-font.xmf` 覆盖 magic、schema、短文件、越界 offset、乱序码点、CRC 错误。
- 记录内部 heap、PSRAM、LVGL heap 在初始化前后的变化。
- 首次渲染与缓存命中后分别测量，不预设性能提升结论。

预期结果：7,445 个 GB2312 字符全部可查询和读取；异常字体完整回退英文；索引与缓存来自 PSRAM，内部 DMA RAM 无异常下降。

### CP4：中文文案与逐页布局

实施内容：

- 建立中／英文文案表和统一字体令牌。
- 中文化 Launcher、Games、PC Monitor、Tools、Settings、Hardware Test 15 页及设备状态提示。
- 增加 Tools Assets 诊断页并重排 160 × 128 布局。

验证方式：

- 静态扫描所有用户可见英文，区分应翻译文案与保留技术缩写。
- 逐页检查标题、正文、提示、动态值、焦点、Wi-Fi 图标和页码无重叠。
- 检查字体不可用时所有中文文案都切回英文，而不是显示方框。

预期结果：默认中文界面完整可用，混合英文缩写与动态值显示正确，英文降级保持可操作。

### CP5：自动化与失败路径

增加与既有自测互斥的 `XIAOMIAO_ASSETS_SERVICE_SELF_TEST`／`XIAOMIAO_FONT_SERVICE_SELF_TEST`，至少覆盖：

1. 正常镜像、字体 Header 和 CRC。
2. 完整字符数量与首／末／随机码点查找。
3. 两档 glyph 固定偏移和越界保护。
4. 缓存命中、淘汰和重复读取一致性。
5. 非法路径集合。
6. 无 SD 时 `asset:/` 可用、`sd:/` 确定失败。
7. 模拟字体不可用时英文回退。
8. 重复 init 的幂等行为和文件句柄释放。

预期日志：

```text
ASSETS_SERVICE_SELF_TEST: PASS
FONT_SERVICE_SELF_TEST: PASS
```

### CP6：Agent 静态复核

Agent 必须执行：

- 源码引用边界检查、字体格式边界审查、整数溢出检查。
- CMake、分区表、资源镜像、sdkconfig defaults 和 `.gitignore` 一致性检查。
- 中文文案字符覆盖扫描。
- 文件句柄、PSRAM、LVGL 对象和字体缓存释放路径检查。
- `git diff --check`、变更范围核对和无关改动检查；若本机 Git 仍不可用，必须如实记录未执行并由人工补验。

Agent 不运行 `idf.py build`、`set-target`、flash、monitor、esptool 或串口操作。

### CP7：人工构建、烧录与实机验收

由项目负责人在 ESP-IDF 6.1 环境执行：

```powershell
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

必须验证：

1. 构建输出包含 factory app 和 `assets` SPIFFS 镜像，资源镜像未超过 1.5 MB。
2. merged bin 包含 `0x220000` 的 Assets 内容，并能从 `0x0` 单文件刷入后正常启动。
3. **完全不插 SD 卡冷启动**，Launcher 和全部页面正常显示中文。
4. SD 初始化失败时字体服务仍为 READY，中文不受影响。
5. 插入、挂载并安全卸载 SD 后，中文和内置图标始终正常。
6. Launcher 五入口、分页、焦点和返回行为无回归。
7. Games、PC Monitor、Tools、Settings 全部页面逐项检查。
8. Hardware Test 15 页逐页往返，中文标题、提示、状态和值不重叠。
9. 显示一级汉字、二级汉字、中文标点及字符表边界样本。
10. 连续进入／退出 Tools Assets 页面 20 次，文件句柄、LVGL 对象、LVGL heap 和 PSRAM 无持续下降。
11. 自测固件输出两个 `PASS`，普通固件恢复后仍能启动。
12. 字体失败注入时进入英文界面而非崩溃、重启或方框中文。

人工结果必须提供构建摘要、资源镜像大小、关键启动日志、内存数值、无 SD 中文页面照片或明确的逐项确认。缺少证据时对应项目保持“未验证”。

## 完成标准

以下条件全部满足后才能把节点 15 移入“最近完成”：

- CP1～CP6 实施和静态复核完成。
- CP7 的 ESP-IDF 6.1 构建、烧录、无 SD 中文 UI、SD 独立性、自测和既有功能回归取得人工证据。
- 完整 GB2312 7,445 字符和全部 UI 文案字符通过生成期检查与固件遍历自测。
- 字体包、SPIFFS 镜像、factory app 均未超过各自分区。
- 无 SD 卡时系统全程显示中文，SD 状态变化不改变字体服务状态。
- 字体损坏时英文降级可操作，不阻塞 Launcher。
- README、项目概览、ROADMAP、Goal 历史和资源许可证已同步。

## 当前未验证范围

- 节点 15 的必需 CP1～CP7 已闭环，验收证据及口径见 `goals/20260922-1711-node15-integration-validation.md`。
- CP5 字体损坏注入后的英文界面实机观察经负责人决定跳过，不记为通过；损坏样本的 CRC 拒收和字体失败回退路径已有自测与静态检查证据。
- `assets/icons/` 仍为占位，实机走已接线的 Symbol／占位条回退链；没有声称已验证独立的图标资源加载。

## 实施记录

- 2026-09-25 22:25 -- 提交前按 15D 的人工验收记录更新总纲当前状态：15A～15D 已完成，节点 15 的构建、自测、无 SD／有 SD、全 UI 与 merged bin 验收均闭环；字体损坏注入的实机观察按负责人既定决定跳过。收口轮发现的配网回归已在独立 Goal 修复并通过连续两次换网验证。
- 2026-09-22 17:01 -- 项目负责人确认方案：中文字库必须位于本机 Flash，设备无 SD 卡时仍正常显示中文；完整中文字体在节点 15 本版本实施，不推迟。创建本 Goal，固定完整 GB2312、12／16 px A2、SPIFFS 流式读取、PSRAM 缓存、默认中文与英文故障降级方案。状态：待实施。
- 2026-09-22 17:11 -- 修正施工拆分：本文件保留为节点 15 总纲，新增 15A Assets 基础、15B Flash 字体、15C 中文 UI、15D 集成验收四个独立 Goal；后续按依赖顺序每次只执行一个。状态：总纲已确认，四个子 Goal 均待实施。
- 2026-09-22 17:45 -- 15A 完成 CP1～CP3 编码与 Agent 静态检查（Assets Service、路径解析、只读文件 API、自测、SPIFFS 镜像构建接入），未构建未烧录，CP4 待人工验证。细则见 `goals/20260922-1711-assets-filesystem-foundation.md`。状态：15A 已实施待人工验证；15B 开始实施。
- 2026-09-22 17:55 -- 15B 完成 CP1～CP3：Noto Sans CJK SC v2.004（OFL-1.1，源 SHA-256 固定）经 `tools/font-pack/` 生成 7,445 glyph、759,456 字节 `<1 MiB` 的 `assets/fonts/xiaomiao-zh-cn.xmf`（两次生成 SHA-256 一致，Agent 独立复核 Header／offset／递增／CRC 通过）；`invalid-font.xmf` 仅 CRC 字节损坏；C 侧 XMF1 校验、PSRAM 索引与 128 glyph LRU、12／16 px `lv_font_t` 与 Montserrat fallback、字体令牌和 `XIAOMIAO_FONT_SERVICE_SELF_TEST` 全部接入。已知偏差：U+3000 与 U+FF3F 为登记在案的空白 glyph（Zs 类／16px 窗口裁剪）。未构建未烧录，CP4 待人工验证。细则见 `goals/20260922-1711-flash-chinese-font-runtime.md`。状态：15B 已实施待人工验证；15C 开始实施。

- 2026-09-22 19:30 -- 15C 与 15D 完成 Agent 侧全部工作：i18n（203 文案 ID、中英双表、默认中文、字库失败整表回退英文）、字体令牌与内置图标三级回退全面接入，Launcher／四个业务 App（含新 Tools Assets 诊断页）／Settings／Hardware Test 15 页中文化与 160 × 128 布局适配；15D 的 CP1 静态部分与 CP2 全量静态复核通过（详见子 Goal）。CP3～CP7 全部等待项目负责人人工构建、烧录与实机验收，节点 15 保持未勾选。状态：15A～15C 已实施、15D Agent 侧完成，统一待人工验证。

## 交付结果

- 15A～15D 已交付 Assets Service、SPIFFS 镜像、完整 GB2312 XMF1 字体包、PSRAM 整包驻留字体运行时、全系统中文 UI 与布局适配。构建、自测 `PASS`、无 SD／有 SD 中文显示、26 组 App 开合、全 UI 目视回归和 merged bin 从 `0x0` 烧录均已获人工证据；细则见四份子 Goal。
- 节点 15 已按豁免口径完成。配网 `0xb008` 和第二会话 DHCP 回归属于独立修复，记录在 `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`。
