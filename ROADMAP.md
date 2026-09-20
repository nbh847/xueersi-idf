# 项目路线图

本文件是项目当前状态的唯一入口。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。

逐条的施工与验证流水（含串口日志分析、口径差异、证据类型差异和未验证范围）保留在各 Goal 施工文档中，完成后不删除；按日期索引与结论汇总见 `goals/ROADMAP-history.md`。本文件只保留当前状态与进度判定。

## 当前状态

- 固件基于 ESP-IDF 6.1 与 LVGL 9.5。普通固件开机进入 `main/framework/` 的 Launcher，按注册顺序显示 `Games`、`PC Monitor`、`Tools`、`Settings` 与 `Hardware Test` 五个入口：前四项占满第 1 页 2 列 × 2 行网格，`Hardware Test` 在第 2 页。焦点移动：`←`／`→` 在行内换列，并在右列按 `→` 翻到下一页**同一行**第一格、在左列按 `←` 翻回上一页**同一行**第二格（目标格不存在时保持焦点）；`↑`／`↓` 在页内换行，不翻页。其余越出网格的移动保持焦点、不循环。该模型于 2026-09-20 取代节点 3 决策 5 的“左右限本行、上下跨页”。
- 分层：`main/framework/` 为 App Framework 运行时（节点 1～3），`main/apps/` 为业务 App（节点 5～8），`main/services/` 为 Service 层（节点 9 的首个 `Settings Service`，App 不直接访问 NVS），`main/main.c` 持有 15 页 Hardware Dashboard 并注册为 `Hardware Test` App。BSP 分层尚未实现。
- Dashboard 覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统、About。
- ESP32 侧已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。GD32 仓库源码只实现 USB CDC、USART1 桥与 ESP32 IO0/EN 控制，I2C `0x40` LED／电机从机协议未实现。
- Settings 的 `wifi_auto_connect` 与 `sound_enabled` 目前只是持久化偏好，要等节点 10／12 消费后才有业务效果；Settings 的 `Wi-Fi`／`Sound` 页与 Tools 的 Wi-Fi 详情页在对应 Service 落地前如实显示不可用。

## 当前开发节点

- **节点 0～9 均已完成并实机验证。** 每个节点的交付内容、验证证据、口径差异与未验证范围见各自施工文档：

  | 节点 | 施工文档 |
  | --- | --- |
  | 0 构建基线恢复 | `goals/20260919-1834-build-baseline.md` |
  | 1 App Framework | `goals/20260919-2037-app-runtime.md` |
  | 2 Navigation | `goals/20260920-0816-navigation.md` |
  | 3 Launcher | `goals/20260920-1007-launcher.md` |
  | 4 Hardware Test App | `goals/20260920-1053-hardware-test-app.md` |
  | 5 Games 占位 App | `goals/20260920-1159-games-placeholder.md` |
  | 6 PC Monitor UI | `goals/20260920-1235-pc-monitor-ui.md` |
  | 7 Tools App | `goals/20260920-1258-tools-app.md` |
  | 8 Settings UI | `goals/20260920-1338-settings-ui.md` |
  | 9 Settings Service 与 NVS | `goals/20260920-1429-settings-service-nvs.md` |

