# 项目路线图

本文件记录项目当前状态。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。每个 Goal 的施工文档保留在 `goals/`，用于沉淀范围、决策、验收证据和限制；完成后不删除，当前状态以本文件为准。

## 当前状态

- ESP32 固件基于 ESP-IDF 6.1 与 LVGL 9.5，当前入口为 `main/main.c` 的单文件 Hardware Dashboard。
- Dashboard 已覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统和 About。
- ESP32 端已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。
- GD32 工程当前只确认实现 USB CDC、USART1 桥和 ESP32 IO0/EN 控制；I2C `0x40` LED/电机从机协议未在仓库源码中实现。
- Service 和 BSP 分层仍处于设计阶段。App Framework 核心运行时（App 描述、Registry、Manager）已于节点 1 实现，Navigation 统一进入／返回与内容根对象所有权已于节点 2 实现，Launcher 两列首页、焦点与分页已于节点 3 实现，均位于 `main/framework/`；节点 4 已把 15 页 Dashboard 注册为 `Hardware Test` App 并把普通固件默认入口切换为 Launcher，可执行的启动、生命周期、输入和 15 页人工测试均已通过。

## 当前开发节点

- 节点 0“构建基线恢复”已完成（2026-09-19，全新配置构建、烧录和基础交互验证通过；未覆盖全部外设回归）。施工文档为 `goals/20260919-1834-build-baseline.md`。
- 节点 1“App Framework”已完成（2026-09-20，框架代码、自测代码、普通构建、自测构建、自测固件 PASS 标记和普通固件 Dashboard 回归均通过）。施工文档为 `goals/20260919-2037-app-runtime.md`。
- 节点 2“Navigation”已完成（2026-09-20，导航运行时 + 自测 + 自测固件实机 PASS + 普通构建 15 页 Dashboard 回归全部通过）。施工文档为 `goals/20260920-0816-navigation.md`。
- 节点 3“Launcher”已完成（2026-09-20，运行时代码 + 自测代码 + 自测构建实机 `LAUNCHER_SELF_TEST: PASS` + 普通构建 15 页 Dashboard 回归与视觉三项确认全部通过）。本节点先通过专用自测构建验收动态入口、焦点和分页，普通固件继续启动 Dashboard，节点 4 接入 Hardware Test App 后再切换默认启动。施工文档为 `goals/20260920-1007-launcher.md`。
- 节点 4“Hardware Test App”已完成（2026-09-20）。普通固件默认进入 Launcher，15 页 Dashboard 注册为 `Hardware Test` App，B 手势判定通道固定为 `keypad_read_cb()` 边沿加独立状态机（对象级事件回调通道经核对不可行）。实机验证：检查点 1（11:20）；生命周期 16 轮／连续 11 轮且 `screen children` 恒为 2、B 短按与长按、15 页可达、MicroSD 缺失路径（11:43）。唯一未验证条款为“从电机或蜂鸣器运行状态退出时先停止持续输出”，因本机无电机硬件、GD32 无 `0x40` 从机实现、蜂鸣器仅鸣叫 140 ms 而无法构造，按本 Goal 失败路径条款标记未验证且不阻塞；**LED、电机、MPU6050 的实机行为仍无证据，不得视为外设回归已通过**。施工文档为 `goals/20260920-1053-hardware-test-app.md`。
- GD32 实机固件与 README 中 `0x40` 协议的对应关系仍在待确认状态，不阻塞不依赖 LED、电机的 v0.1 框架开发。

## 有序开发节点

节点定义、交付和验收标准见 `docs/xiaomiao_firmware_v0.1_design.md` 第 15 节；此处只维护唯一进度状态。

- [x] 节点 0：构建基线恢复。
- [x] 节点 1：App Framework。
- [x] 节点 2：Navigation。
- [x] 节点 3：Launcher。
- [x] 节点 4：Hardware Test App。
- [ ] 节点 5：Games 占位 App。
- [ ] 节点 6：PC Monitor UI。
- [ ] 节点 7：Tools App。
- [ ] 节点 8：Settings UI。
- [ ] 节点 9：Settings Service 与 NVS。
- [ ] 节点 10：Wi-Fi Service。
- [ ] 节点 11：PC Monitor 通信。
- [ ] 节点 12：Audio Service。
- [ ] 节点 13：首个正式游戏。
- [ ] 节点 14：Storage Service。
- [ ] 节点 15：Assets 与文件系统。
- [ ] 节点 16：GD32 `0x40` 协议补全。

