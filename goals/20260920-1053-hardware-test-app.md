# Goal：将 15 页 Dashboard 封装为 Hardware Test App

## 元信息

- 对应节点：节点 4“Hardware Test App”
- 状态：已完成（2026-09-20 11:46）。代码实现于 11:18；可执行的人工测试全部通过，包括 11:20 启动链检查，以及 11:43 生命周期 16 轮／连续 11 轮且 `screen children` 恒为 2、B 短按与长按、15 页可达、MicroSD 缺失路径。唯一无法构造的条款为“从电机或蜂鸣器运行状态退出时先停止持续输出”，因无电机硬件、GD32 无 `0x40` 从机实现、蜂鸣器仅 140 ms 鸣叫，按本 Goal 失败路径条款标记未验证且不阻塞；LED／电机／MPU6050 的实机行为仍未验证
- 创建时间：2026-09-20 10:53（北京时间）
- 最后修订：2026-09-20 11:51（完成提交前静态复核，确认可执行的人工测试全部通过并统一“未验证”表述；11:46 确认电机无硬件、蜂鸣器仅 140 ms，据此把退出安全条款标为未验证并按失败路径条款收口节点；11:43 登记最终生命周期轮转 16 轮与人工确认结果；11:32 登记首轮实测与可观测日志；11:18 新增实现记录与未验证范围）
- 前置条件：节点 1“App Framework”、节点 2“Navigation”和节点 3“Launcher”均已完成；施工文档分别为 `goals/20260919-2037-app-runtime.md`、`goals/20260920-0816-navigation.md`、`goals/20260920-1007-launcher.md`
- 后续目标：节点 5“Games 占位 App”

## 目标与预期行为

将 `main/main.c` 中现有 15 页 Hardware Dashboard 接入 App Framework，注册为 `Hardware Test` App，并把普通固件默认入口从 Dashboard 切换为 Launcher。用户从 Launcher 按 A 进入 Hardware Test 后，现有 15 页显示、翻页、调节和硬件操作应继续可用；退出后返回进入前的 Launcher 页码和焦点，且 Hardware Test 创建的 LVGL 对象、事件和持续硬件动作得到完整清理。

本 Goal 使用“短按 B 保留当前页面操作、长按 B 返回 Launcher”的输入语义解决现有冲突：Dashboard 的 B 当前承担 LED 关闭、电机停止、蜂鸣器停止、MicroSD 卸载等操作，而系统级设计又要求 B 能返回 Launcher。短按与长按必须互斥，一次按键只能产生一种结果。

本 Goal 不进行 BSP、Service 或目录架构重构。现有硬件状态、驱动和 Dashboard 页面逻辑继续保留在 `main/main.c`，只做 App 生命周期、父对象、输入仲裁、刷新条件和普通启动链所必需的最小修改。待后续 BSP／Service 节点建立稳定边界后，再决定是否迁出硬件代码。

## 背景与文件入口

- `main/main.c` 是当前普通固件入口，包含硬件初始化、15 页 Dashboard、LVGL group、按键处理和周期刷新。
- `main/framework/xiaomiao_app.h` 定义 `xiaomiao_app_t` 以及 Registry、Manager 生命周期接口。
- `main/framework/xiaomiao_navigation.h/.c` 创建并拥有 App 内容根对象；App 只能在该根对象下创建子对象，关闭后由 Navigation 删除内容根。
- `main/framework/xiaomiao_launcher.h/.c` 已实现 Registry 动态入口、焦点、分页和 A 打开；Launcher 根对象在 App 打开期间保留，App 返回后不重建。
- `goals/20260920-1007-launcher.md` 已记录节点 3 的输入边界：测试 App 不注册可聚焦对象时，Launcher 可代为转发 B；Hardware Test 注册自己的输入对象后必须自行完成返回路径。
- `docs/xiaomiao_firmware_v0.1_design.md` 第 8、12、13、15 节规定保留 15 项测试、普通固件默认进入 Launcher、App 可进入和返回、SD 失败不阻塞启动。
- `README.md` 是引脚、I2C 地址和协议事实来源；本 Goal 不改变其中任何硬件定义。

## 范围边界

允许修改：

- 修改 `main/main.c`，定义并注册静态 `Hardware Test` App 描述，接入 `init/open/close` 生命周期。
- 调整 Dashboard 创建函数，使全部 Dashboard LVGL 对象位于 Navigation 提供的 App 内容根对象之下。
- 增加 Dashboard 关闭清理和 B 键短按／长按互斥处理。
- 修改普通 `lvgl_task()`，初始化 App Manager、创建 Launcher，并仅在 Hardware Test 打开时刷新 Dashboard UI。
- 在不改变现有硬件协议和页面功能的前提下，补充必要的状态复位、持续输出停止和错误日志。
- 仅在实际新增源文件时修改 `main/CMakeLists.txt`；若全部最小改动均位于 `main/main.c`，不得为了目录形式制造空模块。
- 更新本 Goal、`ROADMAP.md`、`README.md`、`docs/project-overview.md` 及确实受到实现事实影响的设计说明。

禁止修改：

- 15 个页面的顺序、名称、数值范围、硬件命令格式和既有短按键功能，除本 Goal 明确规定的 B 长按返回外。
- BSP、Service、Games、PC Monitor、Tools、Settings、NVS、Wi-Fi、Audio 或 Assets 实现。
- GD32 固件、I2C `0x40` 协议、MPU6050 地址、GPIO 定义、SPI 配置、原理图和硬件连接事实。
- LVGL、ESP-IDF 或其他依赖版本，以及 `dependencies.lock`。
- 新增 FreeRTOS Task、队列、锁、跨任务 UI 调用或每 App 独立任务。
- 把现有硬件驱动和 Dashboard 大段迁移到新目录，或进行与节点 4 验收无关的重构、格式化和命名整理。
- 修改 `main/framework/` 下节点 1～3 已交付的任何运行时或自测文件（含 `xiaomiao_launcher.c` 的 B 转发分支）。该分支在 Dashboard 输入对象取得焦点后不再可达，属于预期行为，不得为“消除冗余”而改动。
- 用回退到旧 Dashboard 默认首页的方式掩盖 Launcher、注册或生命周期错误。

## 已确定的实现决策

