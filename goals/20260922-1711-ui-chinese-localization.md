# 节点 15C：全系统中文化与 160 × 128 布局适配

## 元信息

- 父施工总纲：`goals/20260922-1701-assets-filesystem-chinese-font.md`
- 状态：已实施（编码与静态核收完成），待人工验证
- 创建时间：2026-09-22 17:11（北京时间）
- 前置 Goal：`goals/20260922-1711-flash-chinese-font-runtime.md`
- 后续依赖：节点 15D 最终集成与实机收口

## 目标与预期行为

在已验证的 Flash 完整中文字库上建立固定 `zh-CN` 文案层，把当前 Launcher、四个业务 App、Hardware Test 15 页、设备端状态和配网提示全部迁移为默认中文，并针对 160 × 128 屏幕逐页重排。字体服务不可用时，所有页面整体切回现有英文文案和 Montserrat，不允许出现中文方框、半中文半英文状态或不可操作页面。

本 Goal 不修改字体格式、Assets 挂载、SD 生命周期、业务逻辑和导航语义。

## 必要背景与接口依赖

- 15B 已交付稳定的 `xiaomiao_font_small()`、`xiaomiao_font_body()`、`xiaomiao_font_title()` 与字体 READY 状态。
- 当前 UI 大量直接使用 Montserrat 10／12／14 px 和英文字符串；中文 12／16 px 会改变宽度、行高和换行。
- 屏幕为 160 × 128，右上角固定有 18 × 12 Wi-Fi 图标；Hardware Test 页码结束位置必须保持在图标左侧。
- Launcher 2 × 2 网格、五 App 注册顺序、B 键语义和 Hardware Test 15 页行为都是回归基线。

## 文案架构与已确定决策

建议新增：

```text
main/framework/xiaomiao_i18n.h
main/framework/xiaomiao_i18n.c
```

接口：

```c
typedef enum {
    XM_TEXT_...
} xiaomiao_text_id_t;

const char *xiaomiao_text(xiaomiao_text_id_t id);
bool xiaomiao_i18n_is_chinese(void);
```

决策：

1. 本版本默认 `zh-CN`，不增加语言设置项。
2. 字体服务 READY 后才能选择中文；否则选择完整英文表。
3. 用户可见文案通过稳定 ID 获取，格式化参数由调用方显式传入；不把外部输入当格式字符串。
4. CPU／RAM／GPU、SSID、IP、HTTP、ESP-IDF、版本号等技术缩写保留英文。
5. 串口日志保持英文，便于检索和对照 SDK 错误。
6. Wi-Fi 配网页面可同步中文化，但浏览器字体由手机提供，不计入设备中文字库验收。
7. 动态 SSID、卡名、错误详情和版本号必须有确定的截断、滚动或分行策略。

## 页面范围

必须迁移并目视检查：

- Launcher：标题、五个 App 名称、页码、提示、空状态、图标降级。
- Games：占位标题、说明和返回提示。
- PC Monitor：两页标题、状态、温度标签和无数据状态。
- Tools：主菜单、Wi-Fi、System Info、About、Assets 诊断页。
- Settings：Wi-Fi、Display、Sound、System、配网状态、自动连接、忘记网络和确认页。
- Hardware Test：15 页标题、值说明、操作提示、动作结果和错误状态。
- 全局启动／故障提示及设备端配网信息。

## 允许修改范围

- `main/framework/xiaomiao_i18n.{h,c}` 和字体令牌调用点。
- `main/framework/xiaomiao_launcher.c`。
- `main/apps/games/`、`pc_monitor/`、`tools/`、`settings/`。
- `main/main.c` 中 Hardware Test UI 文案与布局。
- Wi-Fi 配网页面的用户可见 HTML 文案。
- 与中文 UI 直接相关的自测期望、截图说明和文档。

## 禁止修改范围

