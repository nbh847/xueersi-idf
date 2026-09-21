# 项目路线图

本文件是项目当前状态的唯一入口。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。

逐条的施工与验证流水（含串口日志分析、口径差异、证据类型差异和未验证范围）保留在各 Goal 施工文档中，完成后不删除；按日期索引与结论汇总见 `goals/ROADMAP-history.md`。本文件只保留当前状态与进度判定。

## 当前状态

- 固件基于 ESP-IDF 6.1 与 LVGL 9.5。普通固件开机进入 `main/framework/` 的 Launcher，按注册顺序显示 `Games`、`PC Monitor`、`Tools`、`Settings` 与 `Hardware Test` 五个入口：前四项占满第 1 页 2 列 × 2 行网格，`Hardware Test` 在第 2 页。焦点移动：`←`／`→` 在行内换列，并在外侧列翻页——优先落到相邻页**同一行**，该页没有对应行时回退到它的**第一行第一格**，只有目标页没有任何 App 才不翻页；`↑`／`↓` 在页内换行，不翻页。其余越出网格的移动保持焦点、不循环。该模型于 2026-09-20 取代节点 3 决策 5 的“左右限本行、上下跨页”。
- 分层：`main/framework/` 为 App Framework 运行时（节点 1～3，节点 10 新增挂在 top layer 的全局 Wi-Fi 图标），`main/apps/` 为业务 App（节点 5～8，节点 11 把 PC Monitor 接到 Agent Service 快照），`main/services/` 为 Service 层（节点 9 的 `Settings Service`，节点 10 的 `Wi-Fi Service` 及其配网网页、DNS 私有实现，节点 11 的 `Agent Service` 及 `pc-agent/` Python 后端；App 不直接访问 NVS，也不直接调用 `esp_wifi_*`、`esp_http_client` 或 cJSON），`main/main.c` 持有 15 页 Hardware Dashboard 并注册为 `Hardware Test` App。BSP 分层尚未实现。
- Dashboard 覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统、About。
- ESP32 侧已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。GD32 仓库源码只实现 USB CDC、USART1 桥与 ESP32 IO0/EN 控制，I2C `0x40` LED／电机从机协议未实现。
- Settings 的 `wifi_auto_connect` 已由节点 10 的 Wi-Fi Service 消费，用于控制开机自动连接与断线重连；`sound_enabled` 仍只是持久化偏好，要等节点 12 才有业务效果。Settings 的 Wi-Fi 页已支持配网、自动连接和忘记网络，Tools 的 Wi-Fi 页显示真实连接状态。

## 当前开发节点

- **节点 0～11 均已完成并取得人工验收确认。** 节点 11 的项目负责人已确认 ESP-IDF 构建、烧录与 CP5 手动场景全部通过；Agent 未重复执行固件构建、烧录或串口监视，且未收到新增日志。已完成节点的交付内容、验证证据、口径差异与未验证范围见各自施工文档：

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
  | 10 Wi-Fi Service | `goals/20260920-2210-wifi-service.md` |
  | 11 PC Monitor 通信 | `goals/20260921-1238-pc-monitor-communication.md` |

- 节点 11“PC Monitor 通信”已于 2026-09-21 完成（施工文档 `goals/20260921-1238-pc-monitor-communication.md`）：一个统一 Python HTTP Agent（`pc-agent/`，固定 IPv4、端口 `8766`，仅 `/api/v1/health` 与 `/api/v1/pc/metrics`）+ 固件通用 Agent Service（`main/services/xiaomiao_agent_service.{h,c}`，Kconfig 固定地址、Worker、响应上限、JSON 校验、3 秒失效）+ PC Monitor 接入（两页指标与滚动图、LVGL timer 每秒读快照刷新 CPU／RAM／GPU／温度，温度行按 GPU T／CPU T／TEMP 切换）。PC Agent 的 Python 单测 18/18 通过，固件静态复核通过；项目负责人确认固件构建、烧录与 CP5 手动场景全部通过，未提供新增日志。服务发现、AI 额度路由和其他 PC 数据端点不在本节点实现。

- MicroSD GPIO22 冲突修复已于 2026-09-21 21:56 通过人工复测：连续 9 次 `sdmmc_card_init failed (0x107)` 均未再出现 `gpio: conflict found for GPIO[22]`，Hardware Test 关闭、重开和返回 Launcher 正常。正常 SD 卡挂载尚未验证，节点 14 仍保持未完成。

