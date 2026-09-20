# Goal：实现 Games 占位 App

## 元信息

- 对应节点：节点 5“Games 占位 App”
- 状态：已完成（2026-09-20 12:29，普通构建、烧录、Monitor、双入口视觉与按键路径、双 App 焦点、多轮生命周期和 Hardware Test 回归均通过；两处数量口径差异经人工确认接受，原文保留于“验证结果”）
- 创建时间：2026-09-20 11:59（北京时间）；实现时间 2026-09-20 12:15；验收时间 2026-09-20 12:29
- 前置条件：节点 1～4 均已完成；直接依赖 App Registry／Manager、Navigation、Launcher 和节点 4 已接入普通启动链的 App 切换基础
- 前置施工文档：`goals/20260919-2037-app-runtime.md`、`goals/20260920-0816-navigation.md`、`goals/20260920-1007-launcher.md`、`goals/20260920-1053-hardware-test-app.md`
- 后续目标：节点 6“PC Monitor UI”

## 目标与预期行为

新增一个独立的 `Games` 占位 App，并通过统一 App Registry 注册到普通固件。开机后 Launcher 显示 `Games` 与 `Hardware Test` 两个入口，默认焦点位于 Games；用户按 A 进入 Games 占位页，短按 B 返回 Launcher。占位页只验证正式业务 App 的目录组织、静态注册、Navigation 生命周期、Launcher B 返回分支和多 App 焦点保持，不实现任何真实游戏逻辑。

节点 5 完成后，必须用两个真实 App 验证节点 4 单 App 阶段无法证明的焦点语义：从 Games 返回后焦点仍在 Games，从 Hardware Test 返回后焦点仍在 Hardware Test。两种 App 的 B 交互保持各自语义：Games 短按 B 返回，Hardware Test 短按 B 仍执行页面操作、长按 800 ms 返回。

## 背景与文件入口

- `main/main.c` 的 `launcher_boot()` 当前只注册 `s_hardware_test_app`，随后执行 Manager 初始化和 Launcher 创建。
- `main/framework/xiaomiao_app.h` 定义静态 App 描述和 Registry；Registry 按注册顺序枚举，Launcher 也按该顺序生成入口。
- `main/framework/xiaomiao_navigation.h/.c` 在 App `open` 前创建内容根，在 App `close` 后删除内容根及全部子对象。
- `main/framework/xiaomiao_launcher.c` 在 App 打开且 Launcher 根对象仍持有焦点时，忽略方向键和 A，只把 B 转发给 `xiaomiao_navigation_back()`。
- Hardware Test 会把 Dashboard 输入对象加入 LVGL group 并取得焦点，因此其 B 手势仍由 `main/main.c` 的专用状态机处理，不经过 Launcher 转发分支。
- `main/CMakeLists.txt` 当前尚无 `main/apps/` 业务 App 源文件。
- `docs/xiaomiao_firmware_v0.1_design.md` 第 4、6、12、13、15 节规定 Games 在 Launcher 中优先显示，v0.1 只提供可进入、可返回的占位页面，并禁止为每个 App 创建独立 FreeRTOS Task。

## 范围边界

允许修改：

- 新增 `main/apps/games/xiaomiao_games.h` 和 `main/apps/games/xiaomiao_games.c`。
- 修改 `main/CMakeLists.txt`，把 Games 源文件纳入现有 `main` 组件。
- 修改 `main/main.c`，包含 Games 接口并在 `launcher_boot()` 中按确定顺序注册 Games 与 Hardware Test。
- 增加最小打开／关闭日志和必要对象引用，用于验证生命周期与对象释放。
- 更新本 Goal、`ROADMAP.md`、`README.md`、`docs/project-overview.md` 和确实受实现事实影响的设计说明。

禁止修改：

- 实现 Snake、Tetris、2048、游戏菜单、分数、存档、音效、动画或任何真实游戏逻辑。
- 新增 FreeRTOS Task、队列、锁、软件定时器或跨任务 UI 调用。
- 修改 App Framework、Navigation、Launcher 的公开接口或节点 1～3 自测行为。
- 修改 Hardware Test 的 15 页内容、B 短按／长按状态机、硬件状态和刷新逻辑。
- 访问 GPIO、SPI、I2C、ADC、LEDC、MicroSD、Wi-Fi、NVS、音频或其他硬件／Service。
- 引入图片、字体、文件系统资源、第三方依赖，或修改 GD32、协议、引脚、ESP-IDF／LVGL 版本与 `dependencies.lock`。
- 为目录形式进行无关重构、移动 Dashboard 或批量格式化。