- 不修改 Assets Service、XMF1 格式、字体生成算法和缓存策略；发现问题应回退 15B 修正并重新验收。
- 不修改 App 注册顺序、Launcher 导航、Navigation 生命周期或 B 键语义。
- 不修改 Wi-Fi、Agent、Settings、Storage 的业务状态机和持久化语义。
- 不添加语言设置、繁体中文、其他语言或在线翻译。
- 不借中文化重构无关 UI 代码、颜色体系或业务目录。

## 检查点与验收

### CP1：文案表与自动缺字检查

- 为所有用户可见文本建立中／英文映射。
- 扫描新增中文字符并调用 15B 验证工具确认字体覆盖。
- 静态检索残留英文，人工区分应翻译文案与允许保留的技术字段。
- 字体不可用测试模式下，所有文案返回英文。

### CP2：Framework 与四个业务 App

- Launcher 保持 2 × 2 网格、分页和焦点行为。
- Games、PC Monitor、Tools、Settings 逐页迁移字体与文案。
- Tools 增加 Assets 诊断页，菜单焦点、两级 B 和 timer 生命周期保持现有契约。

### CP3：Hardware Test 15 页

- 逐页迁移标题、值、说明、提示和动作结果。
- 保持全部外设操作、A／B／方向键语义和 800 ms 长按返回。
- 页码、Wi-Fi 图标、标题和动态值不得重叠。

### CP4：英文故障降级

- 通过测试配置模拟 Font Service unavailable。
- Launcher、四个 App、Hardware Test 和配网页全部使用英文 Montserrat。
- 不允许仅字体回退、文案仍为中文的方框页面。

### CP5：静态与人工 UI 验收

Agent 执行字符串覆盖、对象生命周期、timer、焦点和 diff 范围静态检查，不运行固件构建或设备操作。

项目负责人人工验证：

1. 无 SD 冷启动进入中文 Launcher。
2. 五个 App 全部进入、返回和分页正常。
3. Tools／Settings 两级 B 行为不变。
4. Hardware Test 15 页逐页往返。
5. 中文标题、正文、动态值、SSID、IP、版本号无溢出和重叠。
6. 深色页面与 Hardware Test 黄色页面均清晰。
7. 英文失败注入模式全部页面可操作。
8. 重复进入／退出各 App，LVGL 对象和 timer 无泄漏。

UI 变化必须提供截图或逐页明确确认；仅有串口日志不能证明布局通过。

## 完成标准

- 全部指定页面默认中文且通过缺字扫描。
- 160 × 128 逐页布局和输入行为取得人工证据。
- 英文故障降级完整，不显示中文方框。
- 业务状态机、导航、生命周期和硬件行为无回归。
- 结果同步本 Goal、父 Goal、ROADMAP 和历史索引。

## 实施记录

- 2026-09-22 17:11 -- 从节点 15 总纲拆分本独立 Goal。状态：等待 15B。
- 2026-09-22 18:28 -- 15C 实施进展快照（Agent 与后台代理并行施工，尚未收口）：
  - 词表已冻结：`main/framework/xiaomiao_i18n.{h,c}` 共 203 个 `XM_TEXT_*` ID，中／英双表完整、无重复缺项；中文仅在 `xiaomiao_font_service_ready()` 时启用，否则整表回退英文；全部非 ASCII 字符已确认落在 GB2312 内，无缺字。
  - 已完成迁移：Launcher（提示／空态文案 + `xiaomiao_font_small()` + 图标三级回退链：内置 `asset:/icons/<id>.bin` 16×16 RGB565 → LVGL Symbol → 占位条，见 `xiaomiao_icons.{h,c}`，已注册 `FRAMEWORK_SRCS`）、Games、PC Monitor（含 GPU T／CPU T／TEMP 行名切换）、Tools（50 处 `xiaomiao_text()`，新增第 4 菜单项 Assets 诊断页：三服务快照 Partition／Total／Used／Font／Glyphs／SD 共 6 行，动态值 `LV_LABEL_LONG_MODE_DOTS` 截断）。
  - 未完成：`main/apps/settings/xiaomiao_settings.c`（18:29 进行中，已有 14 处 `xiaomiao_text()` 调用，尚未收口）与 `main/main.c` Hardware Test 15 页（未开始，0 处调用）；随后还需全量残留英文扫描、203 个 ID 的引用核销与布局复核。恢复 Goal 后先查看后台代理完成通知，再按上述清单核收，勿在代理写入期间改动这两个文件。
