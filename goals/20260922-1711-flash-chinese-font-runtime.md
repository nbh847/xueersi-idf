# 节点 15B：Flash 完整中文字库与字体运行时

## 元信息

- 父施工总纲：`goals/20260922-1701-assets-filesystem-chinese-font.md`
- 状态：已实施（CP1～CP3 实机 `PASS`，构建为 PSRAM 整包驻留版；CP4 中文显示与可读性目视通过），余 20 次开合完整样本与损坏降级启动观察（归 15D CP4／CP5）
- 创建时间：2026-09-22 17:11（北京时间）
- 前置 Goal：`goals/20260922-1711-assets-filesystem-foundation.md`
- 后续依赖：节点 15C 中文 UI 迁移必须在本 Goal 字体接口和实机可读性确认后开始

## 目标与预期行为

生成并提交本机 Flash 内置的完整 GB2312 中文字体包，实现自定义 XMF1 字体解析、PSRAM 索引与 glyph 缓存、LVGL 12／16 px 字体对象和 Montserrat 回退。设备完全不插 SD 卡时，字体自测和中文样例页必须正常；SD 状态变化不得改变字体服务状态。

本 Goal 不负责把全部生产 UI 翻译成中文，只交付可供节点 15C 使用且已经独立验证的字体基础。

## 必要背景与接口依赖

- Assets Service 必须已稳定提供只读 `asset:/fonts/xiaomiao-zh-cn.xmf` 访问。
- 当前 LVGL 私有 heap 为 64 KB，禁止使用会整体载入约 1 MB glyph bitmap 的 `lv_binfont_create()`。
- 当前运行时可用 PSRAM 按 4 MiB 计算；索引、LRU 和校验缓冲必须显式分配到 PSRAM。
- `lv_font_t` 支持自定义 glyph 回调和 `fallback` 链。

## 字符集、格式与容量

1. 必须覆盖 GB2312 的 6,763 个汉字与 682 个符号／标点，共 7,445 字符。
2. 生成工具扫描节点 15 的中文文案；GB2312 之外的必需字符进入显式扩展表。
3. 字体包提供 12 px 和 16 px 两档 A2 位图，共享排序后的 `uint16_t` Unicode 索引。
4. XMF1 使用固定宽度小端 Header，包含 magic、schema、glyph count、各块 offset、file size、payload CRC32 和清零保留字段。
5. 固定单字形长度：12 px 为 36 字节，16 px 为 64 字节；glyph ID 等于索引位置。
6. GB2312 两档位图基线为 744,500 字节，完整字体包必须小于 1 MiB。
7. 完整 SPIFFS 镜像仍必须满足节点 15A 的 1.5 MB 分区限制。

## 字体来源和可复现生成

首选思源黑体／Noto Sans CJK SC。实施前必须固定：

- 精确字体版本和上游地址。
- 可再分发许可证。
- 源文件 SHA-256。
- 生成工具和依赖版本。
- A2 栅格化参数、字符排序和舍入规则。

建议新增：

```text
assets/fonts/xiaomiao-zh-cn.xmf
assets/fixtures/invalid-font.xmf
assets/LICENSES/
tools/font-pack/README.md
tools/font-pack/requirements.txt
tools/font-pack/generate_font_pack.py
tools/font-pack/verify_font_pack.py
tools/font-pack/required-extra-chars.txt
main/services/xiaomiao_font_service.h
main/services/xiaomiao_font_service.c
main/framework/xiaomiao_fonts.h
```

普通固件构建只消费已提交的 `.xmf`，不联网、不下载字体、不运行生成工具。生成工具所需第三方依赖不得全局安装。

## 公开接口与运行时契约

```c
esp_err_t xiaomiao_font_service_init(void);
bool xiaomiao_font_service_ready(void);
void xiaomiao_font_service_get_snapshot(xiaomiao_font_snapshot_t *snapshot);

const lv_font_t *xiaomiao_font_zh_12(void);
const lv_font_t *xiaomiao_font_zh_16(void);
const lv_font_t *xiaomiao_font_small(void);
const lv_font_t *xiaomiao_font_body(void);
const lv_font_t *xiaomiao_font_title(void);
```