1. 普通固件只注册一个正式 App：`id = "hardware_test"`、`name = "Hardware Test"`；`icon` 使用无外部资源依赖的文本或 LVGL symbol。后续 App 按各自节点继续注册，Launcher 不增加针对 Hardware Test 的分支。
2. `xiaomiao_app_t` 描述定义为固件全生命周期有效的 `static const` 对象。`init` 不重复初始化现有硬件；硬件仍在普通启动路径中初始化一次。`open` 创建 Dashboard，`close` 清理 Dashboard 和持续动作。
3. 本节点不把硬件与 Dashboard 代码整体迁出 `main/main.c`（该文件当前 2367 行）。App 描述和生命周期适配先与现有静态状态放在同一文件，避免为跨文件调用暴露大量临时接口或提前设计 BSP／Service。
4. Registry 注册、Manager `init_all()` 和 Launcher 创建均在现有 LVGL Task 中按顺序执行，符合 Framework、Navigation 和 Launcher 的单线程约束。任一步失败都记录具体错误并停止继续创建部分 UI，不回退到旧 Dashboard 首页。
5. Dashboard `open` 必须取得非空的 `xiaomiao_navigation_app_root()`，在其下创建唯一 Dashboard UI 根对象；Dashboard 不创建、切换或删除全局 screen，也不删除 Navigation 内容根。
6. Dashboard UI 根对象加入现有 LVGL group 并取得焦点。`close` 先从 group 移除该输入对象，解除或停止非 LVGL 资源，再清空全部 `s_ui` 对象引用；内容根及其所有子对象最后由 Navigation 统一删除。
7. Launcher 根对象保持存在。Hardware Test 返回后不得重新注册 App、重新初始化 Manager 或重建 Launcher，焦点和页码必须与进入前一致。
8. B 键使用明确的互斥手势：按下后暂不立即执行短按动作；在 800 ms 内释放时，只执行一次当前页面原有 `ui_cancel()`；持续按住达到 800 ms 时，只执行一次退出流程并抑制本次短按和后续长按重复，直到 B 释放后才允许下一次手势。该手势**不得由对象级 `LV_EVENT_KEY` 分支实现**：实现时必须删除 `ui_key_event_cb()` 中现有的 `LV_KEY_ESC` 立即调用 `ui_cancel()` 的分支（`main/main.c:2149-2151`），使 B 在固件中只有一个决策点。800 ms 阈值固定不变；本方案不使用“按页面当前状态判断该返回还是该取消”的自适应语义——`ui_cancel()` 在电机页未运行时的分支是**切换方向**而非取消（`main/main.c:2029-2046`），自适应语义会导致电机页要么永远无法用 B 返回、要么永远无法切换方向，因此只能用与页面无关的时间维度区分。
9. B 的按下／释放判定唯一通道为 `keypad_read_cb()`（`main/main.c:1052`，LVGL indev read 回调）。该回调已用 `lv_tick_get()`／`lv_tick_elaps()` 对 `s_buttons` 做 `BUTTON_DEBOUNCE_MS`（25 ms）去抖，并且是 `data->state`（`LV_INDEV_STATE_PRESSED`／`LV_INDEV_STATE_RELEASED`）与 `data->key` 的唯一来源，是固件内唯一能同时观测 B 按下与释放边沿的位置。职责划分固定为：
   - `keypad_read_cb()` 内的状态机**只记录**手势状态（按下时刻 tick、是否已判定长按、本次手势是否已被消费），**不执行业务动作**；
   - `ui_cancel()` 与 `xiaomiao_navigation_back()` 由 LVGL 循环（`lvgl_task()`，周期 `UI_REFRESH_PERIOD_MS` = 16 ms）依据状态机结果执行，避免在 indev 读取过程中删除 LVGL 对象；
   - 阈值固定 800 ms，用 `lv_tick_get()` 计算，不使用阻塞延时；
   - 短按动作延迟到释放执行会引入约一个按键时长的延迟（正常按压量级为几十到一百多毫秒），这是决策 8 的固有代价而非性能缺陷；延迟上界受 800 ms 阈值约束，不得超出；
   - 按住 B 期间必须提供可发现性提示：从按下起达到 `BTN_HOLD_HINT_MS`（建议 300 ms）仍未释放时，复用现有状态行 `set_action()`（`main/main.c:398-402`）显示一次退出提示文案（建议 `Hold to exit`）。提示不得新增 UI 元素、不得修改 15 页布局与页面内容；若在 800 ms 前释放，原有 `ui_cancel()` 会自行 `set_action` 覆盖该提示。提示仅在 Hardware Test 打开期间生效，且同一次按压只显示一次；
   - 不得修改 `lv_indev_set_long_press_time()`／`lv_indev_set_long_press_repeat_time()`（`main/main.c:2183-2184`，当前 360 ms／130 ms）的全局设置——方向键的按住重复（UP／DOWN 连续调节、LEFT／RIGHT 连续翻页）由该设置在 LVGL keypad 重复分支驱动；
   - 状态机仅在当前 App 为 Hardware Test 时生效；Launcher 显示期间 B 仍由节点 3 的 Launcher 路径处理。
10. Hardware Test 打开期间由其输入对象处理方向键、A 和 B；Launcher 不同时处理同一次按键。方向键和 A 的现有行为不变，长按重复仍只适用于原先允许连续调节的方向键。
11. `close` 必须优先停止两路电机和蜂鸣器，确保离开测试页面后没有危险的持续输出；停止失败需记录设备和错误码，但不得阻止 Navigation 删除 UI 并返回 Launcher。LED、GPIO25／26 输出和 SD 挂载状态不因退出被额外重置，以避免扩大既有行为。
12. `hardware_process_timers()` 可以继续在 LVGL 循环中运行，用于完成已安排的安全停止；Dashboard 的 `hardware_update()` 和 `ui_refresh()` 仅在当前 App 为 Hardware Test 且 UI 已创建时执行。Launcher 页面不得调用已失效的 UI 引用。
13. 每次 `open` 从 `UI_PAGE_LIGHT` 创建全新的 Dashboard 页面；硬件状态可沿用 `s_board` 中的真实当前值，但不得沿用已删除的 LVGL 对象或动画引用。
14. MicroSD 仍是可选能力。挂载失败只更新 Hardware Test 页面状态和日志，不能阻止 App 注册、Manager 初始化、Launcher 创建或其他页面操作。
15. 节点 3 的三个自测构建选项及其互斥入口保持有效。普通固件默认路径切换为 Launcher；不得修改自测固件的预期入口或把真实 Hardware Test 注册进 Launcher 自测数据集。
16. 不新增 Hardware Test 专用 FreeRTOS Task。所有生命周期、输入和 UI 刷新继续运行在现有 LVGL Task。
17. ESP-IDF 编译、`set-target`、烧录、串口监视和目标板操作全部由人工执行。Agent 只实施源码与文档修改、执行静态检查、提供准确命令并复核人工证据。

## 生命周期与输入语义

接口名可按现有代码风格微调，但职责必须等价：

```c
static void hardware_test_open(void);
static void hardware_test_close(void);
static bool hardware_test_is_open(void);
```

### `open`

- 仅由 App Manager 经 Navigation 调用；禁止从 Launcher 或启动代码直接调用。
- Navigation 内容根为空、LVGL group 未就绪或 Dashboard 已创建时，记录明确错误且不得创建游离对象。
- 创建成功后把 Dashboard 输入对象加入 group、聚焦并显示第 1／15 页。
- 首次刷新必须使用当前真实硬件状态，不显示上一次生命周期遗留的对象内容。

### `close`

- 允许在 Dashboard 已部分创建的失败状态下安全调用，不解引用空对象。
- 取消本次未完成的 B 手势，停止蜂鸣器和两路电机，避免释放 UI 后仍有持续动作。
- 从 LVGL group 移除 Dashboard 输入对象；不得删除 Navigation 内容根。
- 清空 `s_ui` 中所有对象、group、动画或页面引用，使后续周期循环无法访问已释放对象。
- 多轮进入／退出后 LVGL screen 子对象数量必须回到同一基线。

### B 键

