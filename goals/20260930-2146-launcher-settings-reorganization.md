# Goal：Launcher 顺序与 Tools／Settings 信息归位

状态：已完成并收口（2026-10-01 09:09）。CP1～CP4 代码实施、CP5 实机验收、CP6 文档交付全部完成；证据类型为负责人人工确认（两轮文字回复），未附串口日志或照片。

创建时间：2026-09-30 21:46（北京时间）。

## 目标与背景

依据 `docs/launcher-tools-settings-reorganization-design.md`，把设备状态、设置与诊断信息统一归入 Settings，调整 Launcher 第一页顺序，为下一步 Tools 番茄时钟提供稳定入口。本任务是已确认 UI 实施顺序的第 1 步，后续依次为番茄时钟、空闲待机画面；每步单独验收。

施工前基线：Launcher 顺序为 Games、监控、Tools、Settings、Hardware Test；Tools 有 Wi-Fi、System Info、Assets、About 四项，Settings 的 System 是配置状态详情。当前状态以 `ROADMAP.md` 为准，硬件事实见 `README.md`，代码入口见 `docs/project-overview.md`。

## 范围与禁止事项

- 后续实施允许修改：`main/main.c` 的普通固件 App 注册顺序，`main/apps/tools/xiaomiao_tools.c`、`main/apps/settings/xiaomiao_settings.c` 及必要的对应头文件，`main/framework/xiaomiao_i18n.{h,c}`，本 Goal、设计文档与受影响的项目文档。
- 复用既有 Service 公开接口、字体、焦点样式与光带动效规则。新增 System 子菜单和 Wi-Fi 详情入口的焦点切换沿用现有行为；页面切换、关闭前停止光带并释放对象，不重构公共动画模块。
- 不实施番茄时钟或待机功能，不提前显示不可用的番茄入口；Tools 本阶段验收为“暂无工具”。
- 不修改 Service 业务语义、NVS schema、凭据、分区表、引脚、GD32 固件、PC Agent、监控业务页、Games 页面或 Hardware Test 行为；不新增依赖、FreeRTOS 任务或第二条输入链。
- 创建时授权仅为文档编写；后续实施与验收已由负责人安排另一 Agent 完成，记录见下文。本次收尾核对代码与文档，不提交、推送或进行外部写入。

## 已确定的行为

1. Launcher 注册顺序改为 `pc_monitor/tools/games/settings/hardware_test`。第一页左上监控、右上工具、左下游戏、右下设置，第二页硬件测试；启动焦点在监控。保留 App ID、两页布局与现有方向导航、边界和跨页回退规则。
2. Settings 顶层仍为 Wi-Fi、显示、声音、系统四项。Wi-Fi 保留状态、自动连接、配网、忘记网络，状态行可进入“连接详情”，显示状态、SSID、信号强度、IPv4。
3. Wi-Fi 详情在打开期间读取最新 Service 快照；断线清空旧 IP 和信号，不显示陈旧连接信息。返回 Wi-Fi 页时恢复对应焦点，不改变配网与凭据保存流程。
4. System 改为四项子菜单：系统信息、资源状态、配置状态、关于。分别承接 Tools 的系统数据、资源数据、Settings 原有配置来源与错误摘要、Tools 的项目信息。每项只有一个 Settings 入口，Tools 不保留隐藏入口。
5. 系统信息保留芯片、CPU 配置频率、Flash、PSRAM、ESP-IDF、固件版本；CPU 不表述为运行时测量。资源状态保留资源分区、容量、使用量、字体、字形数、SD 状态，仅查询、不触发挂载。数据不可用时保留现有 Unknown／None 等降级语义。
6. B 每次按下仅返回一层：系统详情 → System 子菜单 → Settings 顶层 → Launcher；Wi-Fi 连接详情 → Wi-Fi 页 → Settings 顶层。沿用释放锁存，长按不连续退出多层。同一打开会话内保留父菜单位置与焦点；不新增跨关闭或跨重启持久化。
7. Tools 保留注册、图标与生命周期，本阶段仅显示“暂无工具”，B 返回 Launcher；A 与方向键不会进入旧页面或不可用功能。番茄时钟由下一份独立 Goal 实施。
8. 新页面使用现有中文／英文文案表与字体令牌，字库失败时按既有规则整表英文回退。160 × 128 页面须完整显示诊断字段，并避开右上 Wi-Fi 图标；优先调整布局，不以裁掉字段通过验收。

## 施工检查点