运行时决策：

- Header、长度、offset、排序、溢出和 CRC 任一失败，整套中文字体不可用，不接受部分加载。
- Unicode 索引和 128 glyph LRU 使用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`。
- `get_glyph_dsc` 二分查找；`get_glyph_bitmap` 缓存未命中时经 Assets Service 定位并读取固定字节数。
- 字体回调只在 LVGL UI 任务调用，不新增锁、Task、队列或 timer。
- 12／16 px 字体分别回退到相近字号 Montserrat，混排 ASCII、数字和 LVGL Symbol。
- 接口在初始化失败时返回稳定的 Montserrat 字体，不返回 NULL 或悬空对象。
- 生产字体路径固定为 `asset:/fonts/xiaomiao-zh-cn.xmf`，代码中不得出现字体 `sd:/` 路径。

## 允许修改范围

- 字体生成／验证工具、字体包、许可证和 fixture。
- `main/services/xiaomiao_font_service.{h,c}`、`main/framework/xiaomiao_fonts.h`。
- 为接入字体服务所需的最小 `main/CMakeLists.txt`、`main/main.c`、自测入口和配置改动。
- Tools Assets 自测视图或专用自测页面，仅用于验证字体，不迁移全部生产文案。
- 本 Goal、父 Goal、ROADMAP 和必要架构文档。

## 禁止修改范围

- 不翻译全部 Launcher／App／Hardware Test 文案；该工作属于 15C。
- 不从 SD 加载字体，不允许 SD 覆盖字体。
- 不使用 FreeType、`lv_binfont_create()` 或完整字体 RAM 复制。
- 不修改分区布局、Storage Service 生命周期、Wi-Fi、Agent、硬件外设或导航语义。
- 不实现字体下载、动态安装、任意 TTF 加载或多语言设置页面。

## 检查点与验收

### CP1：生成工具与字体包

- 枚举完整 GB2312 和扩展字符。
- 两次生成结果 SHA-256 一致。
- 验证字符唯一、严格递增、glyph 非空、两档集合一致、CRC 正确。
- 扫描中文文案缺字时非零退出。

### CP2：解析器与缓存

- 覆盖正常 Header、magic、schema、短文件、乱序码点、越界 offset、整数溢出、CRC 错误。
- 覆盖 glyph 首项、末项、一级字、二级字、符号、缺失字符和 Montserrat fallback。
- 覆盖缓存命中、淘汰、重复读取一致性和 PSRAM 分配失败。

### CP3：固件自测

新增 `XIAOMIAO_FONT_SERVICE_SELF_TEST`，与其他自测互斥：

1. 遍历全部 7,445 个 GB2312 码点。
2. 两档字体均取得有效 glyph 描述和正确长度位图。
3. 随机重复读取结果一致。
4. 无 SD 状态下字体仍 READY。
5. 损坏 fixture 触发英文回退。

成功输出：

```text
FONT_SERVICE_SELF_TEST: PASS
```

### CP4：人工字体验收

项目负责人完成 ESP-IDF 6.1 构建、烧录和实机检查：

- 不插 SD 冷启动，中文样例页正常。
- 目视检查 12／16 px 常用字、一级字、二级字、标点和边界样本。
- 记录字体文件、Assets 镜像、factory app 大小。
- 记录字体初始化耗时、内部 heap、LVGL heap、PSRAM 前后值。
- 重复打开／关闭样例页 20 次，无句柄、对象或内存持续下降。
- 自测输出 `PASS`，普通固件恢复后正常。

具体人工命令（Windows，先加载 IDF 6.1 环境）：

```bash
# 字体自测固件（改配置后首次构建会自动重新生成 sdkconfig）
idf.py -DXIAOMIAO_FONT_SERVICE_SELF_TEST=ON build
idf.py -p COM5 flash && idf.py -p COM5 monitor     # 预期 FONT_SERVICE_SELF_TEST: PASS
# 恢复普通固件
idf.py -DXIAOMIAO_FONT_SERVICE_SELF_TEST=OFF build
idf.py -p COM5 flash && idf.py -p COM5 monitor     # 预期进入 Launcher，日志含 Font Service READY 与 init_time_ms
```

自测选项经 `main/CMakeLists.txt` 的 `option()` 控制，与其他自测互斥（同一次构建只开一个）。注意：这些开关是 CMake 缓存变量，`-D` 赋值持久生效、不会在下次构建自动失效；切换自测目标时必须把上一项显式置 `OFF`，否则互斥链按声明顺序取先开的分支且触发零重编。常规固件验收前须把全部六个 `XIAOMIAO_*_SELF_TEST` 显式置 `OFF`。另外构建输出须确认 `assets` 镜像生成成功且 < 1.5 MB（本次已把 fixture 缩为 6.5 KB，`assets/` 负载 774,340 字节）。

Agent 不主动执行固件构建、烧录或串口操作。

## 完成标准

- 完整 GB2312 与全部扩展字符通过主机验证和固件遍历。
- 字体包小于 1 MiB，完整 Assets 镜像小于 1.5 MB。
- 无 SD 实机中文样例、PSRAM 分配和英文降级均取得证据。
- 字体接口冻结，15C 无需修改字体包格式或绕过 Font Service。
- 实施结果同步本 Goal、父 Goal、ROADMAP 和历史索引。

## 实施记录

- 2026-09-22 17:11 -- 从节点 15 总纲拆分本独立 Goal。状态：等待 15A。
- 2026-09-22 17:53 -- 完成 CP1～CP3 实施与静态检查。**字体来源固定**：Noto Sans CJK SC Regular v2.004，SIL OFL-1.1，源文件 SHA-256 `2c76254f…365b74b`（GitHub API 速率限制导致无法取精确 commit，改用下载 URL + 源 SHA-256 作权威指纹，已记录于 `assets/LICENSES/font-source.txt`）。**生成工具**：`tools/font-pack/`（Pillow 11.3.0 精确 pin，仅装 `tools/font-pack/.venv/`，已确认被 `.gitignore` 覆盖）。**字体包**：`assets/fonts/xiaomiao-zh-cn.xmf` 共 7,445 glyph（GB2312 全集，扩展 0），759,456 字节 < 1 MiB，payload CRC32 `0x7795D482`，两次生成 SHA-256 一致（`51daac7d…454e8aba`）；Agent 独立复核 Header 逐字段、码点严格递增、offset 公式与 CRC 均通过。**fixture**：`assets/fixtures/invalid-font.xmf` 与正式包仅差 0x1C 一字节（CRC 翻转），只以 CRC 不匹配被拒。**C 运行时**：新增 `main/services/xiaomiao_font_service.{h,c}`（XMF1 全量校验→PSRAM 索引→128 glyph／档 PSRAM LRU→12／16 px `lv_font_t` 回调，Montserrat 12／14 fallback，`get_glyph_dsc` 二分查找，产物读取全部经 Assets Service，无 `sd:/` 字体路径）、`main/framework/xiaomiao_fonts.h` 字体令牌、`xiaomiao_font_service_selftest.{h,c}`（全表遍历／随机重复读一致性／墨迹与确定性采样／fallback 语义／fixture 拒收），`main/CMakeLists.txt` 与 `main/main.c` 启动链接入（Font Service 在 LVGL 图标创建前初始化、失败仅告警）。**已知偏差（如实记录）**：GB2312 全集中 U+3000（全角空格）与 U+FF3F ＿ 两码点为空白 glyph（前者为 Zs 空白类，后者 16px 墨迹落在固定 16×16 窗口外被裁剪），生成器白名单据此实现为 Unicode category Zs + 显式登记 U+FF3F，其余 7,443 字形严格非空。未运行 `idf.py build`、烧录或串口监视；CP2／CP3 的运行时行为与 CP4 全部场景待人工验证。**补充配置**：`sdkconfig.defaults` 增加 `CONFIG_SPIFFS_OBJ_NAME_LEN=48`（IDF 6.1 `components/spiffs/Kconfig` 确认符号名为 `SPIFFS_OBJ_NAME_LEN`）——生产字体路径含挂载点共 32 字符，处于 SPIFFS 默认 32 字节名字上限边界，上调以消除嵌套路径打开风险（本地 `sdkconfig` 需重新生成后生效）。
- 2026-09-22 18:02 -- 收口前容量复核发现阻塞：旧 `invalid-font.xmf` 是完整包的 759 KB 一字节 CRC 翻版，与正式包同放使 `assets/` 负载达 1,526,763 字节，逼近／超出 1.5 MB SPIFFS 分区可用容量，正常 `idf.py build` 的镜像生成大概率失败。修正：fixture 改为 `tools/font-pack/make_test_fixture.py` 合成的 6,592 字节最小自洽包（64 码点、非空位图、结构/度量合法，仅 `payload_crc32` 低字节翻转，SHA-256 `94fc911a…41a270f`，确定性可复现）；配套把 `font_validate()` 的检查顺序调整为“结构一致性 → 码点递增 → CRC → 完整字库策略（数量≥7445／字号／度量，策略失败返回 `ESP_ERR_NOT_SUPPORTED`）”，保证损坏样本确定性以 `ESP_ERR_INVALID_CRC` 被拒、与字体自测断言一致。主机侧 `verify --self-check` 重新通过（完好包通过、内存翻转与磁盘样本均仅以 CRC 被拒）。assets 负载降为 774,340 字节，镜像容量余量恢复。状态：已实施，待人工验证。
- 2026-09-23 -- CP4 字体自测实机首跑（人工日志，Agent 定位）：先遇构建切换陷阱——上一项 `XIAOMIAO_ASSETS_SERVICE_SELF_TEST=ON` 残留在 CMake 缓存，两个开关同开时互斥链按声明顺序仍取 Assets 分支，表现为零重编、esptool `No changed sectors`、日志仍是 Assets 自测；显式置 Assets 为 `OFF` 后 Font 分支真正执行。步骤 1（Assets 前置）与步骤 2（字体 init＋heap delta）通过；步骤 3 全表遍历连续占用 main 任务约 10 秒不让出 CPU，触发 `task_wdt` 告警两次（IDLE0 饿死；backtrace 落在 `xmf_get_glyph_bitmap → xiaomiao_asset_seek → SPIFFS_lseek → esp_flash_read`，是渲染热路径而非死锁）。看门狗配置为仅告警不复位，测试逻辑本身无 FAIL。修复：`xiaomiao_font_service_selftest.c` 遍历循环每 32 个码点 `vTaskDelay(1)`（约 233 次让步、总增 <1 秒）；生产路径不受影响（LVGL 每帧渲染毫秒级、帧间空闲）。人工验证命令段补充缓存开关显式关闭要求。状态：待重跑确认 `FONT_SERVICE_SELF_TEST: PASS` 且无 WDT 告警。
- 2026-09-23 -- CP4 字体自测完整运行（ELF `6418ab4b8`，无 SD，含每 32 码点让步的构建，全程无 task_wdt 告警，让步修复确认生效）：步骤 1 Assets 前置 READY（挂载约 91 ms）；步骤 2 **`font READY: 7445 glyphs, 759456 bytes, init 569 ms`**，heap delta `internal=-316 SPIRAM=-33548 internalDMA=-316`（内部 RAM 仅占 316 字节，索引/LRU 全在 PSRAM，4 MB 映射容量充足）；步骤 3 **`walk done: 7445 codepoints, 0 failures`**——全表两档渲染零失败，但耗时 262.6 秒（≈35 ms/码点、17 ms/字模；根因是每字模一次 SPIFFS 随机 seek 触发对象查找区全表扫描）。此吞吐对自测可接受；对生产首屏冷渲染不可接受（LRU 每档 128 字模，密集中文页首次渲染预计数百毫秒级卡顿），性能整改（候选：init 时把已通过 CRC 的字体位图区整体驻留 PSRAM，读取退化为 memcpy，PSRAM 预算 +760 KB）待与项目负责人确认后另记决策并重验。步骤 4 墨迹采样 200/200、50 次重复读字节一致、cache 计数命中/未命中均非零；步骤 5 fallback 语义（ASCII 中文字体拒收、经链回落 `lv_font_montserrat_12`）通过；步骤 6 损坏 fixture 以 `ESP_ERR_INVALID_CRC` 被拒、正式包 probe OK、缺失路径 `ESP_ERR_NOT_FOUND`，probe 不改服务状态。**`FONT_SERVICE_SELF_TEST: PASS`（265494 ms 处）**——流式版 CP3 全部断言实机通过，测试后停机等待复位属预期。
- 2026-09-23 -- 性能整改决策（项目负责人批准「整包驻留 PSRAM」）：流式版 17 ms/字模的冷读取吞吐不满足生产首屏渲染，根因是 SPIFFS 每次跨 span 随机 seek 触发对象查找区全表扫描。`xiaomiao_font_service.c` 重写为整包驻留：init 时经 Assets Service 一次性顺序读入 759,456 字节到 PSRAM image（与 CRC 扫描同批字节，耗时仍约 0.5 秒级），在内存内完成全部校验（结构→码点递增→CRC→完整字库策略，顺序与 fixture 拒收语义不变），成功后关闭 asset 文件句柄（image 自洽，不再持有 `xiaomiao_asset_file_t`）；`get_glyph_bitmap` 退化为 `image + offset + gid*unit` 指针运算 + A2→A8 展开，LRU／`glyph_slot_t`／`font_read_at`／scratch 全部删除。snapshot ABI 保留：`cache_hits` 语义改为常驻位图取用次数，`cache_misses` 恒为 0（字段不删以守接口冻结）；自测步骤 4 相应改断言 `cache_misses == 0`、日志行改为 `fetches:`。probe 临时整读后释放，上限 `FONT_MAX_FILE_BYTES=0x180000`。PSRAM 预算 +760 KB（4 MB 映射池，实测余量充足）。预期：步骤 3 遍历从 262.6 秒降到秒级，heap delta SPIRAM ≈ -793 KB，init 日志含 `resident` 字样。状态：已实施＋静态检查（三文件非 ASCII 0、括号配平），待人工重跑字体自测确认 `PASS`，随后进入常规固件 CP4。
- 2026-09-24 -- CP4 驻留版字体自测实机 `PASS`（ELF `81482b8ac`，无 SD，全程无 task_wdt）：`font READY: 7445 glyphs, 759456 bytes resident, init 594 ms`（流式版 569 ms，同量级）；heap delta **`internal=0 SPIRAM=-785160 internalDMA=0`**——内部 RAM／DMA 成本归零（init 后不再持有 SPIFFS 句柄），SPIRAM 增量 = image 759,456 + 码点索引 14,890 + 堆管理开销 ≈ 预算值；步骤 3 全表遍历 **703 ms、0 failures**（流式版 262.6 秒 → 约 370 倍提速，进度行逐段推进）；步骤 4 `fetches: hits=15190 misses=0` 与常驻契约一致；步骤 5 fallback、步骤 6 fixture 以 `ESP_ERR_INVALID_CRC` 被拒／正式包 probe OK／缺路径 `NOT_FOUND` 全过；尾行 **`FONT_SERVICE_SELF_TEST: PASS`**（2854 ms 处，整轮较流式版缩短约 93 倍）后按设计挂起。测试构建 `xiaomiao.bin` 0x63fc0（较流式版 +64 字节）。备注：本轮 `app_init: Compile time` 仍显示旧时间戳，系 `esp_app_format` 组件未被增量重编的既有现象，判定新构建以 ELF SHA 与 `resident` 日志行为准。至此 15B 的 CP2／CP3（含运行时性能整改）实机闭环；剩余 CP4 常规固件场景（全开关 `OFF` 无 SD 冷启动中文样例目视、12／16 px 实屏可读性、20 次开合无泄漏）待人工执行。
- 2026-09-24 -- CP4 中文目视**不通过**（实机照片：Launcher 全部中文糊成重叠笔画／裁切残片，图标与色块正常），Agent 定位为字形落位度量错误：LVGL 9.5 `lv_draw_label.c:626` 的摆位公式为 `y1 = line_top + (line_height - base_line) - box_h - ofs_y`，即 `lv_font_t.base_line` 是**降部**（基线到行盒底）、`ofs_y` 是**字模盒底边相对基线的上偏移**（盒底低于基线为负，与 LVGL 8 的“顶边偏移”旧语义相反）。原实现把 XMF1 头部 ascent 直接填进 `base_line`、把 `ofs_y` 填成 `base`，中文字模整体被画到行顶上方约一个行盒（12 px 档约 22 px），故整屏中文叠压。修复：`fonts_build()` 改 `base_line = line_height - base`（12／16 档得 2／3），`xmf_get_glyph_dsc()` 改 `ofs_y = base - px`（两档均 0，整格从行顶起画）；XMF1 包格式与生成器不动。自测补“落位不变量”断言 `(line_height - base_line) - box_h - ofs_y == 0`（两档），堵住“位图正确但排版错位”这类 API 级漏检。教训：此前两轮自测 `PASS` 只证明字节管线，视觉验收不可省略。状态：已修复＋静态检查（ASCII／括号配平），待人工重跑字体自测并重新目视。
- 2026-09-24 -- 落位修复复验通过（两步）：① 字体自测重跑（ELF `e853cd96c`，含新增落位不变量断言）仍 `FONT_SERVICE_SELF_TEST: PASS`，驻留版指标无回归（init 594 ms、`internal=0 SPIRAM=-785160`、遍历 703 ms／0 failures、`fetches: hits=15190 misses=0`）；② 常规固件（六开关全 `OFF`）无 SD 冷启动，`font READY ... resident, init 625 ms` → `Launcher ready, 5 app(s)`，项目负责人目视确认**五个 App 的中文名与页面内容中文显示全部正常**，叠压／上飘缺陷消除。同轮附带开合样本 5 组（Hardware Test／Games／PC Monitor／Tools／Settings 各一次开＋关），`screen children=2` 开合前后恒定、`launcher focus=4 page=1` 正确恢复、无内存告警，仅 `storage_svc 0x107`（无卡重试，既有行为）。15B CP4 的中文显示与可读性项就此取证；20 次开合的完整样本量属 15D CP4／CP6 口径，本轮记为代表样本。

## 交付结果

- CP1：字体包、fixture、生成／验证工具、许可证与来源指纹已提交仓库（见上条路径）；可复现性以两次生成 SHA-256 一致证明。
- CP2／CP3：`xiaomiao_font_service.{h,c}`、`xiaomiao_fonts.h`、`xiaomiao_font_service_selftest.{h,c}` 与构建接入完成；流式版（ELF `6418ab4b8`）与 PSRAM 整包驻留版（ELF `81482b8ac`，及字形落位修复后的 `e853cd96c`）均实机输出 `FONT_SERVICE_SELF_TEST: PASS`，驻留版遍历 703 ms／0 failures、内部 RAM 成本 0。
- 接口按本 Goal「公开接口与运行时契约」冻结；15C 只允许通过 `xiaomiao_font_small/body/title()` 与 `xiaomiao_font_service_ready()` 消费字体，不需要改包格式。
- 已取得实机证据（原未验证项，2026-09-23／24 三轮自测＋一轮常规固件目视）：字体初始化耗时（569／594 ms）、PSRAM 实际占用（驻留版 -785,160 B）、取用计数契约（hits=15190／misses=0）、全表 7,445 码点遍历自测 `PASS`、字形落位不变量（ELF `e853cd96c`）、损坏 fixture 仅以 CRC 被拒（probe）、Montserrat 回退链语义、**常规固件无 SD 冷启动五 App 中文显示与 12／16 px 可读性目视通过**（落位修复后）、5 组 App 开合 `screen children=2` 恒定无泄漏告警（代表样本）。
- 未验证范围（保持“未验证”）：无——20 次开合已由人工补齐（2026-09-24 两轮合计 26 组开合，`screen children=2` 恒定、无内存告警）；生产路径损坏注入的英文回退启动观察经项目负责人决定**明确跳过**（不作为通过项，仅保留 probe 级 CRC 拒收与 token 函数永不返回 NULL 的静态契约）。