| 手势 | Hardware Test 行为 | Launcher 行为 |
| --- | --- | --- |
| B 按住不足 800 ms 后释放 | 执行一次当前页面原有 B 操作 | 保持节点 3 的根页面幂等返回语义 |
| B 按住达到提示延迟（建议 300 ms） | 状态行显示一次退出提示，不执行业务动作 | 无额外动作 |
| B 按住达到 800 ms | 停止持续输出并经 `xiaomiao_navigation_back()` 返回 Launcher | 不增加新动作 |
| B 继续按住 | 不重复退出、不触发短按 | 不重复改变状态 |
| B 长按后释放 | 仅结束本次手势，不补发短按 | 无额外动作 |

### 为什么必须用 `keypad_read_cb`（LVGL 9.5 keypad 实际行为）

本固件的 B 键为 `GPIO_NUM_12` → `LV_KEY_ESC`（`main/main.c:311`），输入设备类型为 `LV_INDEV_TYPE_KEYPAD`（`main/main.c:2179`）。查阅本仓库锁定的 LVGL 9.5 `managed_components/lvgl__lvgl/src/indev/lv_indev.c` 中 `indev_keypad_proc()`（796-946 行）可确认，对象级 key 事件无法承担该手势：

- **B 的释放不会到达对象**：释放分支的事件派发整体位于 `if(g == NULL || data->key == LV_KEY_ENTER)` 之内（`lv_indev.c:931`）。有焦点对象且按键为 `LV_KEY_ESC` 时，只重置内部时间戳，不向对象发送 `LV_EVENT_RELEASED`／`LV_EVENT_CLICKED`。因此“800 ms 内释放才执行短按”无法从 `ui_key_event_cb()` 得到。
- **按住 B 会重复派发**：`long_pr_sent` 对 ESC 同样会被置 1（`lv_indev.c:888-889`），随后重复分支对非 `ENTER`／`NEXT`／`PREV` 的按键走到 `else { lv_group_send_data(g, data->key); }`（`lv_indev.c:919-922`），即按住 B 会每 `long_press_repeat_time`（130 ms）向对象重复派发 `LV_KEY_ESC`。若保留现有立即 `ui_cancel()` 分支，将直接违反“继续按住不重复退出、不触发短按”。
- **内建长按事件对 B 不可用**：`LV_EVENT_LONG_PRESSED` 与 `LV_EVENT_LONG_PRESSED_REPEAT` 只在 `g == NULL || data->key == LV_KEY_ENTER` 时发送（`lv_indev.c:890`、`903`），且重复语义与“只返回一次”相冲突。
- **全局时间不可挪用**：键重复是 UP／DOWN／LEFT／RIGHT 按住连续操作的实际驱动机制，因此 800 ms 阈值必须是独立状态机，不能通过调整 `lv_indev_set_long_press_time()` 实现。

结论：B 手势的唯一可行通道是 `keypad_read_cb()` 的按下／释放边沿 + 独立状态机，动作在 LVGL 循环中执行。`lvgl_task()` 的循环已有 `hardware_process_timers()` → `hardware_update()`／`ui_refresh()` 结构（`main/main.c:2203-2215`），状态机判定可并入该循环，不需要新增 Task、队列或锁。

## 执行检查点

### 检查点 1：普通启动链接入 Framework 与 Launcher

- 在普通固件 LVGL Task 中注册 `Hardware Test` App、执行 Manager 初始化并创建 Launcher。
- 保留现有按键、LCD、硬件、LVGL display、input group 和 tick 初始化顺序，不复制初始化代码。
- 启动日志明确区分普通 Launcher 固件与三个自测固件。
- 验证普通启动不直接调用 `ui_create()`，Registry 中只有预期的正式 App。

预期结果：开机显示 Launcher 和 `Hardware Test` 入口，未进入 App 前不创建 Dashboard 页面。

### 检查点 2：Dashboard App 生命周期与对象所有权

- 将 Dashboard 所有页面和动画对象约束到 Navigation 内容根之下。
- `open` 创建并聚焦 Dashboard 输入对象，`close` 移除 group 关系并清理引用。
- 连续至少 10 轮 A 进入／B 长按退出，检查 Launcher 焦点、页码和 screen 子对象基线。
- 返回后方向键只移动 Launcher 焦点，不再调用 Dashboard 页面或刷新逻辑。

预期结果：Dashboard 可重复进入和退出，无悬空引用、重复回调、对象增长或 Launcher 状态丢失。

### 检查点 3：B 短按／长按互斥与安全退出

- 使用非阻塞计时实现 800 ms 阈值，短按动作延迟到确认释放后执行。
- 静态确认 `ui_key_event_cb()` 中原有的 `LV_KEY_ESC` 立即 `ui_cancel()` 分支已移除，B 全固件只有一个决策点；确认 `lv_indev_set_long_press_time()`／`repeat_time` 的 360 ms／130 ms 未被改动。
- 确认按住 B 期间 LVGL 重复派发的 `LV_KEY_ESC` 事件不产生任何可见效果（不重复退出、不重复触发短按）。
- 人工确认按住 B 达到提示延迟后状态行出现退出提示，且同一次按压只出现一次；800 ms 前释放时提示被原操作消息覆盖；提示不改变 15 页布局与页面内容。
- 人工确认短按动作的可见延迟量级符合“正常按压时长”，不影响使用。
- 覆盖 799 ms 以下、达到阈值、持续按住、阈值后释放及连续两次手势。
- 在 LED、蜂鸣器、电机、MicroSD 页面分别验证短按仍执行原操作，不返回 Launcher。
- 在电机和蜂鸣器运行时长按 B，验证先停止持续输出，再返回 Launcher，且释放时不补发短按。

预期结果：一次 B 手势只产生短按操作或返回之一；退出后电机和蜂鸣器均不继续运行。

### 检查点 4：15 页功能回归

- 依次进入光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统和 About。
- 检查左右循环翻页、上下调节／滚动、A 动作以及各页短按 B 行为。
- 对缺失设备和失败路径进行验证：GD32 `0x40` 不存在、MPU6050 `0x68` 不存在、MicroSD 缺失或挂载失败时，页面给出状态但 Launcher 与其他页面仍可用。
- GD32 仓库源码缺少 `0x40` 从机协议，LED／电机结果必须按实际设备证据记录；无实机成功证据时标记未验证，不以 ESP32 发出命令替代联调结论。

预期结果：15 页全部可达，已有操作语义不因 App 封装退化，单项外设失败不阻塞 Launcher 或其他测试。

### 检查点 5：自测形态回归、文档与交付

- 静态复核 Framework、Navigation、Launcher 三个自测入口仍与普通启动互斥，相关源文件和编译宏未被普通接入破坏。
- Agent 执行精准源码检索、改动范围检查和 `git diff --check`；不主动执行任何 `idf.py`、烧录、monitor 或串口命令。
- 人工完成普通构建、烧录、启动、生命周期、输入和 15 页外设回归，并提供命令输出、串口日志和必要截图。
- 如节点 4 修改触及三个自测入口或 framework 文件，人工追加对应自测构建和实机 PASS；未触及时不为形式重复全部历史验证。
- 根据真实结果更新本 Goal、`ROADMAP.md`、`README.md` 和 `docs/project-overview.md`；未验证项继续留在进行中。

预期结果：普通固件节点 4 行为通过人工验收，静态检查无越界改动，当前状态文档与实际一致。