## 已确定的实现决策

1. Games 是第一个独立业务 App，放在 `main/apps/games/`；不继续把新业务 App 堆入 `main/main.c`，也不提前创建通用 App 基类、页面工厂或主题系统。
2. `xiaomiao_games.c` 内定义固件全生命周期有效的 `static const xiaomiao_app_t` 描述；头文件只暴露 `const xiaomiao_app_t *xiaomiao_games_app(void)`，不暴露内部 LVGL 对象或生命周期回调。
3. App 描述固定为 `id = "games"`、`name = "Games"`、`icon = LV_SYMBOL_PLAY`；`init = NULL`，`open`／`close` 只负责占位 UI 生命周期。
4. 普通固件注册顺序固定为 Games 第一、Hardware Test 第二，与设计稿的 Games 优先顺序一致。任一注册失败都记录 App ID 与错误码并终止后续 Manager／Launcher 初始化，不跳过失败 App，也不回退到旧 Dashboard。
5. Games `open` 只在 `xiaomiao_navigation_app_root()` 下创建一个占位 UI 容器及其标签，不创建、切换或删除全局 screen，也不删除 Navigation 内容根。
6. 占位页适配 160 × 128 屏幕，至少显示 `Games`、`Coming soon` 和 `B Back`。使用 LVGL 内置字体、颜色和 symbol，不依赖外部资源；文字不得溢出或重叠。
7. Games 不向 LVGL group 添加对象，也不注册按键事件。Launcher 根对象继续持有焦点：App 打开期间方向键和 A 被 Launcher 忽略，短按 B 经现有 `launcher_go_back()` 返回。
8. 不为 Games 复制 Hardware Test 的全局 B 状态机。Hardware Test 取得焦点后仍由自身状态机处理 B；Games 不取得焦点时由 Launcher 处理 B，两条输入路径通过当前焦点自然互斥。
9. Games `close` 不调用 `lv_obj_delete()`，只清空自身对象引用并记录关闭日志。Navigation 在回调返回后删除内容根及全部子对象。
10. Games 不创建 timer、event、Task、动态业务状态或非 LVGL 资源。每次 `open` 创建全新占位页，不复用上一次已删除的对象引用。
11. Navigation 内容根为空时记录错误，不在活动 screen 上另建页面绕过 Navigation。现有 `void open(void)` 接口不为节点 5 扩展错误返回机制。
12. `open`／`close` 日志记录活动 screen 子对象数量。`close` 日志发生在 Navigation 删除内容根之前，跨轮次是否泄漏以每次 `open` 的稳定值和下一轮进入为准。
13. 两 App 焦点验收使用公开查询接口和屏幕行为共同判断：Games 注册索引为 0，Hardware Test 为 1；从每个 App 返回后焦点索引分别保持 0 和 1，页码均为 0。
14. 节点 1～3 的三个 self-test 构建不注册真实 Games App，继续使用各自测试数据；普通固件才注册 Games 与 Hardware Test。
15. ESP-IDF 编译、`set-target`、烧录、串口监视和目标板按键／显示验证全部由人工执行。Agent 只实施源码与文档修改、执行静态检查、提供命令并复核人工证据。

## 预期接口与生命周期

```c
#pragma once

#include "framework/xiaomiao_app.h"

const xiaomiao_app_t *xiaomiao_games_app(void);
```

- 返回值始终指向同一个静态描述，不分配内存，调用方不得修改或释放。
- `open` 取得 Navigation 内容根，拒绝重复创建，在其下创建占位容器和三个文本元素；不调用任何 group 或 input API。
- `close` 允许在未创建或部分创建状态下调用，不删除 Navigation 内容根，只清空全部 Games 对象引用。

## 执行检查点

### 检查点 1：Games 模块与静态注册