| 检查点 | 工作与预期结果 | 验证方式 |
| --- | --- | --- |
| CP1 注册顺序 | 五个 App 顺序符合目标，游戏在第三格，硬件测试在第二页 | 静态核对注册与 ID；人工检查启动焦点、导航与返回焦点 |
| CP2 Settings 信息迁入 | Wi-Fi 详情与 System 四项均可达，原配置状态完整保留，数据来源真实 | 静态核对 Service 边界；人工检查连接／断线、字段与页面布局 |
| CP3 返回、焦点与动效 | 多层 B 每次仅退一层，父菜单焦点恢复，快速按键及 A／B 中断无残留 | 静态核对释放锁存、timer、动画与对象清理；人工按键与页面往返 |
| CP4 Tools 过渡页与文案 | 仅显示“暂无工具”，旧详情无入口，中英文文案完整 | 核对静态引用、事件入口与构建列表后清理本次产生的孤儿逻辑；人工目视与 B 返回 |
| CP5 构建与回归 | 构建、烧录、五 App 与 Hardware Test 回归正常 | 负责人执行固件验证，Agent 复核日志、照片与人工确认 |
| CP6 文档交付 | 项目文档准确反映已实现内容及证据，未验证事项如实保留 | 更新 README、项目概览、ROADMAP、历史索引与本 Goal；`git diff --check` |

CP1～CP4 在代码施工后做静态复核，再进行 CP5；没有人工构建与实机证据不得完成 CP6 收口。

## 验收清单与失败路径

以下各项由负责人实机验收确认（2026-10-01 两轮人工确认，未附串口日志或照片；「10 次开合」「串口无新增错误」按整体确认接受，英文回退按约定仅静态核对）：

- [x] 首页顺序、启动焦点、游戏第三格、第二页入口正确，左右翻页与上下移动无回归。
- [x] Wi-Fi 连接详情完整；打开详情期间断线后旧 IP／信号消失，恢复连接后显示新快照；自动连接、配网、取消配网、忘记网络保持原有行为。
- [x] System 四页内容完整，配置来源与 NVS 错误摘要没有丢失；资源／SD 不可用时正确降级且不发起挂载。
- [x] System 四页逐一返回正确；Wi-Fi 详情返回正确；长按 B 不连退，父菜单焦点不重置。快速方向键及动效中 A／B 无旧光带残留、错开页面或无效对象访问。
- [x] Tools 只显示过渡空状态，旧四项不可达，B 可返回；不以番茄入口作为本阶段完成条件。
- [x] 中文布局完整，英文文案与回退静态核对完成；英文回退未实际运行，已记录为未实机验证，未制造生产字体损坏。
- [x] 人工构建与烧录通过；五 App 各完成进入／返回，Settings 与 Tools 各至少 10 次开合，串口无新增错误且 `screen children` 不增长；Hardware Test 15 页与长按 B 返回无回归。
- [x] 静态检查与 `git diff --check` 通过，受影响文档已更新，未验证范围与证据类型明确。

入口丢失、字段遗漏、陈旧网络信息、B 连退、焦点错位、动画残留、对象数量增长或新增错误均视为验收失败，定位并修复后复验相关范围；不得用弱化显示、删除字段或放宽返回规则绕过。电机与 MPU6050 的既有实机缺口不纳入本任务新增通过项。

## 人工验证命令与证据

负责人在仓库根目录、加载 ESP-IDF 6.1 环境后执行（COM5 替换为实际串口）：

```bash
idf.py -p COM5 flash monitor
```

`flash` 会按需构建。预期构建与烧录成功、五 App 注册、启动进入新顺序 Launcher；提交首页与新详情页照片、关键开合及异常日志、逐项按键结果。Agent 只做源码与 diff 静态检查、复核人工证据，不主动编译、烧录、监视或占用串口。缺少证据的条目保持未验证。

## 当前交付与未验证范围

