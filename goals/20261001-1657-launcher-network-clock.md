# Launcher 联网日期时间施工清单

状态：已完成并验收。2026-10-01 16:57 用户接受设计并要求生成 Goal 文档；同日用户经 `/goal` 授权实施，CP0～CP3 完成（17:49）；2026-10-01 18:21 用户确认构建、烧录及实机场景「都确认过了，没问题」（口头确认，未附串口日志或照片），CP4 收口；CP5 文档同步同轮完成。不提交、不推送。

## 目标与验收标准

将 Launcher 两页左上角 `Xiaomiao` 替换为单行完整北京时间 `YYYY-MM-DD HH:MM`，例如 `2026-10-01 16:35`。保留右上角 Wi-Fi 图标，不显示秒，不折行、截断、滚动或省略日期；无有效时间时留空。

- 每次开机必须收到合法联网校时结果才能显示；重启前时间不持久化、不作为有效来源。
- 后台使用内置 SNTP，每小时校时；首次校时不阻塞启动、按键和其他服务。
- 校时后短暂断网仍继续走时；距上次合法校时达到 24 小时即隐藏，重新成功后恢复。有效期用 64 位单调钟计算。
- 时间格式化、网络状态恢复及显示正常刷新完成后，首次有效时间和分钟变化最迟 1 秒更新。
- 时间只属于 Launcher，进入 App 不改变 App 标题，待机覆盖层遮住它；番茄、蜂鸣器和空闲待机计时不受墙上时间校正影响。
- 源码检查及相关宿主验证通过，并取得人工 ESP-IDF 6.1 构建、烧录及核心实机场景验收证据后，才能标记功能完成。

设计依据：`docs/launcher-network-clock-design.md`。用户在本轮接受完整设计，先前文档中“离线有效期尚未确认”的状态已被本轮确认取代。设计调研历史保留在 `goals/20261001-1634-launcher-network-clock-design.md`。

## 背景与文件入口

实施前阅读项目 `AGENTS.md`、`ROADMAP.md`、`README.md`、`docs/project-overview.md` 及上述设计。当前固件基于 ESP-IDF 6.1、LVGL 9.5，160 × 128 屏幕，内部 RAM 预算偏紧。

- `main/framework/xiaomiao_launcher.c`：标题 `(5,2)`，原框 `150 × 14`，卡片从 `y=19` 开始；创建／销毁入口和光带动画生命周期。
- `main/services/xiaomiao_wifi_service.{h,c}`：复制式网络快照，非 CONNECTED 状态清空旧 IPv4。
- `main/main.c`：普通固件 Wi-Fi 初始化、UI 主循环；自测分支与普通链隔离。
- `main/framework/xiaomiao_fonts.h`、`main/services/xiaomiao_font_service.c`：小字体及英文回退入口。
- `main/CMakeLists.txt`、`sdkconfig.defaults`、`sdkconfig.ci`：源码注册、SNTP 组件和可复现配置。

## 修改边界

允许新增 `main/services/xiaomiao_time_service.{h,c}`，修改 Launcher 标题及 timer、普通启动及 poll、CMake 源码清单、SNTP 配置。仅必要时修改字体配置以完整容纳日期字符串；优先已有字体，不新增第三方依赖或中文字库资源。

编码前先在 `AGENTS.md` 明确 Time Service 的 SNTP／系统时间所有权，随后修正 Wi-Fi Service 头部过宽的 esp_netif 所有权注释及设计口径。交付同步 `README.md`、`docs/project-overview.md`、`ROADMAP.md`、设计、本施工文档和 `goals/ROADMAP-history.md`；未实现内容不写入 README 当前功能。

禁止修改 GD32、硬件引脚、分区、NVS schema／凭据、PC Agent、无关 App、CI/CD、真实环境配置；不增任务、队列、全屏缓冲或运行时图片，不改全局 TZ。不提交、推送、部署、烧录或占用串口，不自行委派子 Agent。