- 创建最小 `main/apps/games/` 模块和描述访问接口。
- 修改 CMake 只增加一个 Games 源文件，不引入新组件或依赖。
- 在普通启动链按 Games → Hardware Test 顺序注册两个 App，再执行 Manager 初始化和 Launcher 创建。
- 保留逐步错误检查，日志明确显示成功注册 2 个 App。

预期结果：Launcher 自动出现两个入口，顺序来自 Registry，不含业务硬编码菜单分支。

### 检查点 2：占位 UI 与 Navigation 所有权

- 在 Navigation 内容根下创建 160 × 128 占位页面。
- 人工确认标题、占位文本和返回提示清晰，无裁剪、重叠或屏幕外内容。
- 进入 Games 时不创建新 screen、不注册 group 对象；返回后 Navigation 内容根完整释放。
- 连续至少 10 轮进入／返回，核对 open／close 成对、活动 screen 子对象数量稳定、无 panic 或重启。

预期结果：Games 页面可重复创建和销毁，全部 LVGL 对象由 Navigation 内容根间接拥有，无泄漏。

### 检查点 3：Games 输入与 Launcher 返回分支

- 默认焦点位于 Games，按 A 打开占位页。
- Games 打开期间方向键和 A 均无状态变化、无重复打开、无 Launcher 焦点移动。
- 短按 B 只返回一次，释放或按键重复不得引发第二次 close。
- 返回后焦点仍在 Games，按右键可移到 Hardware Test。

预期结果：Launcher 的 App 打开态 B 转发分支在普通固件真实 App 中得到验证，其他键保持无操作。

### 检查点 4：双 App 焦点与 Hardware Test 回归

- Games 返回后确认焦点索引 0、页码 0，屏幕焦点样式落在 Games。
- 右移到 Hardware Test，按 A 进入；短按 B 仍执行 Dashboard 当前页操作，不返回。
- 长按 B 800 ms 返回后确认焦点索引 1、页码 0，焦点样式落在 Hardware Test。
- 左移回 Games 并再次进入／返回；交替执行至少 5 轮，确认两种 B 路径互不干扰。
- 快速检查 Hardware Test 15 页仍可左右翻页，A／短按 B／长按 B 既有语义无回归。

预期结果：节点 4 单 App 阶段无法证明的焦点保持得到双入口证据，Hardware Test 输入状态机不受 Games 影响。

### 检查点 5：静态检查、人工构建与文档交付

- Agent 检查 App ID 唯一性、注册顺序、CMake 接入、Navigation 父子关系、group 所有权和改动范围。
- Agent 执行 `git diff --check`、精确引用检索和源码静态检查，不主动执行 `idf.py`、烧录、monitor 或串口操作。
- 人工使用 ESP-IDF 6.1 完成普通构建、烧录、Monitor、视觉与按键路径，并提供关键结果。
- 验证完成后更新本 Goal、`ROADMAP.md`、`README.md` 和 `docs/project-overview.md`；未验证项如实保留。

预期结果：代码、实机行为和当前状态文档一致，节点 5 不扩大为真实游戏开发。

## 失败路径与处理要求

- Games 描述为空、ID 重复或注册失败：不得在缺失 Games 的情况下继续作为成功启动；记录 App ID 和错误码，保持节点未完成。
- Games 注册在 Hardware Test 之后：修正 Registry 顺序，不在 Launcher 内增加排序规则。
- Navigation 内容根为空：不得改用 `lv_screen_active()` 创建页面；记录错误并保留 B 返回能力。
- Games 新增 group 对象或抢占焦点：会绕开本节点要验证的 Launcher B 分支，视为范围偏离，除非出现有证据的 LVGL 限制并先修订 Goal。
- Games 内方向键或 A 移动 Launcher 焦点、重新打开 App 或改变页面：视为输入隔离失败。
- 短按 B 无法返回、触发两次 close 或需要长按：视为 Games 返回失败，不复用 Hardware Test 状态机绕过。
- Hardware Test 短按 B 变为返回或长按 B 失效：视为阻塞性回归，恢复基于焦点的输入互斥。
- 返回后焦点总回到 Games 或索引与进入前不同：视为状态保持缺陷，不得在 App close 手动重设固定索引掩盖。
- 多轮后 screen 子对象数量增长、重复日志、崩溃或花屏：保持节点未完成，定位内容根、引用清理或重复创建问题。
- 人工无法完成编译、烧录或实机验证：只记录静态结果，对应项标记“未验证”，不得将节点 5 标记完成。