## 下一步

1. 实施节点 5“Games 占位 App”：提供可进入、可返回的占位页面，不创建独立 FreeRTOS Task。该节点会让 Launcher 出现 2 个 App，届时 `launcher focus=／page=` 日志才具备证明力，可顺带复核节点 4 遗留的“返回后焦点与页码与进入前一致”。
2. GD32 `0x40` 协议补全（节点 16）完成前，LED、电机、MPU6050 的实机回归无法闭环；节点 4 已按设备缺失处理并记录，不重复阻塞后续节点。
3. 可选补录（不影响节点 4 结论）：缺失设备页面的实际显示文本（GD32 `0x40` 的 LED／电机页、MPU6050 的运动页），以及挂载失败后重新进入 App 的行为。
4. 按需另立任务定位 SD／GPIO22 配置冲突（`docs/xiaomiao_firmware_v0.1_design.md` 第 9 节已预告，不属节点 4）。
5. 已实现的节点 4 约束（供后续复核，非待办）：B 的短按／长按判定基于 `keypad_read_cb()` 的按下与释放边沿加独立状态机，动作在 LVGL 循环执行；`ui_key_event_cb()` 中原有的 `LV_KEY_ESC` 立即分支已移除，B 只有一个决策点；`lv_indev_set_long_press_time()` 全局设置未改（方向键按住重复依赖它）。Launcher 在“App 打开”状态下转发 B 的分支经复核为不可达（Dashboard 输入对象取得焦点后 Launcher 不再接收按键），属预期行为，未修改 `main/framework/` 下节点 1～3 文件。

## 待确认与已知风险

- `docs/xiaomiao_firmware_v0.1_design.md` 第 9 节记录的 `sdmmc_card_init failed` 与 `gpio: conflict found for GPIO[22]` 已于 2026-09-20 实机复现两次（11:32 按 A 三次失败三次、11:43 按 A 两次失败两次，`PIN_NUM_SD_CS = GPIO_NUM_22`）。两次均未阻塞启动、进入与返回，符合设计文档“失败则 SD_UNAVAILABLE、记录日志、继续启动 Launcher”的既定策略；但配置 SD 卡时的具体冲突机制尚未定位（第二次及后续尝试在挂载前即报 GPIO22 冲突，提示 CS 引脚未被上一次失败释放），需另立任务，不属节点 4 范围。
- README 记录的 GD32 LED/电机协议已被 ESP32 代码使用，但当前 GD32 工程缺少对应实现，双 MCU 联调结果待确认。
- 本机 ESP-IDF 安装为非默认布局：Python venv 3.14.7 位于 `<IDF_TOOLS_PATH>/tools/python/v6.1/venv`（不在 `<IDF_TOOLS_PATH>/python_env/` 下），需进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 后再运行官方 `export.ps1`；constraints 文件曾错位于工具目录的 `tools/` 子目录，2026-09-19 经授权复制到工具目录根修复（原文件保留）。MSYS/Git Bash 中执行 export 会因 `MSYSTEM` 变量被拒绝。PATH 中的 `idf.py.exe`（idf-exe 1.0.3 包装器）`--version` 显示包装器自身版本，确认 IDF 版本需用 `python "$env:IDF_PATH\tools\idf.py" --version`。
- 节点 0～4 的可执行人工验证均已完成；LED、电机、MPU6050 的真实设备行为仍需相应硬件与 GD32 `0x40` 协议配合验证，MicroSD 缺失路径已覆盖。

## 最近完成

