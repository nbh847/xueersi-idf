# 项目路线图

本文件是项目当前状态的唯一入口。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。

逐条的施工与验证流水（含串口日志分析、口径差异、证据类型差异和未验证范围）保留在各 Goal 施工文档中，完成后不删除；按日期索引与结论汇总见 `goals/ROADMAP-history.md`。本文件只保留当前状态与进度判定。

## 当前状态

- 固件基于 ESP-IDF 6.1 与 LVGL 9.5。普通固件开机进入 `main/framework/` 的 Launcher，按注册顺序显示 `PC Monitor`（显示名 `Monitor`／「监控」）、`Tools`、`Games`、`Settings` 与 `Hardware Test` 五个入口：前四项占满第 1 页 2 列 × 2 行网格（监控、工具、游戏、设置），`Hardware Test` 在第 2 页，启动焦点在监控。焦点移动：`←`／`→` 在行内换列，并在外侧列翻页——优先落到相邻页**同一行**，该页没有对应行时回退到它的**第一行第一格**，只有目标页没有任何 App 才不翻页；`↑`／`↓` 在页内换行，不翻页。其余越出网格的移动保持焦点、不循环。该模型于 2026-09-20 取代节点 3 决策 5 的“左右限本行、上下跨页”；注册顺序由 2026-10-01 收口的信息归位 Goal 调整。
- 分层：`main/framework/` 为 App Framework 运行时（节点 1～3，节点 10 新增挂在 top layer 的全局 Wi-Fi 图标，番茄时钟 Goal 新增其可见性接口），`main/apps/` 为业务 App（节点 5～8，节点 11 把 PC Monitor 接到 Agent Service 快照；AI 额度 Goal 把该 App 扩为 4 页并让 Agent Service 提供两个服务商的额度快照），`main/services/` 为 Service 层（节点 9 的 `Settings Service`，节点 10 的 `Wi-Fi Service` 及其配网网页、DNS 私有实现，节点 11 的 `Agent Service` 及 `pc-agent/` Python 后端，番茄时钟 Goal 的 `Pomodoro Service` 与 `Buzzer Service`——后者是 GPIO14／LEDC 蜂鸣器通道的唯一所有者；App 不直接访问 NVS，也不直接调用 `esp_wifi_*`、`esp_http_client`、cJSON 或 LEDC），`main/main.c` 持有 15 页 Hardware Dashboard 并注册为 `Hardware Test` App。BSP 分层尚未实现。
- Dashboard 覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统、About。
- ESP32 侧已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。GD32 仓库源码只实现 USB CDC、USART1 桥与 ESP32 IO0/EN 控制，未实现 I2C `0x40` 从机协议；但项目负责人于 2026-09-27 报告 Hardware Test 可在实机控制 LED1／LED2，表明板上控制链路可用，实际 GD32 固件与仓库源码的对应关系待查。
- Settings 的 `wifi_auto_connect` 已由节点 10 的 Wi-Fi Service 消费，用于控制开机自动连接与断线重连；`sound_enabled` 已作为番茄到点提醒的总开关生效，Settings 的 Sound 页仍只读；`pomodoro_sound_enabled` 与 `focus_minutes`／`break_minutes`（schema v3，含 v1／v2 原位迁移）由番茄时钟提醒与阶段时长消费，已实机验收。schema v4（2026-10-01，空闲待机 Goal）新增 `screen_idle_minutes`，由 Screen Idle 模块消费，**未实机验证**；Settings 的 Display 页已从只读改为菜单式，「息屏设置」子页提供待机时长五候选（待人工验收）。Settings 的 Wi-Fi 页已支持配网、自动连接和忘记网络，状态行可进入连接详情（状态、SSID、信号、IPv4，实时快照）；System 为系统信息、资源状态（SD 已挂载时显示容量 MB）、配置状态、关于四项子菜单。Tools 经信息归位改为单入口页，2026-10-01 番茄时钟 Goal 实施并验收后显示唯一「番茄时钟」入口（正圆计时页与番茄设置）；原有 Wi-Fi、System Info、Assets、About 已全部迁入 Settings。

## 当前开发节点