- 2026-09-22 18:41 -- 15C 停车时实况（后台代理到达其自身回合上限后被终止，主 Goal 同时到达回合上限暂停）：
  - `xiaomiao_settings.c`：代理已完成整文件迁移（末次写入 18:34:51，66 处 `xiaomiao_text()`，花括号平衡）。
  - `main/main.c`：代理末次写入 18:40:11，Hardware Test 15 个页名 ID（`XM_TEXT_HT_PAGE_*`）全部被引用、8 处 `xiaomiao_text()` 调用点、花括号平衡；从产物看迁移疑似已基本落地，但终止发生在最后一次编辑之后不久，**是否收尾完整未经逐段核收**。
  - 剩余工作（恢复后执行）：逐段核收 Settings 与 main.c 的迁移完整性与布局常量适配、残留英文全量扫描、203 个文案 ID 引用核销、`git diff --check`、ASCII／括号平衡终检；随后完成 15D 终检文档与 README 同步。所有编译与显示效果仍为未验证，等待人工 CP4／CP7。
- 2026-09-22 19:20 -- 15C 收口完成（恢复 Goal 后由专用后台代理补完 main.c，主 Agent 逐段核收）：
  - `main/main.c`：Hardware Test 15 页全量接线——页名 ID 表 + `ui_page_name()`、`short_err()`、10 条提示、7 个手势 token 渲染期映射（模型值保持 ASCII，I2C 轮询先于 Font 初始化）、37 条动作结果（48 字节 `action` 缓冲经 `copy_text`/`snprintf("%s")` 截断安全）、11 条 About 段落头（30 占位/30 参数核对）、App 名运行时解析；`HARDWARE_TEST_APP_NAME`／`BTN_B_HOLD_HINT_TEXT`／`s_page_names`／`lv_font_montserrat_*` 全部移除。
  - 核收结果：203 个 ID 中 201 个被引用；`XM_TEXT_HT_FAIL`（唯一 FAIL 字面量已由 `short_err` 走 `ERR_FAIL`，同文案）与 `XM_TEXT_STATE_DISABLED`（无对应字面量）保留为未引用声明，不构成方框风险。全部 UI 文件花括号平衡、0 非 ASCII 行（PC Monitor 一处中文注释词已改 ASCII 描述）；`git diff --check` 无空白错误；App／Framework 无 `/assets`、`/sdcard` 物理路径与 `esp_wifi_*`／NVS 调用。
  - 有意保留英文：串口日志、配网网页、自测、`"Xiaomiao"` 品牌、页码 `"0/0"`、技术缩写与寄存器值（ON/OFF、VOUT/PWM、`GD32 0x40` 等）、`--` 占位与单位。
  - 未验证：一切编译错误、字模可读性、逐页布局重叠、中文混排截断效果——等待人工构建烧录与 CP 逐项目视确认。
  - 代理写入的文件与主 Agent 的图标接线已通过 CMake／花括号平衡／纯 ASCII 检查；所有编译与显示效果均为未验证（不执行 `idf.py`，等人工 CP）。状态：实施中。

## 交付结果

- 已交付：`xiaomiao_i18n.{h,c}`（203 文案 ID、中英双表、Font READY 决定语言）、`xiaomiao_icons.{h,c}`（内置图标三级回退）、`xiaomiao_fonts.h` 令牌全 UI 接入；Launcher／Games／PC Monitor／Tools（含 Assets 诊断页）／Settings／Hardware Test 15 页中文化与 160 × 128 布局适配。
- 静态核收全部通过（引用覆盖、ASCII、括号、路径与调用边界、`git diff --check`）；编译与实机显示未验证，待人工执行本 Goal CP 与父 Goal CP4／CP7。