宿主验证临时文件只放 `.tmp/launcher-network-clock/`，不得成为固件运行依赖，不清理其他任务文件。

## 确定的实现决策

### Service 与同步

Time Service 是 SNTP 唯一所有者，Wi-Fi Service 保留无线网络生命周期；UI 只读快照。公开接口：

```c
esp_err_t xiaomiao_time_service_init(void);
void xiaomiao_time_service_poll(void);
esp_err_t xiaomiao_time_get_snapshot(xiaomiao_time_snapshot_t *out);
```

快照为 `bool valid` 和 `char datetime[17]`，包含完整 16 个 ASCII 字符与 NUL。NULL 输出返回 `ESP_ERR_INVALID_ARG`；非 NULL 输出先清为无效与空字符串，未初始化返回 `ESP_ERR_INVALID_STATE`，初始化失败返回保存的初始化错误，调用方均按无效处理。

初始化幂等，不等待网络或时间；失败清理局部资源并记警告，设备继续进入 Launcher。校时采用 ESP-NETIF SNTP 封装、立即校正模式，默认候选服务器 `ntp.aliyun.com`、`pool.ntp.org`。两份可复现配置对齐 `CONFIG_LWIP_SNTP_MAX_SERVERS=2`、`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`；CP0 以实际 6.1 头文件／Kconfig 核对。

普通 UI 循环调用 poll，内部每 1 秒检查 Wi-Fi 快照；CONNECTED 且 IPv4 非零才启动。掉线停止／释放 SNTP，重连或 IP 变化重建；停止 SNTP 不清空仍在 24 小时有效期内的时间。初始化／启动失败每 30 秒重试，DNS／UDP 失败交给 lwIP 退避，不新增自定义网络 worker。网络恢复无需回到首页。

同步回调只校验并更新受保护的有效标志与单调时间戳，不调用 LVGL、格式化或网络 I/O。接受 UTC 秒范围 `[2026-01-01, 2100-01-01)`、微秒范围 `0..999999`；非法同步立即使显示失效，合法同步恢复。系统 UTC 读值也必须校验。

读取时保证同步元数据与时间采样一致，格式化在锁外；先核对实际 SNTP 设置系统时间与回调的先后关系，避免仅依靠服务锁误认为它能锁住 lwIP 的系统校时。转换失败、非法或过期均输出无效。UTC 加 8 小时后用 `gmtime_r()` 与固定格式生成北京时间，不修改全局 TZ；同步年龄只读 `esp_timer_get_time()`。

普通 SNTP 仅用于显示，不作为安全时间源。日志只记录同步、初始化失败与有效性变化，不每秒输出、不记录凭据。

### 布局与生命周期

标签在 Launcher 根下，位置 `(5,2)`，可用尺寸 `131 × 16`，与 Wi-Fi 图标起点 `x=140` 留出至少 4 px，底部不进入 `y=19` 卡片区域。复用原颜色与小字体；CP0 测量数字及分隔符宽度上界和行高，覆盖中文与英文回退。放不下时选已有紧凑数字字体；不偷偷缩短日期、不以 CLIP 掩盖超宽。

持久保存标签指针；创建时空文本隐藏，立即读一次快照，单个 1 秒 LVGL timer 更新。只有文本或有效性变化才更新对象；标签不接焦点、不增加光带、不加入 group、不挂 top layer。

分配失败仅放弃时钟，清理局部资源，保留卡片和导航；销毁先删 timer，再删根对象，最后清空指针和缓存。反复创建／销毁不累积 timer 或重复注册 Service。待机唤醒后最迟下一次刷新显示当前日期时间。

## 检查点

检查点按依赖顺序执行；代码实施完成与人工验收通过分别记录，不混为完成。