- **节点 0～11 与节点 14～15 均已完成并取得人工验收确认。** 节点 14 的项目负责人已确认 ESP-IDF 构建、烧录与 CP5 九个实机场景全部通过（2026-09-22 15:38）；节点 15 的 CP1～CP7 于 2026-09-25 闭环（豁免项见其 Goal 文档）。Agent 未重复执行固件构建、烧录或串口监视。已完成节点的交付内容、验证证据、口径差异与未验证范围见各自施工文档：

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
  | 14 Storage Service | `goals/20260922-0938-storage-service.md` |
  | 15 Assets、文件系统与 Flash 完整中文字库（15A～15D） | 总纲 `goals/20260922-1701-assets-filesystem-chinese-font.md`；`goals/20260922-1711-assets-filesystem-foundation.md`、`goals/20260922-1711-flash-chinese-font-runtime.md`、`goals/20260922-1711-ui-chinese-localization.md`、`goals/20260922-1711-node15-integration-validation.md` |

- 节点 11“PC Monitor 通信”于 2026-09-21 完成首版（施工文档 `goals/20260921-1238-pc-monitor-communication.md`）：统一 Python HTTP Agent、固件 Agent Service、API v1 校验、3 秒失效与 PC Monitor 指标显示。首版曾由 Kconfig 固定 Agent IPv4／端口；动态发现由当前补充 Goal 取代。`pc-agent/start.sh` 按执行系统选择 Python（macOS／Linux 用 `python3`，Windows Git Bash 用 `python`），并在 `.venv/<system>/` 中创建、复用对应环境。初版 PC Agent Python 单测 18/18 通过；首版固件构建与 CP5 人工验收已由项目负责人确认。

- **已完成：PC Agent 局域网动态发现补充**（`goals/20260927-1549-agent-service-discovery.md`）。固件与 PC 端发现协议已实施并完成软件侧检查；2026-09-27 已通过首个局域网实机验收（设备动态发现 Agent、HTTP 200、CPU／内存正常显示），Agent 停止后指标失效、重启后自动恢复。2026-09-28 项目负责人确认收口，更换 Wi-Fi 环境后的复验不再保留为未完成项。适用范围是每次设备与电脑位于可互通的同一局域网，暂不处理跨子网发现。

- **监控 App 智谱与 Codex 额度已于 2026-09-27 完成并取得人工验收确认**（设计 `docs/monitor-ai-quota-design.md`；施工 `goals/20260927-1855-monitor-ai-quotas.md`）。PC Agent 新增两个只读额度端点（`/api/v1/quotas/codex`、`/api/v1/quotas/zhipu`，健康端点能力列表同步），固件 Agent Service 新增共享发现链路、额度快照与独立低频 Worker（180 秒过期，与 PC 指标的 1 秒轮询／3 秒失效互不影响），监控 App 扩为四页（第 3 页智谱 `5H`／`1W`、第 4 页 Codex `5H`／`7D`）并把 Launcher 显示名改为「监控」／`Monitor`，i18n 增至 217 个文案 ID。软件侧验证：102 项 Python 用例（含只读缓存、响应无凭据、失败后恢复、同一时间采样四条口径）、本机 HTTP 冒烟两个端点 `status=ok`、倒计时边界宿主编译核对、字库覆盖与静态检查。实机验收：第一轮照片确认第 4 页 Codex 显示正确，同时暴露三处缺陷——`RAM` 被截成 `RA`（28 px 名称框装不下 Montserrat 12 的 ≈27.8 px，折行第二行被裁）、额度页状态文字与右上 Wi-Fi 图标重合（状态框右边界到 156，图标占 140..158）、四页标题底部被裁（标题框 14 px 小于 16 px 档字体的 19 px 行高，折行后裁切）；已分别改为每项指标一个 78 px 单标签（`名称 数值 单位`）并 `CLIP`、状态右边界收到 136、标题框加高到 20 px。项目负责人于 2026-09-27 复测后确认「功能没问题，都验证过了」。证据类型：人工确认，第一轮附实机照片，最终确认与第二轮反馈未附串口日志。