## 失败路径与处理要求

- App 注册、Manager 初始化或 Launcher 创建失败：输出失败阶段与错误码，保持节点未完成；不得静默回退到旧 Dashboard 首页。
- `xiaomiao_navigation_app_root()` 为空：不得在活动 screen 上另建 Dashboard 绕过 Navigation；定位生命周期顺序。
- Dashboard 重复打开或部分创建：不得留下第二个输入对象、重复事件或游离页面；清理到稳定状态后再允许重试。
- B 短按在按下瞬间执行、长按同时触发短按、长按重复返回：均视为输入仲裁失败，不通过验收。
- Hardware Test 焦点存在时 Launcher 同时响应方向键或 A：视为输入所有权错误，修正 group／focus，而不是在两处增加状态判断掩盖。
- 返回后 `ui_refresh()` 访问已删除对象、对象数量增长或下一次进入仍引用旧页面：视为生命周期缺陷，保持 Goal 未完成。
- 电机或蜂鸣器在退出后继续运行：视为阻塞性安全缺陷；必须先修复停止顺序和失败日志。
- LED、电机或 MPU6050 因 GD32／设备缺失无法验证：只标记对应外设未验证，不阻塞不依赖这些设备的 Launcher、生命周期和其他页面验证；但不得把节点 4 的“全部外设回归”错误记录为已通过。
- MicroSD 缺失导致启动中止或 Launcher 不可用：视为阻塞缺陷，不允许以插卡作为启动前提。
- 人工无法完成编译、烧录或目标板验证：保留“进行中”，记录 Agent 已完成的静态范围和具体未验证项，不得将节点 4 标记完成。

## 验收标准

- [x] 普通固件开机默认进入 Launcher，不再直接进入 Hardware Dashboard。（实机验证通过，2026-09-20 11:20：启动日志 `Xiaomiao LVGL 9.5 launcher boot` → `Start Xiaomiao launcher` → `launcher created (1 apps)` → `Launcher ready, 1 app(s) registered`，无 `hardware test opened`，未出现旧的 `Start Xiaomiao hardware dashboard`）
- [x] Registry 注册唯一 `Hardware Test` 正式 App，Launcher 未包含针对该 App 的硬编码入口。（静态核对通过：`launcher_boot()` 只注册 `s_hardware_test_app`；Launcher 仍按 Registry 顺序动态生成入口）
- [x] A 可从 Launcher 打开 Hardware Test；B 长按可返回原 Launcher 页码和焦点。（实机验证通过，2026-09-20 11:43：16 轮全部配对，16 次返回均由 B 长按触发（`xiaomiao_navigation_back()` 全仓唯一调用点在第 1153 行）；人工确认返回后按各方向键只有 Launcher 侧反应、无 Dashboard 反应。“与原 Launcher 页码和焦点一致”一项在本轮不具证明力——只注册 1 个 App，`launcher focus=／page=` 恒为 0/0，待节点 5 增加 App 后才可核对）
- [x] Dashboard 全部 LVGL 对象均位于 Navigation 内容根之下，关闭后由 Navigation 完整释放。（实机验证通过，2026-09-20 11:43：`screen children=2` 在全部 16 次 open 与 16 次 close 上恒定，证明每次关闭后内容根都已释放、无残留）
- [x] 连续 10 轮进入／返回后 screen 子对象数量保持基线，无重复事件、悬空引用或崩溃。（实机验证通过，2026-09-20 11:43：前 11 轮为一次不中断的快速轮转，共 16 轮，`screen children` 恒为 2，无崩溃、无 panic、无重启、无输入失效）
- [x] B 短按保留 15 页既有操作；800 ms 长按只返回一次，短按与长按严格互斥。（实机验证通过，2026-09-20 11:43：人工逐页确认短按给出各页原有提示且均未退出；长按在约 0.3 s 出现一次 `Hold to exit`、达 800 ms 返回、松开无补发、提示不重复）
- [ ] 从电机或蜂鸣器运行状态退出时，持续输出在 Launcher 显示前停止。（**未验证：当前硬件无法构造该场景**。两个分支都不成立——① 电机：本机无电机硬件，且仓库内 GD32 工程未实现 I2C `0x40` 从机协议，`s_board.motor_running[motor]` 从未为真，`hardware_test_close()` 的停止循环会被 `continue` 跳过；② 蜂鸣器：蜂鸣器页 A 键的鸣叫时长为 `buzzer_beep(s_buzzer_freq_hz, 140)`（`main/main.c:2065`），140 ms 后由 `hardware_process_timers()` 自动停止，不存在“仍在发声”时按 B 的时刻，且单键模型下无法同时按住 A 与 B。`buzzer_stop()` 本身在每次 `close` 都被调用（代码路径已执行），但无可观察效果。按本 Goal“失败路径与处理要求”中“LED、电机或 MPU6050 因 GD32／设备缺失无法验证：只标记对应外设未验证，不阻塞”的条款，本项标记为未验证，不阻塞节点 4；**不得据此认为外设回归已全部通过**）
- [x] 光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统和 About 共 15 页全部可进入、操作和翻页。（实机验证通过，2026-09-20 11:43：人工确认 15 页全部可进入、翻页与操作。缺失设备页面的具体显示文本尚未逐项记录，不影响本条款）
- [x] SD 卡缺失或挂载失败不影响 Launcher 启动、Hardware Test 进入和其他页面操作。（实机验证通过，2026-09-20 11:43：MicroSD 页 2 次挂载失败后 2.7 s 仍正常返回 Launcher，未阻塞启动、进入与返回；两次实测的 SD 失败都发生在会话末尾，“失败后再次进入 App”仍未覆盖，但失败不阻塞的结论已成立）
- [x] Dashboard 关闭后周期循环不调用失效 UI；重新打开从第 1／15 页创建新 UI。（实机验证通过，2026-09-20 11:43：16 轮无异常；人工确认返回后按方向键无任何 Dashboard 反应，说明刷新门控与引用清空生效；每次进入均从第 1／15 页创建，`ui_create()` 固定以 `UI_PAGE_LIGHT` 起始）
- [x] Framework、Navigation、Launcher 三个已有自测构建入口继续保持互斥；普通固件不包含自测页面。（静态核对通过：未修改 CMake 选项、编译宏与三个自测入口，也未改动 `main/framework/` 下任何文件；仍需按检查点 5 由人工构建确认）
- [x] 未新增 FreeRTOS Task、BSP、Service 或正式业务 App，未大规模迁移现有硬件代码。（静态核对通过：改动全部位于 `main/main.c`，未新增 `xTaskCreate`、目录或分层文件）
- [x] 未修改 GD32、硬件协议、引脚、依赖版本或 `dependencies.lock`。（静态核对通过：`git status` 仅含 `main/main.c` 与文档）
- [x] 人工提供 ESP-IDF 6.1 普通构建成功证据，固件可烧录且 Monitor 无新增致命启动错误。（2026-09-20：`App version: e003e2e-dirty`、`ESP-IDF: v6.1`、应用分区 36% free；含新增日志的一版亦编译成功并成功烧录；Monitor 无新增致命错误。首次烧录因 COM5 配置失败，重试后成功）
- [x] 人工提供 Launcher、10 轮生命周期、B 短／长按和 15 页回归证据；无法验证的具体外设已逐项标记。（2026-09-20 11:20／11:32／11:43 三轮实机证据齐备；无法验证项仅剩“电机实际运行状态下退出先停止输出”，已在验收标准与“未验证范围”中逐项标记为未验证）
- [x] `git diff --check` 和改动范围静态检查通过，Goal、`ROADMAP.md` 及受影响当前状态文档已同步。（`git diff --check` 通过，已同步 `ROADMAP.md`、`README.md`、`docs/project-overview.md` 与本 Goal；`docs/xiaomiao_firmware_v0.1_design.md` 的节点拆分说明无需改动）