| 编号 | 目标及交付 | 依赖 | 验收方式与预期结果 | 当前状态 |
| --- | --- | --- | --- | --- |
| CP0 | 核对 6.1 API、同步回调顺序、重试行为、字体尺寸；先更新所有权规范 | 设计已确认 | 源码及配置证据，完整日期能在 131 × 16 内显示；必要差异先回写设计，不凭记忆实现 | 已完成（2026-10-01 17:49） |
| CP1 | Time Service、线程安全快照、校时与恢复状态机 | CP0 | 最小宿主桩验证真实业务逻辑，覆盖下列边界和失败路径；不等待网络、不泄漏资源 | 已完成（2026-10-01 17:49，宿主检查通过） |
| CP2 | Launcher 标签、单 timer 和资源释放 | CP0、CP1 | 静态核对显隐、字符串宽度上界、分配失败与销毁顺序；创建／销毁对象和 timer 无累积 | 已完成（2026-10-01 17:49，静态核对通过） |
| CP3 | 启动／poll 集成、CMake 和 SNTP 可复现配置 | CP1、CP2 | diff、引用及组件核对；自测分支隔离、单调倒计时不变、无直接 UI 网络 I/O | 已完成（2026-10-01 17:49，静态核对通过） |
| CP4 | 人工构建、烧录和核心实机回归 | CP3 | 以下清单通过，留存人工结果、日志／照片或明确确认；缺证保持进行中 | 已完成（2026-10-01 18:21，用户口头确认全部通过） |
| CP5 | 文档收口与交付 | CP4 | 实际改动、证据、未验证范围一致；ROADMAP 更新功能状态，施工原文保留 | 已完成（2026-10-01 18:21） |

### 软件边界与失败验证

- 未初始化、重复初始化、NULL 输出、初始化／启动失败及 30 秒重试；部分资源创建失败释放正确。
- 无网络、IPv4 为零、首次联网、掉线、重连、换 IP、配网后恢复；重复 poll 不重复启动。
- 未同步留空、合法／非法同步、微秒越界、UTC 上下界、转换失败；无效后合法同步恢复。
- 同步年龄在 24 小时前 1 微秒仍有效，到达及超过阈值失效；系统墙上时间前跳／回退不改变有效期；重启不沿用有效标志。
- UTC+8 跨日、跨月、闰日、跨年；00:00、09:05、23:59 等补零格式，字符串长度恰为 16 且正确终止。
- 首次同步与快照读取交叠，网络停止／重建时延迟回调；无对象跨线程更新或元数据撕裂。
- UI 标签／timer 分配失败与重复创建销毁，失效隐藏及同步恢复；无法执行的注入路径标明源码检查，不虚报测试通过。

## 人工验证命令与场景

由人工加载 ESP-IDF 6.1 环境，在仓库根目录运行以下任一构建入口；COM5 按实际端口替换：

```bash
idf.py build
idf.py -p COM5 flash monitor
```

仅编译用第一条；构建、烧录和日志观察可直接用第二条，无需重复预先 build。Agent 不执行上述命令、set-target、esptool 或访问设备串口。

1. 无凭据或无网开机：左上角留空，Launcher、卡片导航正常。
2. 取得 IPv4 但互联网／DNS／UDP 123 不可达：留空，设备可用；网络恢复后自动校时显示。
3. 联网成功：完整 `YYYY-MM-DD HH:MM` 与手机自动北京时间一致，分钟边界刷新延迟不超过 1 秒；两页位置一致，不与 Wi-Fi 图标、卡片重叠。
4. 中文正常字体与英文回退：日期完整清晰，无裁切、换行、省略和滚动；回退通过受控软件方式验证，不破坏用户资源分区。
5. 成功后关闭 Wi-Fi、断网或忘记网络：继续走时；恢复／换网／配网结束后重新同步。服务器失败切换行为提供实网证据；24 小时过期用可控单调钟软件检查。
6. 断电重启：未校时前留空，不闪现旧日期或 1970 年。
7. 五 App 往返至少 10 轮：无时钟浮层侵入；Launcher 焦点和光带正常，无 timer／对象累积。记录同条件内存样本，无持续下降趋势。
8. 待机期间后台同步继续；唤醒只恢复原页面，显示最新时间，不泄露时钟到 App。
9. 番茄运行、暂停／恢复、到点提醒及空闲待机回归；墙上时间校正不使剩余时长跳变、不误触发提醒。
10. 观察串口无新增崩溃、assert、反复初始化或高频日志；网络失败不阻塞启动与六键。