- 2026-09-20 11:46：完成节点 4“Hardware Test App”，标记为已完成。人工确认蜂鸣器实际发声正常、本机无电机硬件，据此查明验收条款“从电机或蜂鸣器运行状态退出时先停止持续输出”在当前硬件上无法构造：电机侧 `s_board.motor_running[]` 只在 `gd32_motor_set()` 返回 `ESP_OK` 时置真，无电机且 GD32 无 `0x40` 从机实现故从未为真，`hardware_test_close()` 的停止循环被 `continue` 跳过；蜂鸣器侧 A 键仅鸣叫 140 ms（`buzzer_beep(s_buzzer_freq_hz, 140)`，`main/main.c:2065`）后自动停止，不存在“仍在发声”时按 B 的时刻，单键模型下也无法同时按住 A 与 B。该条款按本 Goal 失败路径条款标记未验证且不阻塞；同时明确 LED、电机、MPU6050 的实机行为仍无证据，不得视为外设回归已通过。同步更新 `ROADMAP.md` 与 Goal。
- 2026-09-20 11:32：为节点 4 检查点 2 补充可观测日志。原实现没有任何输出可用于核对“screen 子对象数量保持基线”，该验收条款实际不可测量，因此在 `hardware_test_open()` 末尾输出活动 screen 的子对象数量、在 `hardware_test_close()` 末尾输出同一数值以及 `xiaomiao_launcher_focused_index()`／`xiaomiao_launcher_page_index()`。前者用于发现内容根泄漏（应恒为 Launcher 根 + 新 App 内容根），后者作为“返回后与进入前一致”的证据。仅新增日志，未改变任何控制流与功能。
- 2026-09-20 11:18：实现节点 4“Hardware Test App”代码。`main/main.c` 新增 `Hardware Test` App 描述（`id=hardware_test`、`name=Hardware Test`、`icon=LV_SYMBOL_SETTINGS`，`init` 为空，硬件仍在 `app_main` 初始化一次）与 `hardware_test_open/close/is_open`；`ui_create()` 改为接收 Navigation 内容根作为父对象，不再使用 `lv_screen_active()`；`close` 先取消 B 手势、停蜂鸣器与两路电机（失败记录错误码但不阻塞），再从 group 移除输入对象并清空全部 `s_ui` 引用。B 键改为 `keypad_read_cb()` 边沿驱动的独立状态机（800 ms 长按、300 ms 后复用 `set_action()` 显示 `Hold to exit` 提示），动作在 LVGL 循环执行；`ui_key_event_cb()` 中立即执行 `ui_cancel()` 的 `LV_KEY_ESC` 分支已移除。`lvgl_task()` 改为注册 App、`init_all()`、创建 Launcher，任一步失败记录阶段与错误码且不回退到 Dashboard；`hardware_update()`／`ui_refresh()` 仅在 Hardware Test 打开时执行，`hardware_process_timers()` 保持常驻。启动日志改为 `Xiaomiao LVGL 9.5 launcher boot` 与 `Start Xiaomiao launcher`。未修改 `main/framework/`、GD32、硬件协议、引脚、依赖版本或 `dependencies.lock`。
- 2026-09-20 11:10：修订节点 4 施工文档，落实三点既定结论：保留 800 ms 长按方案并在决策 8 记录否决“按页面状态自适应”语义的理由（`ui_cancel()` 在电机页未运行时的分支是切换方向，`main/main.c:2029-2046`）；决策 9 新增长按可发现性提示（按住达 300 ms 复用 `set_action()` 显示一次，不新增 UI 元素、不改 15 页布局）；补充短按延迟量级说明（一个按键时长，几十到一百多毫秒，受 800 ms 阈值约束）。
- 2026-09-20 11:02：补齐节点 4 施工文档的 B 键判定通道决策。原决策 9 允许在“对象级事件回调”与“小型状态机”之间二选一，经查本仓库锁定的 LVGL 9.5 `indev_keypad_proc()` 后确认事件回调通道不可行，改为强制使用 `keypad_read_cb()` 的按下／释放边沿加独立状态机：阈值 800 ms 用 `lv_tick_get()` 计算，状态机在回调内只记录手势、`ui_cancel()` 与 `xiaomiao_navigation_back()` 在 LVGL 循环执行，不得改全局按键时间。同时明确删除 `ui_key_event_cb()` 原有 `LV_KEY_ESC` 立即分支使 B 只有一个决策点，禁止改动 `main/framework/` 下节点 1～3 文件，并修正施工文档中 `main/main.c` 的行数表述。仅修改文档，未改动固件源码。
- 2026-09-20 10:53：创建节点 4“Hardware Test App”施工文档，明确普通固件默认切换为 Launcher、15 页 Dashboard 保留在 `main/main.c` 做最小生命周期适配、Navigation 内容根对象所有权、短按 B 与 800 ms 长按 B 互斥、安全停止电机／蜂鸣器、10 轮进入返回和 15 页外设回归标准；尚未修改固件源码或执行人工构建与实机验证。
- 2026-09-20 10:43：完成节点 3“Launcher”：自测构建与普通构建均经人工编译通过（`xiaomiao.bin` 0x88580，应用分区 47% free），自测固件自动断言（0/1/2/3/4/5/7/16 数量边界、未初始化打开失败、App 打开期间 `destroy()` 被拒、两轮 open/back）与实机按键路径（15 步方向遍历 + 3 轮 open/back）输出 `LAUNCHER_SELF_TEST: PASS`，普通固件启动 `Xiaomiao LVGL 9.5 dashboard boot`、按键回归正常，2 列 × 2 行布局／焦点反馈／省略号截断／页码切换四项视觉确认符合预期。首轮编译失败已定位为自测宏参数名与结构体字段名重名，重命名后修复。普通固件默认入口仍为 Dashboard，节点 4 负责切换。
- 2026-09-20 10:20：实现节点 3“Launcher”：新增 `main/framework/xiaomiao_launcher.{h,c}`（按 Registry 动态生成 2 列 × 2 行入口、页码由焦点索引推导、左右限本行且不循环、上下同列 ±2 可跨页、下键在右列越界回退到该行左侧入口、A 经 Navigation 打开、B 幂等返回、空 Registry 空状态、单一根对象承载全部子对象与 group 移除）与 `xiaomiao_launcher_selftest.{h,c}`（16 个静态测试 App 描述、主流程注册 7 个，自动断言 + 15 步方向遍历与 3 轮 open/back 引导式按键路径，PASS 标记 `LAUNCHER_SELF_TEST: PASS`）、CMake option `XIAOMIAO_LAUNCHER_SELF_TEST` 与 `main/main.c` 四形态互斥入口。普通构建路径无行为变化，默认仍启动 15 页 Dashboard。编译、烧录与实机验证待人工执行。
- 2026-09-20 10:07：创建节点 3“Launcher”施工文档，明确两列四项分页、Registry 动态入口、焦点移动、Navigation 进入／返回、空列表与边界行为、自测范围，以及节点 4 前保留普通 Dashboard 启动路径的过渡方案；同步修正 README 中 Navigation 状态。
- 2026-09-20 09:54：完成节点 2“Navigation”：交付 `main/framework/xiaomiao_navigation.{h,c}`（按 ID 打开、统一返回、查询当前 App、获取内容根；Manager 唯一状态来源，内容根在 App `open` 回调前发布、`close` 回调后统一删除）、`xiaomiao_navigation_selftest.{h,c}`（自动断言 + 3 轮 A/B 实机按键路径，PASS 标记 `NAVIGATION_SELF_TEST: PASS`）、CMake option `XIAOMIAO_NAVIGATION_SELF_TEST` 与 `main/main.c` 三形态互斥入口。三次失败均已定位修复（app_main 作用域→互斥结构；ccache 裸名 launcher→PATH 含 ccache；open 回调根对象缺失→s_app_root 提前赋值）。自测固件 `xiaomiao.bin` 0x83a10（49% free）实机 PASS，普通构建 15 页 Dashboard 回归通过。
- 2026-09-20 08:33：实现节点 2“Navigation”代码与自测：新增 `main/framework/xiaomiao_navigation.h/.c`（按 ID 打开、统一返回、查询当前 App、获取内容根；Manager 仍为当前 App 唯一状态来源，App `close` 后由 Navigation 统一删除内容根）、`xiaomiao_navigation_selftest.h/.c`（错误路径与多轮 open/back 自动断言 + 3 轮 A 进入／B 返回／B 幂等实机按键路径，PASS 标记 `NAVIGATION_SELF_TEST: PASS`）、CMake option `XIAOMIAO_NAVIGATION_SELF_TEST` 及 `main/main.c` 最小自测入口。普通构建路径无行为变化。编译、烧录与实机验证待人工执行。
- 2026-09-20 08:18：更新项目验证分工：ESP-IDF 编译、`set-target`、烧录、串口监视和目标板操作统一由人工执行；Agent 只提供命令、执行静态检查并复核人工证据。节点 2 Goal 已同步该要求。
- 2026-09-20 08:16：创建节点 2“Navigation”施工文档，明确统一打开／返回接口、App 内容根对象所有权、LVGL 清理、开发自测、失败路径和验收标准；尚未开始实现。
- 2026-09-20 07:33：完成节点 1“App Framework”：在 `main/framework/` 实现 `xiaomiao_app_t` 描述、静态 Registry（容量 16）、生命周期 Manager（init_all/open/close/current）和开发自测；通过 CMake option `XIAOMIAO_FRAMEWORK_SELF_TEST` 接入；普通构建和自测构建均通过 ESP-IDF 6.1 编译，自测固件串口输出 `APP_FRAMEWORK_SELF_TEST: PASS`，普通固件 15 页 Dashboard 翻页及 A/B 键回归通过。
- 2026-09-19 20:50：将 `dependencies.lock` 独立同步到项目的 ESP-IDF 6.1 基线（IDF 6.1.0、锁文件格式 3.0.0）；LVGL 仍为 9.5.0，`manifest_hash` 未变化。
- 2026-09-19 20:37：创建节点 1“App Framework”施工文档，明确框架接口、静态 Registry、生命周期 Manager、自测方式、范围边界和验收标准；尚未启动执行。
- 2026-09-19 20:35：完成节点 0 补充实机验证，固件可正常烧录和启动，15 个 Dashboard 页面均可翻页，A/B 键操作正常；未将未测试外设标记为已验证。
- 2026-09-19 20:23：修正节点 0 Goal 与路线图中的机器绝对路径表述，改用环境变量占位符；保留 `.tmp/` 中的本机诊断证据。
- 2026-09-19 19:52：完成节点 0“构建基线恢复”：定位环境错配根因（`IDF_TOOLS_PATH` 未设置、venv 非默认布局、`MSYSTEM` 泄漏、constraints 文件错位），以进程级环境变量修复并经授权复制 constraints 文件到工具目录根；在 `.tmp/build-baseline/` 完成全新配置构建与二次构建验证；构建入口与环境要求写入 `AGENTS.md` 与 `docs/project-overview.md`。
- 2026-09-19 18:34：创建节点 0 的 Goal 施工文档，明确目标、边界、检查点、失败路径、验收标准和交付内容。