- **已完成：空闲待机与动态小电视**。2026-10-01 16:22 负责人确认构建、烧录及待机实机测试全部通过（口头确认，未附新日志或照片）。默认 2 分钟；Settings → Display → 息屏设置可选关闭／1／2／5／10 分钟，schema v4 保值迁移 v1／v2／v3；六键唤醒并吞键至全部释放，原页面、焦点和后台服务保留。单圈浅白小电视约 104 × 90 px 居中，50 ms timer 推进眨眼、天线抬落与小嘴动作。接手重新编译真实源码的状态机及配置迁移 10 步宿主检查均 PASS（ASan／UBSan 无报错）；独立自测固件、量化内存与失败注入没有新增实机证据。详情见 `goals/20261001-1449-screen-idle.md`；设计与预览见 `docs/screen-idle-design.md`、`docs/screen-idle-preview.html`。背光仍常亮。


- **暂缓：Launcher 电池电量显示**（`docs/battery-indicator-design.md`；记录 `goals/20261001-1206-battery-indicator-design.md`）。2026-10-01 用户决定暂不开发，左上角保留 `Xiaomiao`，不替换为电量或未知占位。原理图与源码未发现已确认的电池读取通道，实板未验证；保留调研与候选方案，恢复开发需用户重新明确要求。

- **已完成：Launcher 顺序与 Tools／Settings 信息归位**（设计 `docs/launcher-tools-settings-reorganization-design.md`；施工 `goals/20260930-2146-launcher-settings-reorganization.md`）。2026-09-30 22:12 完成 CP1～CP4 代码实施：Launcher 注册顺序改为 `pc_monitor/tools/games/settings/hardware_test`（Games 第一页第三格，启动焦点在监控）；Settings 的 Wi-Fi 状态行进入连接详情（迁移自 Tools，实时快照每秒刷新），System 改为系统信息／资源状态／配置状态／关于四项子菜单；Tools 重写为「暂无工具」过渡页；i18n 追加 4 个文案 ID。2026-10-01 两轮实机验收通过（第一轮反馈资源状态页 SD 行改为已挂载时显示真实容量 MB，复验通过），负责人确认全部正常；证据类型为人工确认，未附串口日志或照片，英文回退未做损坏注入实机演示（静态核对）。CP6 文档已同步收口。

- **已完成：Tools 番茄时钟**（设计 `docs/pomodoro-timer-design.md`；施工 `goals/20261001-1036-tools-pomodoro.md`）。2026-10-01 用户以 `/goal` 授权完成功能开发，当日 13:30 负责人实机验收通过，13:38 接手复核源码与文档并再次依据负责人验收确认收尾（口头确认，未附串口日志或照片；Settings 自测固件未单独运行，迁移逻辑以源码与宿主检查覆盖）。交付：CP0 收口（详情页 `↑` 进番茄设置不暂停计时；手动硬件测试优先，提醒不排队不补响）；独立 `Pomodoro Service`（64 位单调钟截止时间差、跨 App 继续计时、单次完成事件，阶段时长在开始时从 Settings 读取；**专注到点自动进入休息**，休息完成后停在「再来一轮」页，重置即按即生效）与 `Buzzer Service`（GPIO14／LEDC 唯一所有者，3 声 988 Hz 非阻塞提醒，Hardware Test 迁移到同一接口）；Settings Service 升级 schema v3（新增 `pomodoro_sound_enabled` 与 `focus_minutes`／`break_minutes`，v1／v2 blob 原位迁移保留原值，时长范围 1～180／1～60 分钟、默认 25／5）并把自测扩为 8 步；Tools 重写为菜单＋正圆计时页（86 px 圆环、中央「圆润玻璃」沙漏——贝塞尔采样 21 点折线，沙子为 6 条 2 px 实心阶梯条＋落沙＋10 帧翻转、左右 16 px 数字内移至 x=55／105）＋番茄设置三行（时长编辑与到点声音，`↑↓` 选行、`←→` 调分钟、离开行或 B 持久化、写失败回退并提示）；`↑` 进设置、B 逐层返回，计时相关视图隐藏全局 Wi-Fi 图标、退出恢复；设置行与详情选项行接入光带扫入特效（全局规则见 `AGENTS.md` UI 交互约定）；i18n 增至 234 个文案 ID。宿主可控时钟聚焦检查 41 项断言全部通过（`POMODORO_HOST_CHECK: PASS`），`git diff --check` 干净。验收过程修复四轮构建/实机暴露的问题（编译错误 5 处、`HG_PTS` 零填充点画出穿屏斜线、沙子形态、选项框类型混淆闪退、设置页行值错位），详见施工文档「实施记录」。