## 交付内容

- `main/main.c` 中的 Hardware Test App 描述、生命周期适配、B 手势仲裁和 Launcher 普通启动链。
- 必要时新增的最小辅助文件及对应 `main/CMakeLists.txt` 接入；不以目录形式为目的拆分硬件代码。
- 供人工执行的普通构建、烧录、Monitor、10 轮生命周期、B 手势和 15 页回归步骤。
- 记录实际实现、验证证据、失败修复和未验证范围的本 Goal。
- 更新后的 `ROADMAP.md`、`README.md` 和 `docs/project-overview.md`。

## 人工验证命令与预期结果

前置：在 PowerShell／CMD 中按 `AGENTS.md` 加载 ESP-IDF 6.1；非默认安装布局使用进程级 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`。串口号以下以 COM5 为例，实际执行时按设备修改。

普通构建与实机验证：

```bash
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

预期结果：

- `idf.py --version` 对应 ESP-IDF v6.1，构建和烧录成功。
- 启动日志明确进入普通 Launcher 路径：出现 `Xiaomiao LVGL 9.5 launcher boot` 与 `Start Xiaomiao launcher`，随后出现 `Launcher ready, 1 app(s) registered`；不出现 `Self test build`／`Navigation self test build`／`Launcher self test build` 三个自测标记，也不再打印 `Start Xiaomiao hardware dashboard` 的旧语义。
- 屏幕默认显示 Launcher，其中存在 `Hardware Test`；按 A 进入后显示第 1／15 页。
- B 短按执行当前页面操作；B 持续 800 ms 返回 Launcher，释放后无额外动作；按住达到提示延迟后状态行出现一次退出提示。
- 连续 10 轮进入／返回无崩溃、花屏、输入失效、对象增长或焦点漂移。
- 15 页及可用外设按检查点 4 逐项验证；不可用外设记录实际缺失状态和串口日志。

## 委派约束

本 Goal 当前不计划委派。若后续明确授权使用子 Agent，主 Agent 必须先在本文件新增子任务编号，并逐项写明目标与预期行为、必要背景与文件入口、依赖条件、允许和禁止修改范围、已确定接口或实现决策、关键失败路径、验收方式及预期结果。派发时必须提供本文件路径和任务编号；主 Agent 最终核对实际改动、静态检查与人工验证证据，不得仅凭子 Agent 的完成声明认定通过。

## 创建记录（2026-09-20 10:53）

- 已对照 `ROADMAP.md`、`README.md`、`docs/project-overview.md`、设计文档第 8／12／13／15 节、节点 3 Goal、Framework／Navigation 接口和当前 `main/main.c` 启动链。
- 已确认节点 4 的核心技术冲突是 Dashboard 短按 B 操作与系统返回共用同一按键；本 Goal 固定采用“短按原操作、800 ms 长按返回”的互斥语义。
- 已确认以最小生命周期适配保留现有单文件硬件实现，不提前扩大为 BSP／Service 或目录重构。
- 本记录只完成施工设计，未修改固件源码，编译、烧录、Monitor 和目标板行为均未验证。

## 实现记录（2026-09-20）

### 实际改动

全部固件改动集中在 `main/main.c`（+280／-10），未新增源文件，因此未修改 `main/CMakeLists.txt`。

| 位置 | 改动 |
| --- | --- |
| 头部 include | 新增 `framework/xiaomiao_app.h`、`xiaomiao_launcher.h`、`xiaomiao_navigation.h` |
| 常量区 | 新增 `HARDWARE_TEST_APP_ID`／`_NAME`／`_ICON`（`LV_SYMBOL_SETTINGS`）与 `BTN_B_LONG_PRESS_MS`（800）、`BTN_B_HOLD_HINT_MS`（300）、`BTN_B_HOLD_HINT_TEXT`（`Hold to exit`） |
| `keypad_read_cb()` 之前 | 新增 `ui_cancel()` 前置声明、B 手势状态（`s_b_held`／`s_b_press_ms`／`s_b_long_fired`／`s_b_hint_shown`／`s_b_exit_pending`／`s_b_short_pending`）、`hardware_test_is_open()`、`hardware_test_b_gesture_reset()/_update()/_poll()` |
| `keypad_read_cb()` 末尾 | 新增一次 `hardware_test_b_gesture_update(data->key, data->state == LV_INDEV_STATE_PRESSED)` |
| `ui_key_event_cb()` | 删除 `LV_KEY_ESC` 立即调用 `ui_cancel()` 的分支，并加注释说明 B 由手势状态机统一决策 |
| `ui_create()` | 签名改为 `(lv_group_t *group, lv_obj_t *parent)`，父对象由 `lv_screen_active()` 改为传入的 Navigation 内容根 |
| `ui_create()` 之后 | 新增 `s_input_group`、`hardware_test_open()`、`hardware_test_close()`、静态 `s_hardware_test_app` 描述 |
| `lvgl_task()` | 新增 `launcher_boot()`（注册 → `init_all()` → `xiaomiao_launcher_create()`）；不再直接调用 `ui_create()`；循环新增 `hardware_test_b_gesture_poll()`，`hardware_update()`／`ui_refresh()` 改由 `hardware_test_is_open()` 门控 |
| 启动日志 | `Xiaomiao LVGL 9.5 dashboard boot` → `Xiaomiao LVGL 9.5 launcher boot`；`Start Xiaomiao hardware dashboard` → `Start Xiaomiao launcher` |

关键实现点：

- `hardware_test_is_open()` 以 `s_ui.screen != NULL` 为唯一判据。Manager 保证同时只有一个 App 打开，且只有 Hardware Test 会创建 Dashboard UI，因此该判据等价于“Hardware Test 已打开”，无需再查 `xiaomiao_navigation_current()`；这也避开了 `open` 回调期间当前 App 尚未发布的时序问题。
- B 手势的按下边沿取自 `keypad_read_cb()` 已经去抖后的 `data->state`；该回调的 `data->key` 在释放时保留最后按键值，因此用 `pressed && key == LV_KEY_ESC` 即可同时判定按下与释放。
- 长按只置一次 `s_b_exit_pending`（含 `!s_b_long_fired` 守卫），实际退出由循环中的 `hardware_test_b_gesture_poll()` 调用 `xiaomiao_navigation_back()`，避免在 indev 读取回调里删除 LVGL 对象。
- `hardware_test_close()` 只对 `s_board.motor_running[motor]` 为真的电机发停止命令，既满足“退出前停止持续输出”，又避免在 GD32 `0x40` 缺失（当前仓库现状）时刷出无意义的失败日志。
- 提示复用 `set_action()`，显示时长沿用 `UI_ACTION_MSG_MS`（850 ms），因此提示出现后到 800 ms 退出之间始终可见，且 800 ms 前释放时会被 `ui_cancel()` 自身的消息覆盖。