## 验收标准

全部条款均已取得证据并判定达标（第 8 条附两处数量口径差异，见“验证结果”的“口径差异说明”，已由人工确认接受）。

- [x] 新增独立 `main/apps/games/` 模块，未把 Games 实现继续堆入 `main/main.c`。
- [x] Games 描述使用 `id = "games"`、`name = "Games"` 和 LVGL 内置图标，并通过只读接口提供给启动链。
- [x] 普通固件按 Games → Hardware Test 顺序注册 2 个真实 App；Launcher 不含 Games 专用入口逻辑。
- [x] 开机显示两个入口，默认焦点位于 Games，右键可移动到 Hardware Test。
- [x] A 可进入 Games，占位页清晰显示 `Games`、`Coming soon` 和 `B Back`。
- [x] Games UI 全部位于 Navigation 内容根下，不创建／切换全局 screen，不加入 LVGL group。
- [x] Games 内方向键和 A 无操作；短按 B 经 Launcher 现有分支返回且只关闭一次。
- [x] Games 返回后焦点保持索引 0；Hardware Test 返回后焦点保持索引 1；页码均为 0。（HT 侧 4/4 次 `launcher focus=1 page=0`；Games 侧为目视与行为证据，见口径差异说明）
- [x] 连续 10 轮 Games 进入／返回及至少 5 轮双 App 交替后，open／close 成对、screen 子对象数量稳定，无崩溃、panic、重启或输入失效。（实际为 10 轮 Games 与 4 轮 HT，全部配对、`screen children=2` 恒定；差异见口径差异说明）
- [x] Hardware Test 左右翻页、A、短按 B 和长按 800 ms 返回无回归。
- [x] Games 未访问硬件、未创建 Task／timer／队列／锁，未实现真实游戏功能。
- [x] Framework、Navigation、Launcher 运行时与三个 self-test 入口未修改。
- [x] 未修改 GD32、协议、引脚、依赖版本或 `dependencies.lock`。
- [x] 人工提供 ESP-IDF 6.1 普通构建、烧录、Monitor、双入口视觉、Games B 返回、双 App 焦点、多轮生命周期和 Hardware Test 回归结果。
- [x] `git diff --check` 与改动范围静态检查通过，Goal、`ROADMAP.md` 及受影响状态文档已同步。

## 交付内容

- `main/apps/games/xiaomiao_games.h/.c`：Games 描述访问接口、占位 UI 和最小生命周期。
- `main/CMakeLists.txt`：Games 源文件接入。
- `main/main.c`：Games → Hardware Test 的普通固件注册顺序。
- 人工构建、烧录、Monitor、双 App 焦点和多轮生命周期验证步骤。
- 记录实现、验证证据、失败修复和未验证范围的本 Goal。
- 更新后的 `ROADMAP.md`、`README.md` 和 `docs/project-overview.md`。

## 人工验证命令与预期结果

前置：在 PowerShell／CMD 中按 `AGENTS.md` 加载 ESP-IDF 6.1；非默认布局使用进程级 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`。串口号以下以 COM5 为例。

```bash
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

预期：构建和烧录成功，Launcher 注册 2 个 App；左侧 Games、右侧 Hardware Test，初始焦点在 Games；Games 三段文本显示正常，方向键和 A 无操作，短按 B 返回且焦点保持；Hardware Test 短按 B 不退出、长按 800 ms 返回且焦点保持；多轮切换无对象增长、panic 或输入失效。

## 委派约束

本 Goal 当前不计划委派。若后续明确授权使用子 Agent，主 Agent 必须先在本文件新增子任务编号，写明目标、背景、依赖、允许与禁止修改范围、实现决策、失败路径、验收方式和预期结果。派发时必须提供本文件路径和任务编号；主 Agent 最终核对实际改动和验证证据，不得只凭子 Agent 声明认定通过。

## 创建记录（2026-09-20 11:59）