## 最近验证

- 2026-09-20 11:46（节点 4 收口判定，人工确认、Agent 复核）：人工确认蜂鸣器实际发声正常、本机无电机硬件。据此判定“从电机或蜂鸣器运行状态退出时先停止持续输出”在当前硬件上不可构造：电机侧 `s_board.motor_running[]` 仅在 `gd32_motor_set()` 返回 `ESP_OK` 时置真（`ui_motor_toggle()`），无电机且 GD32 未实现 I2C `0x40` 从机协议，该标志从未为真，`hardware_test_close()` 的 `if (!s_board.motor_running[motor]) continue;` 跳过停止命令；蜂鸣器侧 A 键调用 `buzzer_beep(s_buzzer_freq_hz, 140)`（`main/main.c:2065`），140 ms 后由 `s_buzzer_stop_at` 在 `hardware_process_timers()` 中自动停止，不存在“仍在发声”时按 B 的时刻，且 `keypad_read_cb()` 为单键模型无法同时按住 A 与 B。`buzzer_stop()` 在每次 `close` 均被调用、代码路径已执行但无可观察效果。按本 Goal 失败路径条款（设备缺失只标记对应外设未验证、不阻塞）标记该条款未验证并收口节点 4；同时在 Goal 与路线图中明确 LED、电机、MPU6050 的实机行为仍无证据，不得视为外设回归已通过。
- 2026-09-20 11:43（节点 4 最终生命周期轮转与人工确认，人工执行、Agent 复核）：同一 Monitor 会话内 16 轮 `hardware test opened`／`hardware test closed` 全部配对（16/16），其中**前 11 轮为一次不中断的快速轮转**（每轮 1.09～1.29 s），满足“连续至少 10 轮”。**`screen children=2` 在全部 16 次 open 与 16 次 close 上恒定**，无一次增长，证明 Navigation 每次都在关闭后完整释放 App 内容根、Dashboard 子对象无泄漏，也即“连续 10 轮后 screen 子对象保持基线”成立；16 轮无崩溃、无 panic、无重启循环、无输入失效。`launcher focus=0 page=0` 全程不变，但**该项本轮不具证明力**（普通固件只注册 1 个 App，焦点无处可移、只有 1 页，恒为 0/0），Launcher 状态保留只能靠行为观察。人工确认：返回 Launcher 后按各方向键只有 Launcher 侧反应、无任何 Dashboard 反应（证明焦点已交回、周期循环未触碰失效 UI）；B 短按逐页给出原有提示且均未退出；按住约 0.3 s 出现一次 `Hold to exit`、达 800 ms 返回、松开无补发、提示不重复；15 页全部可进入、翻页与操作。MicroSD 页 2 次挂载失败后 2.7 s 仍正常返回，既存 SD／GPIO22 问题再次复现。当时仍缺“电机／蜂鸣器运行状态下退出先停输出”，已在 11:46 定性。
- 2026-09-20 11:32（节点 4 生命周期轮转与 MicroSD 缺失路径，人工执行、Agent 复核）：同一 Monitor 会话内 19 轮 `hardware test opened`／`hardware test closed` 严格配对，无孤立 open、无重复 close、无 panic、无重启循环；快速轮转段最长连续 6 轮（每轮 1.2～1.5 s），另有 4 轮与 3 轮连续段，长时段停留（35.8 s、25 s、15.4 s、16.2 s）后仍正常返回。`main/main.c` 中 `xiaomiao_navigation_back()` 只有第 1153 行一处调用（B 手势退出分支），`xiaomiao_launcher.c:253` 的 Launcher 转发分支在 Dashboard 持有焦点时不可达，因此 19 次返回全部由 B 长按手势触发且每次只产生一次关闭。第 19 轮内 MicroSD 页按 A 三次，3 次 `sdmmc_card_init failed (0x107)` 与 `gpio: conflict found for GPIO[22]`，1.7 s 后仍正常返回 Launcher——既未阻塞启动也未阻塞返回，符合设计文档第 9 节“SD 失败则 SD_UNAVAILABLE、记录日志、继续启动 Launcher”的既定策略；`sd_try_mount()` 只由 A 键的 `ui_action()` 调用（`main/main.c:2077`），不存在自动重试循环。该 SD／GPIO22 组合是设计文档已记录的既存问题（`PIN_NUM_SD_CS = GPIO_NUM_22`），本次为首次实机复现，不属节点 4 范围。当时仍缺 screen 子对象基线与 Launcher 焦点／页码（尚未补日志）、连续 10 轮、B 短按与提示目视确认、15 页与缺失设备回归。
- 2026-09-20 11:20（节点 4 检查点 1 实机验证，人工执行、Agent 复核）：人工编译并烧录后 Monitor 输出完整。应用信息 `App version: e003e2e-dirty`、`Compile time: Sep 20 2026 11:20:14`、`ESP-IDF: v6.1`，说明烧录的固件确含本次未提交改动。启动日志出现 `Xiaomiao LVGL 9.5 launcher boot` 与 `Start Xiaomiao launcher`，随后 `launcher: launcher created (1 apps)` 与 `Launcher ready, 1 app(s) registered`；全程未出现 `Self test build`／`Navigation self test build`／`Launcher self test build` 三个自测标记，也未出现旧的 `Start Xiaomiao hardware dashboard`，且没有 `hardware test opened`——说明普通构建走的是 Launcher 路径、Registry 只有 1 个正式 App、开机未创建 Dashboard。LVGL 显示初始化输出 `160x128, dpi=60, 3 full-screen DMA buffers, SPI=60 MHz`，无非预期错误、无 panic、无重启循环。检查点 1 全部满足。
- 2026-09-20 11:18（节点 4 实现后静态检查）：`git diff --check` 通过（仅 ROADMAP 的 CRLF 提示），大括号配平（346/346）。逐项核对改动只落在 `main/main.c` 与文档：未修改 `main/framework/` 下节点 1～3 文件、GD32、硬件协议、引脚、依赖版本或 `dependencies.lock`；未新增源文件，因此未改 `main/CMakeLists.txt`。确认 `ui_create()` 只剩一处调用（`hardware_test_open`）且传入 Navigation 内容根；`ui_key_event_cb()` 中 `LV_KEY_ESC` 立即分支已移除，B 只由 `hardware_test_b_gesture_update()/poll()` 决策；`lv_indev_set_long_press_time()`／`repeat_time` 未改；`hardware_update()`／`ui_refresh()` 已由 `hardware_test_is_open()` 门控，`hardware_process_timers()` 保持常驻；`hardware_update()` 只做 ADC／I2C 探测／MPU 采样，不含电机或蜂鸣器安全逻辑，门控不引入安全缺口。按项目分工未执行编译、烧录、串口监视或目标板操作；本条目执行时节点 4 运行时验收项均为未验证，后续进展见 11:20 条目。
- 2026-09-20 11:02（节点 4 施工前静态可行性核对）：逐项核对施工文档引用的符号，全部存在——`ui_create()`（`main/main.c:2154`，第 2158 行以 `lv_screen_active()` 为父对象）、`lv_group_add_obj`／`focus_obj`（2165-2166）、`ui_cancel()`（1983）、`ui_refresh()`（1725）、`hardware_update()`（757）、`hardware_process_timers()`（523）、`UI_PAGE_LIGHT`（233）、`s_ui`／`s_board`（315-316）、`xiaomiao_navigation_app_root()`（`xiaomiao_navigation.h:87`）、`xiaomiao_app_t` 五字段（`xiaomiao_app.h:46-66`）。发现 1 项阻塞性缺口：原决策 9 允许的“对象级事件回调”通道不可行，依据为本仓库锁定 LVGL 9.5 `lv_indev.c` 的 `indev_keypad_proc()`——ESC 释放不向对象派发事件（931）、按住期间每 130 ms 重复派发（919-922）、内建长按事件仅对 `LV_KEY_ENTER` 发送（890、903）；已改写决策 8／9 并补充依据小节。另修正施工文档中 `main/main.c` 行数“约 1800 行”为实际 2367 行。仅执行静态核对与文档修订，未修改固件源码，未执行编译、烧录、串口监视或目标板操作。
- 2026-09-20 10:53（节点 4 施工文档复核）：对照路线图、硬件与协议说明、项目概览、v0.1 设计文档、节点 3 已知输入边界、Framework／Navigation 接口和当前 Dashboard 启动链，确认节点 4 只做 App 生命周期与默认入口切换，不提前实施 BSP、Service 或大规模目录迁移；固件源码、编译、烧录、串口和实机行为均未验证。
- 2026-09-20 10:47（节点 3 最终验收）：复核 Launcher 运行时、自测源码、CMake 接入、普通启动分支、Goal 验收项与人工验证记录，未发现阻塞缺陷；`git diff --check` 通过，确认改动范围不含 `dependencies.lock`、GD32、硬件协议、引脚或依赖版本。按项目分工未重复执行编译、烧录、串口监视或目标板操作。
- 2026-09-20 10:43（节点 3 普通构建回归与视觉确认，人工执行、Agent 复核）：普通构建（默认分支，不含任何自测宏）经人工编译与烧录成功，串口输出 `Xiaomiao LVGL 9.5 dashboard boot` 与 `Start Xiaomiao hardware dashboard`（非 `Launcher self test build`），未出现新增错误；人工确认按键操作正常，15 页 Dashboard 行为与节点 2 基线一致。自测固件运行期间人工目视确认：2 列 × 2 行布局正确、焦点项蓝底 + 白框与未选中卡片区分明显、`Long Application` 以省略号截断不遮挡相邻卡片、页码在 `1/2` 与 `2/2` 间切换。节点 3 全部验收条款满足，标记完成；普通固件默认入口仍为 Dashboard。
- 2026-09-20 10:40（节点 3 自测固件实机验证，人工执行、Agent 复核）：`.tmp/build-launcher-selftest/`（`XIAOMIAO_LAUNCHER_SELF_TEST=ON`）经人工编译 exit=0，`xiaomiao.bin` 0x88580 字节（应用分区 0x100000 剩余 47%）。烧录后串口证据完整：8 个数量边界 `launcher created (0/1/2/3/4/5/7/16 apps)`、Manager 未初始化时 `open 'self.app0' failed: ESP_ERR_INVALID_STATE (0x103)`、App 打开期间 `destroy refused: an App is open`（两轮 open/back 各一次）、`automatic checks passed, guided key path: 15 steps + 3 rounds`；随后实机按键完成 `guided traversal done`（15 步方向遍历）、`round 1/3 → 2/3 → 3/3 done`（每轮 A 打开 / B 返回 / B 幂等），最终输出唯一标记 `LAUNCHER_SELF_TEST: PASS`。剩余：普通构建与 15 页 Dashboard 回归。
- 2026-09-20 10:40（节点 3 自测构建与首轮编译修复）：自测形态经人工执行 ESP-IDF 6.1 `set-target esp32` 与 `build` 均 exit=0。首轮编译在 `xiaomiao_launcher_selftest.c` 失败，根因为 `XM_TEST_APP_ENTRY` 宏参数名 `id/name/icon` 与结构体字段名重名，预处理把 `.id/.name/.icon` 中的标识符一并替换；重命名宏参数为 `app_id/app_name/app_icon` 后编译通过。其余新增文件首轮即通过编译。
- 2026-09-20 10:20（节点 3 静态检查）：`git diff --check` 通过（仅 README/ROADMAP 的 CRLF 提示）。逐文件核对改动落在 Goal 允许范围（新增 4 个 `main/framework/xiaomiao_launcher*` 文件、`main/CMakeLists.txt`、`main/main.c` 互斥入口、文档）；普通构建分支未改动，未修改 Dashboard、GD32、BSP、Service、硬件协议、引脚、依赖版本或 `dependencies.lock`；对照 LVGL 9.5 组件头文件确认所用 API 存在。未执行编译、烧录、串口监视或目标板操作，编译与实机验证标记为未验证。
- 2026-09-20 10:07（节点 3 施工文档复核）：对照设计文档、节点 1 Registry、节点 2 Navigation 和当前 Dashboard 启动链，确认节点 3 只实现 Launcher 本体、动态入口、焦点与分页，不提前迁移 Hardware Test App 或开发正式业务 App；编译、烧录和实机操作继续归人工执行。
- 2026-09-20 10:00（节点 2 主 Agent 最终验收）：逐项核对 Goal、Navigation 与自测源码、CMake 接入、普通启动分支和人工验证记录，未发现阻塞缺陷；确认未修改 `dependencies.lock`、GD32、硬件协议、引脚或依赖版本。仅执行静态检查，未重复编译、烧录、串口监视或目标板操作。
- 2026-09-20 09:54（节点 2 普通构建回归，人工执行、Agent 复核）：普通构建（不含 `XIAOMIAO_NAVIGATION_SELF_TEST`）通过 ESP-IDF 6.1 编译；烧录普通固件后 15 页 Hardware Dashboard 启动、左右翻页、A 执行动作与 B 停止／取消行为与节点 1 基线一致，无新增错误。节点 2 全部验收条款满足，标记完成。
- 2026-09-20 09:43（节点 2 自测固件实机验证，人工执行、Agent 复核）：修复 open 回调根对象缺失 bug 后重构建烧录，串口证据完整：`automatic checks passed`（覆盖未初始化/空 ID/未知 ID/打开冲突/NOT_FOUND 无泄漏/两轮自动 open/back 与对象基线恢复）+ 3 轮实机按键路径（每轮 `test App opened` → `test App closed` → 幂等检查）+ 最终 `NAVIGATION_SELF_TEST: PASS`。Goal 检查点 1～3 与自测相关验收条款全部满足。剩余：普通构建回归。
- 2026-09-20 09:23（节点 2 自测构建）：两轮失败后定位根因并完成自测固件构建。第一轮为 `app_main()` 变量重复定义（已按三形态互斥结构修复）；第二轮 ninja 静默 exit 2，经 idf.py 日志与 ninja 补跑定位为构建会话 PATH 缺 ccache（build.ninja launcher 写裸名 `ccache`，`CreateProcess failed`），非源码缺陷。PATH 补入 ccache 后补跑成功：`main.c` 仅 3 个预期内未使用函数警告，`xiaomiao.bin` 0x83a10 字节（应用分区 49% free），bootloader 与分区检查通过。烧录、`NAVIGATION_SELF_TEST: PASS` 实机路径与普通构建回归仍未验证。
- 2026-09-20 08:33（节点 2 静态复核）：`git diff --check` 通过；逐文件核对改动仅覆盖 Goal 允许范围（新增 4 个 framework 文件、`main/CMakeLists.txt`、`main/main.c` 最小自测入口），普通构建路径行为不变；未修改 GD32、硬件协议、引脚、依赖版本或 `dependencies.lock`。编译、烧录与实机回归尚未执行，标记为未验证。
- 2026-09-20 08:18（验证流程复核）：核对 `AGENTS.md` 与节点 2 Goal，确认编译、烧录、monitor 和实机交互均明确归属人工；Agent 的职责限定为命令准备、静态检查和证据复核，缺少人工结果时必须标记“未验证”。
- 2026-09-20 08:16（节点 2 施工文档复核）：对照 v0.1 设计文档节点 2、节点 1 已交付接口和当前 Dashboard 启动链，确认范围未扩大到 Launcher、Hardware Test App、BSP、Service、GD32 或硬件迁移；验收覆盖 A 进入、B 返回、失败回滚、LVGL 对象完整释放和普通 Dashboard 回归。