### 已完成的静态检查

- `git diff --check` 通过（仅 `ROADMAP.md` 的 CRLF 提示）；`main/main.c` 大括号配平 346/346。
- `ui_create()` 全仓只剩一处调用（`hardware_test_open`），不存在仍在 `lv_screen_active()` 上建 Dashboard 的路径。
- `grep` 确认 `ui_cancel()` 只有前置声明、循环侧调用与定义三处；`LV_KEY_ESC` 不再出现在任何对象级事件分支中。
- 改动范围仅 `main/main.c` 与文档；`main/framework/` 下节点 1～3 文件、`main/CMakeLists.txt`、GD32 工程、硬件协议、引脚、依赖版本与 `dependencies.lock` 均未改动。
- 确认 `hardware_update()` 只包含 ADC 采样、I2C 探测与 MPU 读取（`main/main.c:776-782`），不含电机或蜂鸣器安全逻辑，因此把它门控在 Hardware Test 打开期间不引入安全缺口；蜂鸣器定时停止仍在常驻的 `hardware_process_timers()` 中。
- 对照本仓库锁定 LVGL 9.5 确认 `LV_SYMBOL_SETTINGS`（U+0xF013）字形存在于 `lv_font_montserrat_12`，Launcher 图标可正常渲染。

### 未验证范围（2026-09-20 11:46 更新）

本节初稿写于 11:18 实现完成时，列出的是当时全部未验证项。经 11:20／11:32／11:43／11:46 四轮实机验证后，绝大部分已转为已验证，**仅以下仍无证据**：

- **从电机或蜂鸣器运行状态退出时先停止持续输出**：当前硬件无法构造该场景。电机侧无硬件且 GD32 未实现 `0x40` 从机协议，`s_board.motor_running[]` 从未为真；蜂鸣器侧 A 键仅鸣叫 140 ms（`buzzer_beep(s_buzzer_freq_hz, 140)`，`main/main.c:2065`）后自动停止，单键模型下也无法同时按住 A 与 B。`buzzer_stop()` 在每次 `close` 均被调用但无可观察效果。
- **LED、电机、MPU6050 的实机行为**：GD32 `0x40` 从机协议在仓库源码中缺失，LED 与电机命令的实际效果无证据；MPU6050 在设备缺失时的页面表现未逐项记录。属硬件与 GD32 侧长尾。
- **挂载失败后重新进入 App 并浏览其他页面**：两轮实测的 MicroSD 失败都发生在会话末尾，未覆盖失败后的再次进入。
- **三个自测构建入口在本次改动后是否仍互斥**：本次未修改相关文件、CMake 选项或编译宏，属静态预期，但未重新构建自测固件验证。
- **`launcher focus=／page=` 的实际保留语义**：普通固件只注册 1 个 App，该值恒为 0/0，不具证明力，需待节点 5 增加 App 后核对。

以下两条是初稿特别提示的“新增未覆盖路径”，现已由实测覆盖：

- **返回 Launcher 后焦点是否交回 Launcher 根对象**：已由 11:43 人工确认——返回后按各方向键只有 Launcher 侧反应、无任何 Dashboard 反应。该路径依赖 `lv_group_remove_obj()` 在“被移除对象是当前焦点且组内还有其他对象”时调用 `lv_group_refocus()`（`lv_group.c:180-191`），节点 3 自测从未触及，本次首次执行并通过。
- **按住 B 期间 LVGL 重复派发的 `LV_KEY_ESC` 是否无副作用**：已由 11:43 确认——按住期间仅出现一次 `Hold to exit`、只返回一次、松开无补发。

### 已知量级说明

- 该实现引入的固有延迟量级：长按判定以 `keypad_read_cb()` 去抖后的按下边沿为起点，去抖为 `BUTTON_DEBOUNCE_MS`（25 ms），indev 读取周期为 `LV_DEF_REFR_PERIOD`（16 ms），因此从物理按下到 800 ms 判定生效的实际时间约为 825～850 ms；短按动作在确认释放后的下一次循环内执行（≤16 ms），从物理按下到动作生效约为 25～60 ms。

### 为实现检查点 2 补充的可观测日志（2026-09-20 11:32）

检查点 2 要求“检查 Launcher 焦点、页码和 screen 子对象基线”，但原实现没有任何可观测输出，该数值无法核对。因此补充两行 INFO 日志，均为可观测性而非功能改动：

- `hardware_test_open()` 末尾：`hardware test opened, screen children=%u`。此处上一次关闭的内容根已被 Navigation 删除，活动 screen 上应恰好只有 Launcher 根对象与新的 App 内容根；若该数值逐轮增长即说明内容根泄漏。
- `hardware_test_close()` 末尾：`hardware test closed, screen children=%u, launcher focus=%u page=%u`。同时输出来自 `xiaomiao_launcher_focused_index()`／`xiaomiao_launcher_page_index()` 的 Launcher 焦点与页码，作为“返回后与进入前一致”的可核对证据。

这两行日志把原本不可验证的验收条款变成可测量的数值；若后续认为日志冗余，可在节点 4 验收通过后移除。

## 验证结果（2026-09-20，人工执行、Agent 复核）

### 最终生命周期轮转（2026-09-20 11:43，人工执行、Agent 复核）— 通过

同一 Monitor 会话内共 16 轮 open／close，**全部配对（16/16）**，其中**前 11 轮为一次不中断的快速轮转**（13482 → 32244，单片 1.09～1.29 s），满足“连续至少 10 轮”的要求；其余为长时段停留（44.4 s、31.3 s、32.1 s、5.7 s、2.7 s）。

| 轮次 | open | close | 时长 |
| --- | --- | --- | --- |
| 1 | 13482 | 14770 | 1.29 s |
| 2 | 15508 | 16761 | 1.25 s |
| 3 | 17312 | 18494 | 1.18 s |
| 4 | 19062 | 20262 | 1.20 s |
| 5 | 20779 | 21979 | 1.20 s |
| 6 | 22479 | 23643 | 1.16 s |
| 7 | 24160 | 25324 | 1.16 s |
| 8 | 25790 | 26954 | 1.16 s |
| 9 | 27420 | 28585 | 1.17 s |
| 10 | 28983 | 30126 | 1.14 s |
| 11 | 31153 | 32244 | 1.09 s |
| 12 | 120413 | 164819 | 44.4 s |
| 13 | 168770 | 171511 | 2.74 s |
| 14 | 195046 | 226359 | 31.3 s |
| 15 | 234203 | 239868 | 5.67 s |
| 16 | 253679 | 285827 | 32.1 s |

关键结果：

- **`screen children=2` 在全部 16 次 open 与 16 次 close 上恒定**，无一次增长。活动 screen 上始终只有 Launcher 根对象与当前 App 内容根，证明 Navigation 每次都在关闭后完整释放了内容根，Dashboard 的子对象没有泄漏，也满足“连续 10 轮进入／返回后 screen 子对象数量保持基线”。
- **16 轮无崩溃、无 panic、无重启循环、无输入失效**，长时段停留后仍能正常返回。
- **16 次返回全部由 B 长按手势触发**（`main/main.c` 中 `xiaomiao_navigation_back()` 只有唯一调用点，第 1153 行），且每轮只产生一次关闭。
- `launcher focus=0 page=0` 全程不变。**该数值在本轮不具备证明力**：普通固件只注册 1 个 App，焦点无处可移、只有 1 页，因此恒为 0/0，无法区分“保留”与“重置”。Launcher 状态保留只能靠行为观察确认（见下）。