- 2026-09-30 21:46：已创建施工文档，统一本阶段 Tools 空状态验收口径，并在路线图与历史索引登记。
- 2026-09-30 22:12：CP1～CP4 代码实施完成并做静态复核，未执行构建、烧录或实机验证。交付内容：
  - CP1：`main/main.c` 的 `launcher_boot()` 注册顺序改为 `pc_monitor` → `tools` → `games` → `settings` → `hardware_test`，App ID 与注册表逻辑未动；框架按注册顺序枚举，无硬编码顺序依赖。
  - CP2：`main/apps/settings/xiaomiao_settings.c` 新增 Wi-Fi 连接详情视图（状态行进入，迁移自 Tools 的四行只读页，进入时读新快照、复用既有 20 ms timer 每秒刷新、断线清空旧 IP／信号）；System 改为四项子菜单（系统信息、资源状态、配置状态、关于），系统信息／资源状态／关于从 Tools 原实现迁移（保留 Unknown／None 降级与 CPU 为配置频率口径），配置状态沿用原 Settings Service 持久化状态页；`main/main.c` 之外的 Service 边界未动，未引入 `esp_wifi_*`／NVS 直接调用。
  - CP3：`settings_handle_escape()` 按视图逐层返回（Wi-Fi 详情 → Wi-Fi 页 → 顶层 → Launcher；System 详情 → System 子菜单 → 顶层 → Launcher），沿用 B 按下锁存与 20 ms 释放检测；父菜单焦点索引（`s_menu_index`／`s_wifi_index`／`s_system_index`）仅在 open／close 重置，会话内往返保留；视图重建前先 `settings_band_stop()`，光带动效规则未改。
  - CP4：`main/apps/tools/xiaomiao_tools.c` 重写为过渡空状态页（标题 + “暂无工具” + B 返回），移除四项菜单、全部详情页、Wi-Fi／Assets 轮询与光带代码及对应 include；注册、图标、生命周期与 B 释放锁存保留。`xiaomiao_tools.h`、`xiaomiao_settings.h` 注释同步更新。
  - i18n：`xiaomiao_i18n.{h,c}` 追加 4 个文案 ID（`XM_TEXT_TOOLS_EMPTY`「暂无工具／No tools」、`XM_TEXT_WIFI_DETAILS`「连接详情／Details」、`XM_TEXT_SETTINGS_RESOURCES`「资源状态／Resources」、`XM_TEXT_SETTINGS_CONFIG_STATUS`「配置状态／Config」），追加在枚举尾部不影响既有 ID 取值；中英双表同步，既有 ID 含义未改。`XM_TEXT_TOOLS_ASSETS` 等旧 ID 暂无引用，按“不改既有 ID 含义”原则保留在表中。
  - 静态检查：`git diff --check` 通过；`settings_build_wifi_details` 前置声明补齐；无孤儿引用（tools 旧函数全为 static，全仓 grep 无残留）。
- 本次未委派子 Agent，未执行 `idf.py`、烧录或串口监视，未提交。
- 2026-10-01 09:02：第一轮实机验收反馈处理。负责人确认首页新顺序（CP1）、Wi-Fi 连接详情与 System 页面结构、多层 B 返回、Tools 空状态均正常；提出资源状态页 SD 行在已挂载时应显示容量（单位 MB）。已改 `settings_build_assets()`：`XIAOMIAO_STORAGE_MOUNTED` 且容量大于 0 时显示真实容量（`capacity_bytes` 换算 MB，DOTS 模式防溢出），其余状态沿用原状态词，仍不触发挂载。该改动需重新烧录复验资源状态页。i18n 机器侧验证口径与负责人对齐：浏览全部新页面确认中文显示正常、无 `???` 标记即为通过；英文回退路径按节点 15 先例不做损坏注入实机演示，以静态核对为准。
- 2026-10-01 09:09：CP5 收口与 CP6 文档交付。负责人烧录复验后确认「验收过了，都正常」：资源状态页 SD 行已挂载显示真实容量（MB），i18n 全部新页面中文正常，其余检查点维持第一轮结论。CP6 同步更新本 Goal、`ROADMAP.md`、`README.md`、`docs/project-overview.md` 与 `goals/ROADMAP-history.md`，`git diff --check` 通过。
- 证据类型说明：本 Goal 全部实机结论均为负责人人工确认（2026-10-01 两轮文字回复），未附串口日志、照片或 `screen children` 计数输出；「Settings／Tools 各至少 10 次开合」「串口无新增错误」两项按负责人整体确认接受，未单独取证，与节点 8／9 末轮回归的口径一致。英文回退路径按约定未做损坏注入实机演示，仅静态核对，保持「未实机验证」记录。

- 2026-10-01 09:18：接手收尾复核实际 diff：注册顺序、System 四项入口、Wi-Fi 详情每秒快照与断线清空、B 返回层级及释放锁存、页面重建前光带清理、Tools timer／group 释放与新增中英文文案均与目标一致；未发现明确新增代码问题。同步项目 AGENTS 的 Tools 定位，清理设计与路线图的旧阶段措辞。`git diff --check` 通过；未重复固件构建或实机验证，原人工验收与证据限制保持不变。