核心实机项为 1、2、3、5、6、7、8、9、10；字体回退和难注入故障应保留独立证据边界。无法完成核心项时保持进行中；若用户明确豁免某项，记录原话、影响和替代证据。

## 交付与证据记录

最终交付为 Service 源码、Launcher 与启动集成、可复现配置、必要软件验证及人工验收记录，连同规范、设计、README、概览和进度同步。交付报告只列关键改动、验证结果、未验证范围与风险，不宣称未测精度或性能。

- 2026-10-01 16:57：施工清单生成；产品规则按用户接受的设计收口，固件未修改，运行时 Goal 未启动。
- 2026-10-01 17:49：CP0～CP3 实施完成（`/goal` 授权）。
  - CP0：对照 ESP-IDF `release/v6.1` 核对 `esp_netif_sntp.h`（`esp_sntp_config_t` 字段、`start=false`／`wait_for_sync=false`、`sync_cb`）、`esp_netif_sntp.c`（init 失败内部自清理、deinit 幂等）、`components/lwip/apps/sntp/sntp.c`（IMMED 模式先 `settimeofday()` 后回调，回调直接收到同步 `tv`）、esp-lwip 核心 sntp.c（失败轮换服务器 + 指数退避，上限 10 倍）与 `components/lwip/Kconfig`（`LWIP_SNTP_MAX_SERVERS` 默认 1 范围 1..16；`LWIP_SNTP_UPDATE_DELAY` 默认 3600000、下限 15000）。字体核对：XMF1 字库只含 GB2312 双字节码点（`tools/font-pack/generate_font_pack.py` 的 `gb2312_codepoints()` 只枚举 0xA1A1..0xF7FE，03 区为全角形式 U+FF10 起），ASCII 数字走 `lv_font_montserrat_12` 回退；宽度上界 12 数字 × 8 px + `-`／`:`／空格 ≈ 111 px，行高 14 px，131 × 16 可容纳，无需 CLIP 或 DOTS。规范更新：`AGENTS.md` 增加 Time Service SNTP／系统时间唯一所有权；`xiaomiao_wifi_service.h` 头注释把 `esp_netif_*` 所有权收窄为「通用接口」，明确 `esp_netif_sntp_*` 专用接口例外。
  - CP1：新增 `main/services/xiaomiao_time_service.{h,c}`（快照契约、portMUX 临界区、1 Hz poll、30 秒失败重试门限、24 小时单调钟有效期、UTC 范围 `[2026-01-01, 2100-01-01)` 校验、微秒 0..999999 校验、UTC+8 `gmtime_r` 格式化、长度恰 16 校验）。宿主检查两场景（`main`／`bootfail`）共约 60 项断言全部 PASS：未初始化／NULL／重复初始化、无网络不启动、首次租约启动一次、重复 poll 不重启、合法／非法同步与恢复、上下界含 2100-01-01 拒绝、24 小时前 1 µs 有效／到达即失效、墙上时间前后跳不影响有效期、1970 墙钟拒绝、跨年／闰日／跨月／补零／午夜格式、断网与配网期间显示续走、同 IP 恢复一次重建、换 IP 恰一次重建、零租约视为离线、init 失败保存错误、30 秒重试门限前后行为、启动失败释放与重试。ASan／UBSan（`-fno-sanitize-recover=all`）下复跑 PASS。
  - 宿主检查发现并修复两个服务缺陷：(1) 网络恢复分支在实例被先前 teardown 释放后只调用 `esp_netif_sntp_start()` 而不重建实例；(2) 顶层重试块在离线 poll 每秒 create→destroy 抖动。修复后重建统一由在线分支负责并受 30 秒失败门限约束。
  - CP2：`xiaomiao_launcher.c` 以 131 × 16 时钟标签（`LAUNCHER_CLOCK_*`，起点 (5,2)）替换 `Xiaomiao` 品牌标签；创建时空文本隐藏并立即读一次快照，单个 1 s LVGL timer 仅在有效性或文本变化时更新；不进 group、不接焦点、不挂 top layer；timer 分配失败删除标签放弃时钟；销毁顺序 timer → band → 根对象 → 清指针缓存。Launcher 自测断言全部使用相对计数，不受影响（已核对）。
  - CP3：`main.c` 普通 branch 在 Agent Service 之后初始化 Time Service、`lvgl_task` 主循环接入 `xiaomiao_time_service_poll()`（循环上限 16 ms，内部 1 Hz 节流）；全部自测分支未触碰；`main/CMakeLists.txt` 注册新源文件（所有构建共用，自测链接完整）；`sdkconfig.defaults` 与 `sdkconfig.ci` 对齐 `CONFIG_LWIP_SNTP_MAX_SERVERS=2`、`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`。
  - 静态检查：`git diff --check` 干净；include 风格与现有跨层引用一致（`services/xiaomiao_time_service.h`）。