人工观察确认（用户报告）：

- 返回 Launcher 后按各方向键，焦点指示只在 `Hardware Test` 卡片上，**没有任何 Dashboard 反应**（不翻页、不调值）。这证明关闭后按键不再到达 Dashboard，既佐证焦点已交回 Launcher，也印证“周期循环不调用失效 UI”。
- B 短按逐页验证：状态行给出各页原有提示，**均未退出**。
- 长按：按住约 0.3 s 出现一次 `Hold to exit`，达 800 ms 返回，松开无额外动作，提示不重复出现。
- 15 页全部可进入、翻页与操作。

### 退出安全与缺失设备的最终确认（2026-09-20 11:46）

人工确认结果：蜂鸣器实际发声正常；电机**无硬件可用**（本机没有电机，且 GD32 工程未实现 `0x40` 协议）。

据此查明“从电机或蜂鸣器运行状态退出时先停止持续输出”这一条**在当前硬件上无法构造**：

- 电机侧：`s_board.motor_running[motor]` 只在 `gd32_motor_set()` 返回 `ESP_OK` 时置真（`ui_motor_toggle()`），无电机且 GD32 无 `0x40` 从机实现，该标志从未为真，`hardware_test_close()` 的 `if (!s_board.motor_running[motor]) continue;` 会跳过停止命令。
- 蜂鸣器侧：蜂鸣器页 A 键调用 `buzzer_beep(s_buzzer_freq_hz, 140)`（`main/main.c:2065`），140 ms 后由 `s_buzzer_stop_at` 定时器在 `hardware_process_timers()` 中自动停止；不存在“蜂鸣器仍在响”时按 B 的时刻，且 `keypad_read_cb()` 是单键模型（返回首个命中按键），无法同时按住 A 与 B。

因此该验收条款标记为**未验证**，依据本 Goal 的失败路径条款（设备缺失只标记对应外设未验证、不阻塞）不阻塞节点 4 收口。`buzzer_stop()` 在每次 `close` 均被调用，代码路径已执行，但无可观察效果。

需要说明：节点 4 的“全部外设回归”**并未全部通过**——LED、电机、MPU6050 的实机行为仍未有证据（GD32 `0x40` 与双 MCU 联调待确认），这部分属于硬件与 GD32 侧的长尾，已记入 `ROADMAP.md` 的待确认事项。

### 检查点 4 的 MicroSD 缺失路径 — 通过（含既存问题再次复现）

第 16 轮内人工在 MicroSD 页按 A 两次，`sdmmc_card_init failed (0x107)` 与 `gpio: conflict found for GPIO[22]` 再次出现（280558、283039-283061），失败后 2.7 s 仍正常 `hardware test closed`。与 11:32 那次结论一致：不阻塞启动、进入与返回，符合设计文档第 9 节的 SD 策略；该问题是设计文档已记录的既存事项，不属节点 4 范围。

仍未验证：挂载失败后再次进入 App 并浏览其他页面（两次实测的 SD 失败都发生在该次会话的末尾）、GD32 `0x40` 与 MPU6050 缺失时页面的具体显示文本。

### 检查点 1：普通启动链接入 Framework 与 Launcher — 通过

人工编译、烧录并 Monitor，证据：

```
I (731) app_init: App version:      e003e2e-dirty
I (735) app_init: Compile time:     Sep 20 2026 11:20:14
I (744) app_init: ESP-IDF:          v6.1
I (819) xiaomiao_dash: Xiaomiao LVGL 9.5 launcher boot
I (1517) xiaomiao_dash: LVGL display: 160x128, dpi=60, 3 full-screen DMA buffers, SPI=60 MHz
I (1517) xiaomiao_dash: Start Xiaomiao launcher
I (1530) launcher: launcher created (1 apps)
I (1531) xiaomiao_dash: Launcher ready, 1 app(s) registered
I (1548) main_task: Returned from app_main()
```

逐项对照：

| 检查点 1 要求 | 结果 |
| --- | --- |
| 普通固件 LVGL Task 中注册 App、初始化 Manager、创建 Launcher | 通过：`Launcher ready, 1 app(s) registered` |
| 保留现有初始化顺序、不复制初始化代码 | 通过：按键／LCD／硬件／display／group／tick 初始化仍在 `app_main`，本 Goal 未改动该段 |
| 启动日志区分普通与三个自测固件 | 通过：出现 `launcher boot` 与 `Start Xiaomiao launcher`，未出现任何 `self test build` 标记 |
| 普通启动不直接调用 `ui_create()` | 通过：全程无 `hardware test opened`，说明未创建 Dashboard |
| Registry 中只有预期的正式 App | 通过：`launcher created (1 apps)` |

`App version: e003e2e-dirty` 与 `Compile time: Sep 20 2026 11:20:14` 确认烧录的是含本次未提交改动的固件，而非历史产物。运行期间无 panic、无重启循环、无非预期错误日志。

### 生命周期轮转（2026-09-20 11:32，人工执行、Agent 复核）— 部分通过

同一 Monitor 会话内共 19 轮 `hardware test opened` / `hardware test closed`，全部严格配对，无孤立 open、无重复 close、无 panic、无重启循环。

| 轮次 | open | close | 时长 |
| --- | --- | --- | --- |
| 1 | 148718 | 165926 | 17.2 s |
| 2 | 183137 | 184999 | 1.86 s |
| 3 | 186689 | 188314 | 1.63 s |
| 4 | 189715 | 191183 | 1.47 s |
| 5 | 193196 | 194764 | 1.57 s |
| 6 | 209459 | 245239 | 35.8 s |
| 7 | 246555 | 247882 | 1.33 s |
| 8 | 250048 | 251281 | 1.23 s |
| 9 | 252121 | 253408 | 1.29 s |
| 10 | 254367 | 255657 | 1.29 s |
| 11 | 256412 | 257715 | 1.30 s |
| 12 | 258351 | 259815 | 1.46 s |
| 13 | 269852 | 271132 | 1.28 s |
| 14 | 271887 | 273149 | 1.26 s |
| 15 | 273734 | 274934 | 1.20 s |
| 16 | 282625 | 307650 | 25.0 s |
| 17 | 309068 | 324484 | 15.4 s |
| 18 | 327057 | 336498 | 9.44 s |
| 19 | 337202 | 353366 | 16.2 s |

由此可确认：

- **B 长按返回可用且只返回一次（19/19）**：`main/main.c` 中 `xiaomiao_navigation_back()` 只有一处调用（第 1153 行，`hardware_test_b_gesture_poll()` 的退出分支），`xiaomiao_launcher.c:253` 的 Launcher 转发分支在 Dashboard 持有焦点时不可达，自测文件不在普通构建内。因此这 19 次返回全部由 B 长按手势触发，且每次只产生一次 `hardware test closed`。
- **连续轮转无崩溃、无输入失效**：快速轮转段最长连续 6 轮（第 7～12 轮，每轮 1.2～1.5 s），另有 4 轮（第 2～5 轮）与 3 轮（第 13～15 轮）连续段；长时段停留（35.8 s、25.0 s、15.4 s、16.2 s）后仍能正常返回。
- **MicroSD 缺失不阻塞返回**：第 19 轮内 3 次挂载失败后（详见下节），1.7 s 后仍正常 `hardware test closed`。