- 已对照路线图、项目概览、设计文档第 4／6／12／13／15 节、节点 1～4 Goal、App／Navigation 接口、Launcher App 打开态输入分支和当前 `launcher_boot()` 注册链。
- 已确定 Games 使用独立业务目录和只读描述访问接口，不为单个占位页增加通用抽象。
- 已确定按 Games → Hardware Test 注册，借两个真实入口补验节点 4 单 App 阶段无法证明的焦点保持。
- 已确定 Games 不加入 LVGL group，短按 B 复用 Launcher 已实现但尚未在普通固件真实 App 中验证的返回分支；Hardware Test 长按 B 状态机保持不变。
- 本记录只完成施工设计，未修改固件源码，编译、烧录、Monitor 和目标板行为均未验证。

## 实现记录（2026-09-20 12:15）

### 实际改动

- 新增 `main/apps/games/xiaomiao_games.h`：只声明 `const xiaomiao_app_t *xiaomiao_games_app(void)`，包含 `framework/xiaomiao_app.h`，不暴露 LVGL 对象或生命周期回调（决策 2）。
- 新增 `main/apps/games/xiaomiao_games.c`：
  - `static const xiaomiao_app_t s_games_app`，字段为 `id="games"`、`name="Games"`、`icon=LV_SYMBOL_PLAY`、`init=NULL`、`open=games_open`、`close=games_close`（决策 3）。
  - `games_open()` 先查 `s_container` 非空则报错返回（拒绝重复创建，决策 10），再取 `xiaomiao_navigation_app_root()`；为空时记录 `no App content root to build the placeholder in` 并返回，不在活动 screen 上另建页面（决策 11）。随后在内容根下创建 `lv_obj_create` 容器（`LV_PCT(100)` × `LV_PCT(100)`、背景 `0x0E1016`、不可滚动），再依次创建 `Games`（`lv_font_montserrat_14`、`0xC8D0E0`）、`Coming soon`（`lv_font_montserrat_12`、`0x9AA6BC`）、`B Back`（`lv_font_montserrat_10`、`0x5A6478`）三个标签，全部 `LV_TEXT_ALIGN_CENTER` 且宽度 `LV_PCT(100)`，分别对齐 `LV_ALIGN_TOP_MID` y=24、`LV_ALIGN_TOP_MID` y=52、`LV_ALIGN_BOTTOM_MID` y=-12（决策 6）。
  - 标签创建与对齐收敛在一个 `games_create_label()` 内，模块只保留 `s_container` 一个静态引用；三个标签无独立引用，随 Navigation 删除内容根一并释放。
  - 未调用任何 group 或 input API，未注册键事件，未创建 timer／event／Task，未访问任何硬件（决策 7、10）。
  - `games_close()` 只把 `s_container` 置空，不调用 `lv_obj_delete()`（决策 9）。
  - `open`／`close` 各输出一条含活动 screen 子对象数量的日志（决策 12），格式为 `games opened, screen children=%u` 与 `games closed, screen children=%u`。
- `main/CMakeLists.txt`：新增 `set(APP_SRCS "apps/games/xiaomiao_games.c")` 并加入 `idf_component_register(SRCS ...)`；同时把 `INCLUDE_DIRS ""` 改为 `INCLUDE_DIRS "."`（原因见下）。
- `main/main.c`：新增 `#include "apps/games/xiaomiao_games.h"`；新增 `static esp_err_t launcher_register_app(const xiaomiao_app_t *app)`（统一输出 App ID 与错误码）；`launcher_boot()` 改为先注册 `xiaomiao_games_app()`、再注册 `s_hardware_test_app`，任一步失败直接返回，不回退到旧 Dashboard（决策 4）。

### 必须记录的一处框架外修正

`main/CMakeLists.txt` 原为 `INCLUDE_DIRS ""`，`main/` 自身不在编译 include 路径上（已从 `build/compile_commands.json` 核实 main.c 的编译命令中不存在 `-I<main>`；此前所有 `framework/...` 形式引用都靠“相对包含文件所在目录”解析成功）。本 Goal 决策 2 要求 `xiaomiao_games.h` 写 `#include "framework/xiaomiao_app.h"`，而该文件位于 `main/apps/games/`，相对路径无法解析，因此必须把 `main/` 加入 include 路径。改为 `INCLUDE_DIRS "."` 后，`main/` 下所有 `xiaomiao_*` 头文件均可按 `framework/...`／`apps/...` 引用；已确认 `main/` 内不存在可能与系统头文件或组件头文件重名的头文件名（全部为 `xiaomiao_*`），本次改动不改变既有文件的解析结果。