- **已完成：方向键焦点切换光带扫入动效**（设计 `docs/directional-focus-transition-design.md`；施工 `goals/20260928-1037-directional-focus-sweep.md`）。2026-09-28 完成固件实现并当日收口：140 ms 浅白光带只在目标卡片／行内扫过（Launcher 12 px、Settings／Tools 8 px，初始值），静态焦点先更新，光带从旧项所在一侧扫入；快速连按取消前段，A 中断、页面销毁前均先删动画再删对象。首轮实机确认 Launcher 与 Settings 特效正常；同轮反馈 Tools 菜单无特效（原范围未覆盖），补接入 `xiaomiao_tools.c` 后由项目负责人烧录复验收口（「没问题了，我测过了」，附串口日志：三 App 开合 `screen children=2` 恒定、无新增错误）。未附动效视频，效果判定以负责人目视为准；参数未做调整。

- **PC Monitor Y 轴上限标签修正已实机通过**。`100` 标签框从 18 px 加宽至 24 px，图表起点相应右移；负责人烧录后确认标签完整显示，量程和数据限幅保持 `0–100`。

- 节点 14“Storage Service”已于 2026-09-22 完成（施工文档 `goals/20260922-0938-storage-service.md`）：MicroSD 生命周期迁到独立 `main/services/xiaomiao_storage_service.{h,c}`，`main/main.c` 启动链在 `lcd_init()` 后自动尝试首次挂载，Hardware Test MicroSD 页改用 Service 快照与挂载／卸载接口。CP5 九个实机场景全部通过（2026-09-22 15:38）：无卡冷启动与 10 次重试无 GPIO22 冲突、插卡挂载成功（屏幕 MOUNTED + SD 29818MB）、带卡冷启动直接进 MOUNTED、卸载与幂等重挂、15 页往返与五 App 烟测无回归。`cmd=5 R1 illegal command` 确认为 IDF v6.1 `sdmmc_init.c` 标准 SDIO 探测步骤、microSD 拒绝 CMD5 属预期正常；无卡时的 `0x107`（`ESP_ERR_TIMEOUT`）为卡不响应的预期行为，与共享 SPI 总线问题已区分。
- MicroSD GPIO22 冲突修复已于 2026-09-21 21:56 通过人工复测并在节点 14 再次确认：连续 9 次 `sdmmc_card_init failed (0x107)` 均未再出现 `gpio: conflict found for GPIO[22]`，节点 14 的 10 次无卡重试同样无冲突。历史 Goal 与流水保留故障演进记录，不再把 GPIO22 冲突列为当前未解决问题。