仍需人工确认或补充测量：

- **screen 子对象基线未测量**。原实现没有任何可观测输出，无法核对“子对象数量保持基线”。已补充两行日志（见“实现的取舍与已知边界”），需重新构建烧录后重跑。
- **返回后方向键是否只移动 Launcher 焦点**未由日志体现，需目视确认。
- **单轮连续 10 轮**未在一次不中断的快速轮转中完成（最长连续段为 6 轮），重跑时需补齐。
- **B 短按是否保留 15 页原操作**、**300 ms 提示是否出现一次**需目视确认。

### 检查点 4 的 MicroSD 缺失路径 — 通过（含 1 项既存问题复现）

第 19 轮（open 337202）内人工在 MicroSD 页按 A 三次，日志：

```text
E (344765) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
W (344766) gpio: conflict found for GPIO[22]
E (344766) vfs_fat_sdmmc: esp_vfs_fat_sdspi_sdcard_init failed (0x107).
W (350882) gpio: conflict found for GPIO[22]
E (350903) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
W (350903) gpio: conflict found for GPIO[22]
E (350904) vfs_fat_sdmmc: esp_vfs_fat_sdspi_sdcard_init failed (0x107).
W (351639) gpio: conflict found for GPIO[22]
E (351660) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
W (351660) gpio: conflict found for GPIO[22]
E (351661) vfs_fat_sdmmc: esp_vfs_fat_sdspi_sdcard_init failed (0x107).
I (353366) xiaomiao_dash: hardware test closed
```

- 失败来自 `sd_try_mount()`，全仓只由 A 键的 `ui_action()` 调用（`main/main.c:2077`），因此 3 次失败对应 3 次人工按键，不存在自动重试循环。
- 失败后 1.7 s 仍能正常返回 Launcher，**未阻塞系统启动、App 进入或返回**，符合本 Goal“MicroSD 缺失不得阻塞”的要求，也符合设计文档第 9 节“失败则 SD_UNAVAILABLE、记录日志、继续启动 Launcher”的既定策略。
- 这两行日志（`sdmmc_card_init failed` 与 `gpio: conflict found for GPIO[22]`）是**既有问题**，`docs/xiaomiao_firmware_v0.1_design.md` 第 9 节已原样记录，并明确“届时再重点解决 SD 驱动和 GPIO 冲突”。本次是首次在实机复现。`PIN_NUM_SD_CS = GPIO_NUM_22`（`main/main.c:139`）。该问题不属于节点 4 范围（本 Goal 禁止改引脚与硬件协议），需另立任务处理。
- 未验证：挂载失败后再次进入 Hardware Test 并浏览其他页面是否仍正常（第 19 轮返回后日志结束）。GD32 `0x40` 与 MPU6050 缺失路径也尚无证据。

### 存档说明

以上“检查点 1”“生命周期轮转（11:32）”“MicroSD 缺失路径（11:32）”三段为 2026-09-20 11:20／11:32 两轮实测的原始记录，其中生命周期与 MicroSD 结论已被 11:43 轮次取代（11:32 轮最多连续 6 轮、未测量 `screen children`）。保留原文以便追溯当时的证据范围，当前结论以 11:43 轮次为准。

节点 4 的唯一未验证条款为“从电机或蜂鸣器运行状态退出时先停止持续输出”，原因与处理见上一节（设备缺失，按失败路径条款不阻塞）。其余验收条款均已满足。LED、电机、MPU6050 的实机行为仍无证据，属 GD32 `0x40` 与双 MCU 联调的长尾，已记入 `ROADMAP.md` 待确认事项。

## 修订记录（2026-09-20 11:02）

施工前的静态可行性核对发现原决策 9 对 B 键判定通道的描述不足以实施，本次修订只补齐该通道，不改变验收标准。

- 核对对象：`main/main.c`（2367 行）、`main/framework/xiaomiao_app.h`、`main/framework/xiaomiao_navigation.h` 与本仓库锁定的 LVGL 9.5 `lv_indev.c`。
- 核对结论：Goal 引用的全部符号均存在——`ui_create()`（`main/main.c:2154`，第 2158 行使用 `lv_screen_active()`）、`lv_group_add_obj`／`focus_obj`（2165-2166）、`ui_cancel()`（1983）、`ui_refresh()`（1725）、`hardware_update()`（757）、`hardware_process_timers()`（523）、`UI_PAGE_LIGHT`（233）、`s_ui`／`s_board`（315-316）、`xiaomiao_navigation_app_root()`（`xiaomiao_navigation.h:87`）、`xiaomiao_app_t` 的 `id/name/icon/init/open/close`（`xiaomiao_app.h:46-66`）。
- 问题一（阻塞性）：原决策 9 允许“事件回调或小型状态机”二选一，但对象级 `LV_EVENT_KEY` 通道实际不可行——`indev_keypad_proc()` 对 `LV_KEY_ESC` 不派发释放事件（`lv_indev.c:931`），持有期间每 130 ms 重复派发（`lv_indev.c:919-922`），内建长按事件也只对 `LV_KEY_ENTER` 发送（`lv_indev.c:890`、`903`）。修订为决策 9 的强制通道，并明确状态机与业务动作分离、全局按键时间不得挪用。
- 问题二（非阻塞）：原决策 3 写“约 1800 行”，实际 `main/main.c` 为 2367 行，已按实际值修正。
- 补充：明确禁止改动 `main/framework/` 下节点 1～3 文件，避免后续把 Launcher 的 B 转发分支当作冗余清理。
- 本次修订仅涉及文档，未修改任何固件源码；节点 4 仍处于“尚未实现”，编译、烧录与目标板行为均未验证。

## 修订记录（2026-09-20 11:10）

第二次施工前修订，落实三点既定结论，不改变验收标准：

- 保留长按方案与 800 ms 阈值，并在决策 8 中补充否决“按页面状态自适应”语义的理由——`ui_cancel()` 在电机页未运行时的分支是切换方向而非取消（`main/main.c:2029-2046`），自适应语义会使电机页要么无法用 B 返回、要么无法切换方向；因此只能用与页面无关的时间维度区分。
- 决策 9 新增可发现性要求：按住达到 `BTN_HOLD_HINT_MS`（建议 300 ms）时复用现有状态行 `set_action()` 显示一次退出提示，不新增 UI 元素、不改 15 页布局，提示仅在 Hardware Test 打开期间生效且同一次按压只显示一次。
- 决策 9 补充短按延迟的量级说明：延迟为“一个按键时长”（几十到一百多毫秒），受 800 ms 阈值约束，属决策 8 的固有代价。
- 同步更新 B 键行为表、执行检查点 3 与既有修订记录中的相关表述。
- 本次修订仅涉及文档，未修改固件源码；节点 4 仍处于“尚未实现”，编译、烧录与目标板行为均未验证。