### 已完成的静态检查

- `git diff --check` 通过（仅 `ROADMAP.md` 的 CRLF 提示）；大括号配平 `xiaomiao_games.c` 9/9、`xiaomiao_games.h` 1/1、`main/main.c` 350/350。
- 改动范围核对：`main/apps/games/`（新增 2 文件）、`main/CMakeLists.txt`、`main/main.c`、文档。未修改 `main/framework/` 下节点 1～3 文件、Dashboard 15 页内容与 B 短按／长按状态机、GD32、硬件协议、引脚、ESP-IDF／LVGL 版本或 `dependencies.lock`；未新增组件或第三方依赖。
- 禁止项检索：`xiaomiao_games.c` 内无 `lv_group`、`lv_obj_add_event_cb`、`xTaskCreate`、`esp_timer`、`vTaskDelay`、队列／信号量／互斥量、`gpio_`／`i2c_`／`spi_`／`adc_`／`ledc_`／`nvs_`／`sdspi`、`fopen`、`lv_obj_delete`；仅有两处只读 `lv_obj_get_child_count(lv_screen_active())`，不创建或切换 screen。
- LVGL 9.5 API 核对：所用函数与宏、`lv_font_montserrat_10/12/14`、`LV_SYMBOL_PLAY` 均已在本仓库 `managed_components/lvgl__lvgl` 中确认存在；字体由 `sdkconfig.defaults` 的 `CONFIG_LV_FONT_MONTSERRAT_10/12/14=y` 启用。
- 自测形态核对：节点 1～3 三个 self-test 入口不调用 `launcher_boot()`，因此不会注册 Games，继续使用各自测试数据（决策 14）；Games 源文件仍参与编译。

### 范围外但必要的一处事实同步

`AGENTS.md` 原写“`main/main.c` 是当前 ESP32-WROVER-B 固件的唯一业务源文件……当前尚无 `apps/`、`services/` 或 `bsp/` 分层”。新增 `main/apps/games/` 后该句成为错误事实，会误导后续 Agent 判断目录职责，故做一处最小事实订正（改写该句为：`main/main.c` 为主业务源文件并持有启动链，`main/framework/` 为框架运行时，`main/apps/<app>/` 为业务 App，`services/` 与 `bsp/` 尚未建立）。未改动该文件的其他规则、构建命令或验证分工。

### 实现时的未验证范围（2026-09-20 12:15，已被 12:29 验证结果取代）

- 编译（`idf.py build`）、烧录、Monitor 与目标板按键／显示行为全部未执行，按项目分工由人工完成。
- 检查点 1～4 的全部运行时结论（双入口顺序与焦点、三段文本显示、方向键与 A 无操作、短按 B 只返回一次、返回后焦点索引 0／1、10 轮 Games 进入返回、5 轮双 App 交替、Hardware Test 回归）均未取得证据。
- 焦点证据链说明：Games 不引用 Launcher 查询接口，避免业务 App 反向依赖 Launcher；`Games 返回后焦点仍在 Games` 由屏幕焦点样式、右键可移动到 Hardware Test、以及再次按 A 仍进入 Games 三项行为共同判断，`Hardware Test 返回后焦点索引 1` 由既有 `hardware test closed, ... launcher focus=%u page=%u` 日志提供数值证据。

保留原文以便追溯实现交付时的证据边界，当前结论以“验证结果”为准。

## 验证结果（2026-09-20 12:29，人工执行、Agent 复核）

人工完成 ESP-IDF 6.1 普通构建、烧录与 Monitor，提供自启动到运行 173 s 的完整串口日志，并确认视觉与按键路径“都没问题”。以下按日志逐条复核。

### 启动与注册（检查点 1）-- 通过