- 未验证项汇总（均已在各自 Goal 中判定为不阻塞收口）：电机未接入，实机行为尚不可测；MPU6050 实机行为无证据。项目负责人于 2026-09-27 13:36 报告 Hardware Test 已能控制两颗 LED（无日志）；当前仓库 GD32 源码不含相应 I2C 处理，实际板载固件来源待查。节点 9 的 `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败无法在不改分区表的前提下构造，只按源码检查确认；节点 8 与节点 9 的末轮回归为人工口头确认，无补充串口日志。
- Launcher 导航改为网格 + 右键翻页（2026-09-20，非设计文档第 15 节的有序节点）：**已完成**。`←`／`→` 在行内换列，在右列按 `→` 翻到下一页同一行第一格、在左列按 `←` 翻回上一页同一行第二格；目标页没有对应行时回退到该页第一行第一格，只有目标页没有任何 App 才保持焦点（因此 5 个 App 时第 1 页第 2 行也能进入第 2 页）；`↑`／`↓` 在页内换行且不翻页。每页 4 格时从第 1 格按 `→` 两次即翻页。网格几何、每页容量、槽位的行优先对应关系、卡片布局与配色均未改。实机验证：自测固件输出 `LAUNCHER_SELF_TEST: PASS`（引导路径 13 步，数量边界含缺行回退断言），普通固件实机行为与五个 App、Hardware Test 15 页由人工确认无问题（无补充串口日志）。本变更经三次模型修订（首版“左右线性 ±1 且删掉上下键”→ “外侧列翻页 + 上下换行” → “翻页保持同一行” → 最终“同行优先、缺行回退”），施工文档为 `goals/20260920-2015-launcher-lr-paging.md`。
- 节点 10“Wi-Fi Service”已于 2026-09-21 完成。交付独立 Wi-Fi Service（状态快照、扫描、异步连接、1／2／5／10／30 秒退避重连、忘记网络、RSSI 采样）、受密码保护的 SoftAP + 手机网页配网、取得 IPv4 后才提交的私有凭据存储、Settings／Tools 接入、全局四档 Wi-Fi 图标、双缓冲自适应降级与自定义 4 MB Flash 分区表。人工构建、烧录及手动测试由项目负责人确认全部通过；新增确认未附日志的证据类型和仅静态复核的故障路径已在 Goal 中区分。施工、核对与验证记录见 `goals/20260920-2210-wifi-service.md`。
- 实机 LED1／LED2 已确认可控；板载 GD32 固件与仓库源码的对应关系仍待查。电机未接入，行为不可实测；该项不阻塞不依赖 LED／电机的 v0.1 框架开发。

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
- [x] 节点 14：Storage Service。
- [x] 节点 15：Assets、文件系统与 Flash 完整中文字库。（总纲 `goals/20260922-1701-assets-filesystem-chinese-font.md`；15A～15D 的 CP1～CP7 于 2026-09-25 全部闭环：两套自测实机 `PASS`、无 SD 中文目视通过、26 组开合无泄漏、有 SD 对照、全 UI 回归和 merged bin 从 `0x0` 烧录后中文正常；字体损坏英文回退注入经负责人决定跳过，不作通过项。烧录轮次发现的配网回归已在独立 Goal 修复并完成连续两次换网验证。）
- [ ] 节点 16：GD32 `0x40` 协议源码补齐与实机固件差异核对。2026-09-27 调研确认原理图芯片为 `GD32F350G8U6TR`；仓库及对应公开源码目录均未找到 I2C 从机实现，公开 Release 也未找到单独标注的 GD32 镜像。`letsgo.bin` 是否包含 GD32 固件未知。LED1／LED2 实机可控（负责人报告，无日志），与当前源码的对应关系待确认；电机未接入，无法实测。

## 下一步

三项 UI 工作的实施顺序已由项目负责人确认：**Launcher 顺序与 Tools／Settings 信息归位 → Tools 番茄时钟 → 空闲待机画面**。每步独立实施并完成人工验收后再进入下一步；信息归位与番茄时钟均已完成并验收（2026-10-01），空闲待机也已于 2026-10-01 16:22 根据负责人全部通过确认收口。

- ~~信息归位~~：**已完成并验收（2026-10-01）**，入口与页面结构稳定；其时 Tools 为「暂无工具」过渡页，已被随后的番茄时钟实施取代；记录见上方已完成条目。设计见 `docs/launcher-tools-settings-reorganization-design.md`。
- ~~番茄时钟~~：**已完成并验收（2026-10-01 13:30）**。专注到点自动进入休息、休息完成「再来一轮」、时长编辑（schema v3）、到点声音两级开关、光带特效；负责人口头确认通过，未附串口日志或照片，Settings 自测固件未单独运行。记录见上方已完成条目与施工文档「实施记录」。
- ~~空闲待机画面~~：**已完成并验收（2026-10-01 16:22）**，三项 UI 工作均已收口。交付与证据边界见 `goals/20261001-1449-screen-idle.md`。

1. 节点 16 先取得板载 GD32 固件来源或可核对的固件读取结果，查明其与仓库源码的差异，再补齐 `0x40` 处理；电机协议按 README 与 ESP32 发送格式核对实现。电机未接入，实机动作保持未验证；MPU6050 实机行为另行标记为未验证。
2. 节点 16（GD32 `0x40` 协议源码补齐与实机差异核对）可按计划继续；节点 12（Audio Service）与节点 13（首个正式游戏）继续推迟。节点 15 与配网回归均已收口，证据分别见节点 15 总纲和 `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`。
   后续固件人工复验可直接执行 `idf.py -p COM5 flash monitor`：`flash` 会先按需构建，构建成功后烧录并进入串口监视，无需单独预先运行 `idf.py build`。
3. 可选补录（均不影响已完成节点的结论，固件已在板上，重新 `idf.py -p COM5 monitor` 即可，无需重新编译或烧录）：节点 4 缺失设备页的实际显示文本与挂载失败后重新进入 App 的行为；节点 5 的“连续 10 轮”与“5 轮双 App 交替”样本；节点 8 与节点 9 末轮回归的补充串口日志。
4. 已实现的设计约束（供后续复核，非待办）：B 的短按／长按判定基于 `keypad_read_cb()` 的按下与释放边沿加独立状态机，动作在 LVGL 循环执行，`lv_indev_set_long_press_time()` 全局设置未改；Launcher 的“App 打开态转发 B”分支对已取得输入焦点的 App 不可达。细节见 `goals/20260920-1053-hardware-test-app.md` 与 `goals/20260920-1159-games-placeholder.md`。

## 待确认与已知风险

- **SoftAP 配网回归已收口（2026-09-25，两次连续换网已通过）**：
  - **HTTPD 与内存**：旧双缓冲固件曾在第二会话出现 `ESP_ERR_HTTPD_TASK (0xb008)`。当前单缓冲固件连续两次启动 HTTPD 成功，第二次 `largest8bit=36864`；两次停止后的内部 free 分别为 70619 和 73535 字节，未见单调下降。同条件长期内存回归及显示刷新表现仍待验证。
  - **第二会话 DHCP 与换网**：此前手机停在“寻找 IP”。在 AP netif 销毁前显式停止 DHCP 的新固件上，两次会话均向手机分配 `192.168.4.2`，完成网页扫描、目标网络 `CMCC-5pu4`／`CMCC-U2Tx` 连接、STA IPv4 获取和凭据保存，会话均正常关闭。两次换网流程已由人工日志验证；旧 UDP/67 遗留机制与修复效果吻合，但没有抓包或底层对象证据证明旧 PCB 确实遗留。详情见 `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`。
  - 另注：merged bin 从 `0x0` 整段擦写会清掉 NVS（Wi-Fi 凭据与 Settings，已由 `has_credentials=0` 证实），属单文件重装的预期代价。因此诊断期间使用不会写 NVS 的普通 `idf.py -p COM5 flash`，不要再从 `0x0` 烧录 merged bin。细则见 `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`。

- **分区表：app 2 MB + assets 1.5 MB**：项目自有 `partitions.csv` 保留 `nvs` 24 KB @ `0xA000` 与 `phy_init` 4 KB，`factory` 为 2 MB，`assets` 为 1.5 MB `data/spiffs`。节点 15 已将完整 GB2312 字库及 manifest 写入资源镜像，并验证普通 flash 与 merged bin 烧录；从 `0x0` 写 merged bin 会覆盖 NVS，需重新配网。分区和镜像验收细节见节点 15 总纲与 15D Goal。
- **UI 中文本地化（15C）已实施，核心显示已实机确认（2026-09-24/25）**：生产中文字库位于本机 1.5 MB `assets` SPIFFS 分区，不依赖 SD。范围固定为完整 GB2312（6,763 汉字 + 682 符号），12／16 px A2 两档，XMF1 整包 PSRAM 驻留读取，字形落位缺陷已修复并复验；默认中文，字体挂载、校验或读取失败时切回英文文案和 Montserrat（损坏注入回退观察经负责人决定跳过，未实机演示）。五 App 中文与 26 组开合已目视通过；160 × 128 逐页布局的 Hardware Test 15 页等剩余页面归入 CP6 全 UI 逐项回归。完整方案与验收标准见 `goals/20260922-1701-assets-filesystem-chinese-font.md`。
- **Flash 与 PSRAM 容量已核对（2026-09-21）**：`esptool flash-id` 读出 `Manufacturer 20 / Device 4016 / 4MB`，芯片为 ESP32-D0WD rev v1.0；`Tools → System Info` 同时给出运行时读数 `Flash 4 MiB`，两条独立路径一致。**PSRAM 运行时可用量为 4 MiB**，而设计文档写的是 8 MB——这不是模块缺容量，而是 ESP32 的 PSRAM 映射窗口上限即 4 MB（8 MB 颗粒也只能用到 4 MB，除非做 bank 切换，IDF 默认不做）；后续规划大体积资源（字库、音频、Assets）时按 4 MiB 计算。4 MB Flash 中约 2.41 MB 未进分区表，可用于扩分区或新建 data 分区。另注：`Tools → System Info` 的 `CPU` 行打印的是 `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` 配置值而非运行时测量值。
- **内部 RAM 预算偏紧（两次配网已实机验证）**：节点 10 时三块 40 KB 显示 DMA 缓冲与 Wi-Fi 共存失败，曾接受两块；本次 HTTPD 运行期间出现 AP 关联响应发送失败。当前单块 40,960 字节全屏 DMA 缓冲释放内部 RAM，人工日志确认连续两次配网成功；第二次 HTTPD 启动后 `largest8bit=36864`，两次停止后 free 未单调下降。显示刷新性能、更多轮同条件内存回归和重启持久化待验证；历史双缓冲验收仍保留在节点 10 Goal。
- **配网热点的已接受行为**：热点 SSID 固定为 `Xiaomiao-XXXX`，密码每次会话随机生成。手机若缓存过同名热点的旧密码，首次关联会使用旧密码并失败，需要重新输入设备屏幕上的本次密码。该行为是固定 SSID 与随机密码安全要求叠加的结果，已确认保持现状；定位与取舍见 `goals/20260920-2210-wifi-service.md` 第 17 条。
- **MicroSD／GPIO22 冲突已闭环（2026-09-22）**：手动拆分 SDSPI 初始化与 FATFS 挂载后，失败清理、成功卸载均在 `sdspi_host_remove_device()` 前复位 GPIO22；节点 14 的 10 次无卡重试全程无 `gpio: conflict found for GPIO[22]`。README 引脚表中 GPIO22 仍仅分配给 SD CS，固件内无第二个使用者。正常 SD 卡挂载已于 2026-09-22 验证通过（29818MB 卡成功挂载），无卡时的 `0x107`（`ESP_ERR_TIMEOUT`）确认为卡不响应的预期行为，与共享 SPI 总线问题已区分。
- README 记录的 GD32 LED／电机协议已被 ESP32 代码使用；项目负责人报告两颗 LED 已可由实机控制，但 GD32 工程缺少对应 I2C 实现，板载固件与仓库源码对应关系待查。电机未接入，实机行为未验证。
- 本机 ESP-IDF 安装为非默认布局（venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下），需进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`；MSYS/Git Bash 会因 `MSYSTEM` 变量被 `export.ps1` 拒绝。环境细节与已验证事实见 `AGENTS.md`。
- PC Monitor 通过 Agent Service 获取指标；当前固件会在 Wi-Fi 取得 DHCP IPv4 后广播发现 PC Agent。电脑需运行 `pc-agent/`，并与设备处于可互通的同一局域网；发现需允许 TCP `8766` 与 UDP `8767` 入站。动态发现、Agent 停止／恢复已通过目标板实机验证，该 Goal 已于 2026-09-28 收口。AI 用量监控（智谱／Codex 额度）已复用同一 HTTP Agent 和 `/api/v1/quotas/...` 路由实现，并于 2026-09-27 完成实机验收（见 `goals/20260927-1855-monitor-ai-quotas.md`）。

## 历史记录

逐条的施工与验证流水（时间、事件、结论、对应施工文档）见 `goals/ROADMAP-history.md`。本文件在 2026-09-20 重构时把这两节迁出，以避免与 Goal 文档重复；重构前的完整原文见 `git log -p ROADMAP.md`。