- 2026-10-01 18:21：CP4 人工验收通过——用户确认「都确认过了，没问题」（构建、烧录及核心实机场景全部通过；证据类型为口头确认，未附串口日志或照片；场景 4 英文回退、服务器故障切换实网证据与 24 小时过期实机观察未单独留证，分别以宿主检查、lwIP 内建退避与可控单调钟软件检查覆盖）。CP5 文档同步：`ROADMAP.md` 功能状态收口，`README.md` 与 `docs/project-overview.md` 写入已验收的真实功能，设计文档状态更新，`goals/ROADMAP-history.md` 补流水。
- 文档验证：交付前执行引用存在性和 `git diff --check` 检查；不需要固件构建。
- 未验证与证据边界（不阻塞收口）：实机验证为口头确认，无串口日志或照片；字体英文回退未做实机受控演示（宿主与静态核对覆盖）；SNTP 服务器故障切换的实际实网行为、24 小时过期的实机长时观察未单独留证（lwIP 内建退避与宿主可控时钟检查覆盖）；实机显示精度、刷新延迟量化与长期内存趋势无测量数据。

## 接手收尾复核

- 2026-10-01 19:49：核对实际 Time Service、Launcher 标签／timer 销毁、普通启动与 poll、自测分支、CMake、两份 SNTP 配置及规范／README／概览。保留施工记录中 18:21 的人工口头验收，不新增或编造实机证据；本轮未改固件代码。
- 2026-10-01 19:49：重新复制当前真实 `xiaomiao_time_service.c` 到本轮临时目录，配现有 Wi-Fi／SNTP／时钟桩，使用 clang 的 ASan／UBSan、`-fno-sanitize-recover=all` 重新编译；`TIME_SERVICE_HOST_CHECK main: PASS`、`TIME_SERVICE_HOST_CHECK bootfail: PASS`，无 sanitizer 报错。产物位于 `.tmp/launcher-network-clock-closeout/`，不复用旧二进制。
- 2026-10-01 19:49：修正设计文档中的实施前源码描述、快照读取实际锁边界、旧“未启动实施”结尾及过宽的验证收口表述；保留未单独取证的英文回退、服务器实网切换、长期内存与延迟量化。当前宿主桩不含真正并发，不能声称已完成同步回调交叠／延迟回调压力验证。
- 2026-10-01 19:49：源码引用、SNTP 唯一所有权、倒计时单调钟与 `git diff --check` 静态检查通过。未重复执行 ESP-IDF 构建、烧录、串口监视；不提交、不推送。