- 启动链为 `Xiaomiao LVGL 9.5 launcher boot` → `Start Xiaomiao launcher` → `launcher: launcher created (2 apps)` → `Launcher ready, 2 app(s) registered`，证明普通固件注册了 2 个 App，符合决策 4 的 Games → Hardware Test 顺序。
- 全程未出现 `Self test build`／`Navigation self test build`／`Launcher self test build` 三个自测标记，也未出现旧的 `Start Xiaomiao hardware dashboard`，确认走的是普通构建与 Launcher 路径。
- 日志顺序在每轮均为 `games opened`／`hardware test opened` 先于 `launcher: opened '<id>'`，与 Navigation“先创建并发布内容根 → 运行 App `open` 回调 → Manager 最后发布 current App”的实现顺序一致，无异常倒序。

### 生命周期与对象所有权（检查点 2）-- 通过

- Games 侧 10 轮 `games opened`／`games closed` 严格配对（10/10），Hardware Test 侧 4 轮 `hardware test opened`／`hardware test closed` 严格配对（4/4）；无孤立 open、无重复 close。
- **`screen children=2` 在全部 28 条 open／close 日志上恒定**（Launcher 根 + 当前 App 内容根），无一次增长，证明 Navigation 每次关闭后都完整释放 App 内容根，Games 容器与三个标签无泄漏。
- 启动 banner 只出现一次，时间戳自 803 ms 单调递增至 173147 ms，无 panic、无看门狗、无重启循环、无输入失效。

### Games 输入路径与 Launcher 返回分支（检查点 3）-- 通过

- Games 每轮进入→返回耗时依次为 3.35、1.90、0.78、0.42、0.49、0.70、0.58、0.61、0.61、0.53 s。后 8 轮均小于 1 s，说明**短按 B 即可返回**，走的是 Launcher 的 App 打开态 B 转发分支（`launcher_go_back()`），而非 Hardware Test 的 800 ms 长按状态机。
- 每轮 open 只对应一次 close，未出现“释放或按键重复触发第二次 close”的现象。
- 人工确认：Games 打开期间方向键与 A 均无状态变化（无 Launcher 焦点移动、无重复打开）；短按 B 返回后焦点高亮仍在 Games，右键可移动到 Hardware Test。

### 双 App 焦点保持与 Hardware Test 回归（检查点 4）-- 通过（含口径差异）

- Hardware Test 的 4 次 `close` 日志全部输出 `launcher focus=1 page=0`，与该 App 的注册索引一致且逐次不变--这是节点 4 单 App 阶段**无法证明**的焦点保持结论，本轮首次取得数值证据。
- Hardware Test 每轮停留 3.60、3.47、2.95、1.91 s，全部超过 800 ms 阈值，与“短按 B 执行页面操作、长按 B 才返回”的既有语义一致；Games 与 Hardware Test 两种 B 语义在同一 Monitor 会话内并存且互不干扰。
- 人工确认：Hardware Test 内短按 B 不退出、长按 800 ms 返回；15 页左右翻页、A 操作无回归；Games 打开期间按方向键只有 Games 侧反应、无 Dashboard 反应。

### 口径差异说明（已由人工确认接受）

检查点 4 与验收标准第 9 条的条款文字为“连续 10 轮 Games 进入／返回及至少 5 轮双 App 交替”，本轮实测为 **10 轮 Games（最长不中断连续段 4 轮：164105→173147 ms）与 4 轮 Hardware Test 进入／返回**，即总局数达标但“一次不中断连续 10 轮”与“第 5 轮交替”两个数量口径未满足。

- 两项均为计数口径差异，不是功能或对象生命周期缺陷：条款的实质目的（open／close 成对、`screen children` 稳定、无崩溃与输入失效）已在 14 次应用往返中完整覆盖。
- 提出该项差异后，人工明确选择“按现有证据接受”，故本 Goal 按达标记录，并在此保留实际轮次与差异原文，供后续复核时判断证据强度。

### 仍无证据的范围（不属本节点）

- LED、电机、MPU6050 的实机行为仍无证据（GD32 未实现 I2C `0x40` 从机协议、本机无电机硬件），沿用节点 4 的结论。
- MicroSD／GPIO22 冲突未在本轮日志中出现（本轮未进入 MicroSD 页操作），既存问题状态不变，仍待另立任务定位。