- 未验证项汇总（均已在各自 Goal 中判定为不阻塞收口）：LED、电机、MPU6050 的实机行为无证据（等节点 16 的 GD32 `0x40` 协议）；节点 9 的 `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败无法在不改分区表的前提下构造，只按源码检查确认；节点 8 与节点 9 的末轮回归为人工口头确认，无补充串口日志。
- Launcher 导航改为网格 + 右键翻页（2026-09-20，非设计文档第 15 节的有序节点）：**已完成**。`←`／`→` 在行内换列，在右列按 `→` 翻到下一页同一行第一格、在左列按 `←` 翻回上一页同一行第二格，目标格不存在时保持焦点；`↑`／`↓` 在页内换行且不翻页。每页 4 格时从第 1 格按 `→` 两次即翻页。网格几何、每页容量、槽位的行优先对应关系、卡片布局与配色均未改。实机验证：自测固件输出 `LAUNCHER_SELF_TEST: PASS`（引导路径 13 步，八种数量边界与失败路径均通过），普通固件的四方向导航与五个 App、Hardware Test 15 页由人工确认符合要求（无补充串口日志）。本变更经两次模型修订（首版“左右线性 ±1 且删掉上下键”→ “外侧列翻页 + 上下换行” → 最终“翻页保持同一行”），施工文档为 `goals/20260920-2015-launcher-lr-paging.md`。
- GD32 实机固件与 README 中 `0x40` 协议的对应关系待确认，不阻塞不依赖 LED、电机的 v0.1 框架开发。

## 有序开发节点

节点定义、交付和验收标准见 `docs/xiaomiao_firmware_v0.1_design.md` 第 15 节；此处只维护唯一进度状态。

- [x] 节点 0：构建基线恢复。
- [x] 节点 1：App Framework。
- [x] 节点 2：Navigation。
- [x] 节点 3：Launcher。
- [x] 节点 4：Hardware Test App。
- [x] 节点 5：Games 占位 App。
- [x] 节点 6：PC Monitor UI。
- [x] 节点 7：Tools App。
- [x] 节点 8：Settings UI。
- [x] 节点 9：Settings Service 与 NVS。
- [ ] 节点 10：Wi-Fi Service。
- [ ] 节点 11：PC Monitor 通信。
- [ ] 节点 12：Audio Service。
- [ ] 节点 13：首个正式游戏。
- [ ] 节点 14：Storage Service。
- [ ] 节点 15：Assets 与文件系统。
- [ ] 节点 16：GD32 `0x40` 协议补全。

## 下一步

1. **节点 10“Wi-Fi Service”尚未立项**，是下一个有序开发节点；它需要消费节点 9 的 `wifi_auto_connect`，并在落地后替换 Settings 的 `Wi-Fi` 页与 Tools 的 Wi-Fi 详情页文案。Launcher 网格导航改造已于 2026-09-20 完成并验证，不再是待办。
2. **节点 10“Wi-Fi Service”尚未立项**，是下一个有序开发节点；它需要消费节点 9 的 `wifi_auto_connect`，并在落地后替换 Settings 的 `Wi-Fi` 页与 Tools 的 Wi-Fi 详情页文案。
3. GD32 `0x40` 协议补全（节点 16）完成前，LED、电机、MPU6050 的实机回归无法闭环；节点 4 已按设备缺失处理并记录，不重复阻塞后续节点。
4. 按需另立任务定位 SD／GPIO22 配置冲突（见下节“待确认与已知风险”）。
5. 可选补录（均不影响已完成节点的结论，固件已在板上，重新 `idf.py -p COM5 monitor` 即可，无需重新编译或烧录）：节点 4 缺失设备页的实际显示文本与挂载失败后重新进入 App 的行为；节点 5 的“连续 10 轮”与“5 轮双 App 交替”样本；节点 8 与节点 9 末轮回归的补充串口日志。
6. 已实现的设计约束（供后续复核，非待办）：B 的短按／长按判定基于 `keypad_read_cb()` 的按下与释放边沿加独立状态机，动作在 LVGL 循环执行，`lv_indev_set_long_press_time()` 全局设置未改；Launcher 的“App 打开态转发 B”分支对已取得输入焦点的 App 不可达。细节见 `goals/20260920-1053-hardware-test-app.md` 与 `goals/20260920-1159-games-placeholder.md`。

## 待确认与已知风险

- **MicroSD／GPIO22 冲突未定位根因**：`PIN_NUM_SD_CS = GPIO_NUM_22`，按 A 挂载时报 `sdmmc_card_init failed` 与 `gpio: conflict found for GPIO[22]`，2026-09-20 实机复现两次（按 A 共 5 次失败 5 次）。两次均未阻塞启动、进入与返回，符合设计文档第 9 节“失败则 SD_UNAVAILABLE、记录日志、继续启动 Launcher”的既定策略；但第二次及后续尝试在挂载前即报冲突，提示 CS 引脚未被上一次失败释放，需另立任务定位。
- README 记录的 GD32 LED／电机协议已被 ESP32 代码使用，但 GD32 工程缺少对应实现，双 MCU 联调结果待确认。
- 本机 ESP-IDF 安装为非默认布局（venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下），需进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`；MSYS/Git Bash 会因 `MSYSTEM` 变量被 `export.ps1` 拒绝。环境细节与已验证事实见 `AGENTS.md`。
- PC Monitor 的指标当前为编译期占位，真实数据与通信属节点 11，尚未开始。

## 历史记录

逐条的施工与验证流水（时间、事件、结论、对应施工文档）见 `goals/ROADMAP-history.md`。本文件在 2026-09-20 重构时把这两节迁出，以避免与 Goal 文档重复；重构前的完整原文见 `git log -p ROADMAP.md`。