- 未验证项汇总（均已在各自 Goal 中判定为不阻塞收口）：LED、电机、MPU6050 的实机行为无证据（等节点 16 的 GD32 `0x40` 协议）；节点 9 的 `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败无法在不改分区表的前提下构造，只按源码检查确认；节点 8 与节点 9 的末轮回归为人工口头确认，无补充串口日志。
- Launcher 导航改为网格 + 右键翻页（2026-09-20，非设计文档第 15 节的有序节点）：**已完成**。`←`／`→` 在行内换列，在右列按 `→` 翻到下一页同一行第一格、在左列按 `←` 翻回上一页同一行第二格；目标页没有对应行时回退到该页第一行第一格，只有目标页没有任何 App 才保持焦点（因此 5 个 App 时第 1 页第 2 行也能进入第 2 页）；`↑`／`↓` 在页内换行且不翻页。每页 4 格时从第 1 格按 `→` 两次即翻页。网格几何、每页容量、槽位的行优先对应关系、卡片布局与配色均未改。实机验证：自测固件输出 `LAUNCHER_SELF_TEST: PASS`（引导路径 13 步，数量边界含缺行回退断言），普通固件实机行为与五个 App、Hardware Test 15 页由人工确认无问题（无补充串口日志）。本变更经三次模型修订（首版“左右线性 ±1 且删掉上下键”→ “外侧列翻页 + 上下换行” → “翻页保持同一行” → 最终“同行优先、缺行回退”），施工文档为 `goals/20260920-2015-launcher-lr-paging.md`。
- 节点 10“Wi-Fi Service”已于 2026-09-21 完成。交付独立 Wi-Fi Service（状态快照、扫描、异步连接、1／2／5／10／30 秒退避重连、忘记网络、RSSI 采样）、受密码保护的 SoftAP + 手机网页配网、取得 IPv4 后才提交的私有凭据存储、Settings／Tools 接入、全局四档 Wi-Fi 图标、双缓冲自适应降级与自定义 4 MB Flash 分区表。人工构建、烧录及手动测试由项目负责人确认全部通过；新增确认未附日志的证据类型和仅静态复核的故障路径已在 Goal 中区分。施工、核对与验证记录见 `goals/20260920-2210-wifi-service.md`。
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
- [x] 节点 10：Wi-Fi Service。
- [x] 节点 11：PC Monitor 通信。
- [ ] 节点 12：Audio Service。（已推迟，2026-09-21 确认后面再做，等有真实消费者再启动）
- [ ] 节点 13：首个正式游戏。（已推迟，2026-09-21 确认不在本设备做游戏）
- [ ] 节点 14：Storage Service。
- [ ] 节点 15：Assets 与文件系统。
- [ ] 节点 16：GD32 `0x40` 协议补全。

## 下一步

1. GD32 `0x40` 协议补全（节点 16）完成前，LED、电机、MPU6050 的实机回归无法闭环；节点 4 已按设备缺失处理并记录，不重复阻塞后续节点。
2. **GPIO22 冲突修复已人工验证通过（2026-09-21 21:56）**：连续 9 次 `sdmmc_card_init failed (0x107)` 均无 GPIO22 冲突警告，Hardware Test 生命周期也正常。仍需插入已知良好 SD 卡验证 `MOUNTED`，以区分 `0x107` 是未插卡预期行为还是共享 SPI 总线问题。
3. 可选补录（均不影响已完成节点的结论，固件已在板上，重新 `idf.py -p COM5 monitor` 即可，无需重新编译或烧录）：节点 4 缺失设备页的实际显示文本与挂载失败后重新进入 App 的行为；节点 5 的“连续 10 轮”与“5 轮双 App 交替”样本；节点 8 与节点 9 末轮回归的补充串口日志。
4. 已实现的设计约束（供后续复核，非待办）：B 的短按／长按判定基于 `keypad_read_cb()` 的按下与释放边沿加独立状态机，动作在 LVGL 循环执行，`lv_indev_set_long_press_time()` 全局设置未改；Launcher 的“App 打开态转发 B”分支对已取得输入焦点的 App 不可达。细节见 `goals/20260920-1053-hardware-test-app.md` 与 `goals/20260920-1159-games-placeholder.md`。
5. **UI 中文本地化尚未立项**，建议排在节点 15「Assets 与文件系统」之后（或作为其第二阶段）；字库体积、字号与两条加载路径见下节「待确认与已知风险」。

## 待确认与已知风险

- **分区表已重划：app 2 MB + assets 1.5 MB（2026-09-21 确认）**：加 Wi-Fi 后 `build/xiaomiao.bin` 达 1,391,264 字节（1359 KB），上一版 1500 KB 的 app 槽只剩 141 KB，而 Flash 另有 2.41 MB 未进分区表。现改为项目自有 `partitions.csv`（经 IDF `gen_esp32part.py` 校验）：`nvs` 24 KB @ 0xA000 与 `phy_init` 4 KB 完全不变（已存 Settings 与 Wi-Fi 凭据不受影响），`factory` 由 1500 KB 扩到 **2 MB**，新增 **1.5 MB `assets`（data/spiffs）** 分区预留给节点 15 的字体/图标/音效资源，末尾留约 384 KB 未分配。这是节点 10「不修改分区表」边界的唯一例外，已获确认。因分区表文件变化，构建前必须重新生成被忽略的本地 `sdkconfig`。`assets` 分区在节点 15 落地前不挂载、不写入，因此对当前固件完全无害。
- **UI 中文本地化尚未立项**：建议排在节点 15「Assets 与文件系统」之后（或作为其第二阶段）。理由：① 全量中文字库不能进 app（app 内静态资源同样受 app 分区限制，现余量约 690 KB），应放进已预留的 1.5 MB `assets` 分区或 SD；② 节点 11～14 还会新增或改写界面文案，现在翻译会重复劳动；③ 中文需要 14/16 px 字号并逐页重排版。字库有两条路径：`assets` 分区 + `esp_partition_mmap`（零 RAM、不怕拔卡），或走节点 15 的 Assets／SD 机制。若只想先看效果，可只把现有文案做成子集字库（约 10～20 KB）静态编入 app。
- **Flash 与 PSRAM 容量已核对（2026-09-21）**：`esptool flash-id` 读出 `Manufacturer 20 / Device 4016 / 4MB`，芯片为 ESP32-D0WD rev v1.0；`Tools → System Info` 同时给出运行时读数 `Flash 4 MiB`，两条独立路径一致。**PSRAM 运行时可用量为 4 MiB**，而设计文档写的是 8 MB——这不是模块缺容量，而是 ESP32 的 PSRAM 映射窗口上限即 4 MB（8 MB 颗粒也只能用到 4 MB，除非做 bank 切换，IDF 默认不做）；后续规划大体积资源（字库、音频、Assets）时按 4 MiB 计算。4 MB Flash 中约 2.41 MB 未进分区表，可用于扩分区或新建 data 分区。另注：`Tools → System Info` 的 `CPU` 行打印的是 `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` 配置值而非运行时测量值。
- **节点 10 内部 RAM 预算偏紧（已确认取舍）**：Wi-Fi 驱动与 LCD 的全屏 DMA 缓冲共用内部 RAM（ESP32 的 SPI DMA 不能寻址 PSRAM），三块 40 KB 缓冲与 Wi-Fi 相加放不下，2026-09-21 实测第三块分配失败。已采取两项措施（Wi-Fi／LWIP 缓冲优先进 PSRAM、第三块失败时降级为双缓冲），并确认接受双缓冲。当前显示为 2 块全屏缓冲，代码仍保留“尝试第三块、失败降级”的自适应写法。内部内存余量仍然不大：后续若要恢复三缓冲、或再叠加 Service 与大缓冲，需先核算内部 RAM 并重新评估。
- **配网热点的已接受行为**：热点 SSID 固定为 `Xiaomiao-XXXX`，密码每次会话随机生成。手机若缓存过同名热点的旧密码，首次关联会使用旧密码并失败，需要重新输入设备屏幕上的本次密码。该行为是固定 SSID 与随机密码安全要求叠加的结果，已确认保持现状；定位与取舍见 `goals/20260920-2210-wifi-service.md` 第 17 条。
- **MicroSD／GPIO22 修复的冲突部分已验证通过（2026-09-21）**：手动拆分 SDSPI 初始化与 FATFS 挂载后，失败清理、成功卸载均在 `sdspi_host_remove_device()` 前复位 GPIO22；最新实机日志连续 9 次 `sdmmc_card_init failed (0x107)` 均无 GPIO22 冲突警告。README 引脚表中 GPIO22 仍仅分配给 SD CS，固件内无第二个使用者。正常 SD 卡挂载尚未验证，`0x107`（`ESP_ERR_TIMEOUT`）的卡不响应问题仍待已知良好卡区分，节点 14 不能因此标记完成。
- README 记录的 GD32 LED／电机协议已被 ESP32 代码使用，但 GD32 工程缺少对应实现，双 MCU 联调结果待确认。
- 本机 ESP-IDF 安装为非默认布局（venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下），需进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`；MSYS/Git Bash 会因 `MSYSTEM` 变量被 `export.ps1` 拒绝。环境细节与已验证事实见 `AGENTS.md`。
- PC Monitor 的指标已接入节点 11 的 Agent Service 快照：真实数据需在被忽略的本地 `sdkconfig` 配置 Agent IPv4、PC 端启动 `pc-agent/` 并与设备同处一个局域网；可提交配置地址为空，固件在该状态显示 `--` 且不受影响。节点 11 的 CP5 已由项目负责人确认通过；该确认未附新增串口日志、截图或资源数值。首版固定 Agent IPv4，不支持服务发现；后续 AI 用量监控将复用同一 HTTP Agent 和 `/api/v1/quotas/...` 路由，尚未立项。

## 历史记录

逐条的施工与验证流水（时间、事件、结论、对应施工文档）见 `goals/ROADMAP-history.md`。本文件在 2026-09-20 重构时把这两节迁出，以避免与 Goal 文档重复；重构前的完整原文见 `git log -p ROADMAP.md`。
