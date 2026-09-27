# Goal：监控 App 增加智谱与 Codex 额度页

状态：已完成（2026-09-27）。PC Agent、固件 Agent Service、监控 App 四页与 i18n 均已实现，软件侧验证通过；实机验收经两轮反馈与三处排版修正后，由项目负责人复测确认通过。

创建时间：2026-09-27 18:55（北京时间）

## 目标

在现有「电脑监控」App 的 CPU／RAM、GPU／温度两页后，增加第 3 页智谱 Token 额度和第 4 页 Codex 额度。每页展示两个窗口的剩余百分比、进度条、本地重置时间及括号里的剩余时长。Launcher 中文入口改为「监控」，英文入口改为 `Monitor`。数据由电脑上的统一 Xiaomiao Agent 采集与缓存，设备经现有 Agent Service 获取。完成后四页可循环导航，PC 指标与两个服务商互不干扰。

本 Goal 的设计依据为 `docs/monitor-ai-quota-design.md`；此处保留施工必需的接口、决策和验收口径，当前状态以 `ROADMAP.md` 为准。立项时，`../ai-quota-monitor` 已实现 Codex 5H／7D 页面；智谱额度此前通过只读脚本成功查询，仓库内有可运行查询代码快照，但本项目的智谱 Python PC Agent 适配器、额度端点和设备页面尚未实现。

## 边界与依赖

- 改动范围：`pc-agent/` 的只读额度适配器、缓存、路由与对应软件验证；`main/services/xiaomiao_agent_service.{h,c}` 的额度快照与轮询；`main/apps/pc_monitor/` 的第 3／4 页；`main/framework/xiaomiao_i18n.{h,c}` 的相关文案；必要的 `main/CMakeLists.txt`、`README.md`、`docs/project-overview.md`、本 Goal、`goals/ROADMAP-history.md` 与 `ROADMAP.md`。
- 复用现有 Agent HTTP `8766`、UDP `8767` 发现、Wi-Fi Service 与 App Framework。不新建服务进程或端口，不让 App 直接访问 NVS、HTTP、JSON 或服务商接口。
- 保留 `pc_monitor` App ID、目录、图标、Launcher 注册位置和前两页外部行为；保留 `/api/v1/pc/metrics`、`/api/v1/health` 与发现协议。第三方凭据仅在 PC Agent 本机使用，不进入固件、HTTP 响应、串口日志或屏幕。
- 不做智谱 MCP 月度额度、模型／工具统计、历史曲线、告警、多账号、其他服务商、硬件引脚或分区调整。不得为此修改 `.env`、真实密钥、NVS、生产数据或系统配置。
- 当前动态发现补充 Goal `goals/20260927-1549-agent-service-discovery.md` 仍有换 Wi-Fi 实机复验待完成。本 Goal 复用其已实现协议，不以该复验作为额度页面开发的前置条件；若基础发现实际不可用，应先记录并定位，不能用固定地址绕过。

## 已确定的页面与交互

| 页码 | 内容 | 窗口 |
| --- | --- | --- |
| 1 | CPU／RAM | 原有 60 点图与数值 |
| 2 | GPU／温度 | 原有 60 点图与数值 |
| 3 | 智谱 | Token 5H、1W |
| 4 | Codex | 5H、7D |

- 右键按 1 → 2 → 3 → 4 → 1 循环；左键反向。因此第 1 页按左键到 Codex，第 4 页按左键到智谱；B 返回 Launcher。重新打开 App 从第 1 页开始。
- 第 3 页标题中文「智谱」，第 4 页为 `Codex`。每页两块窗口，各显示 `label`、`remaining_percent`、进度条、`MM-DD HH:MM` 和括号内剩余时长；页首显示该服务商状态，页脚显示页码与导航提示。中文字体不可用时遵循现有 i18n 英文回退，不出现半页乱码。
- 倒计时 `<1h` 用 `Nm`，`<1d` 用 `NhNm`，其余用 `NdNh`；剩余秒数向上取整到显示单位。重置已到、下一有效快照未更新时显示「待同步」，未知时间或倒计时显示 `--`。`0%` 是有效数据，不能变成 `--`。
- 状态映射：正常 `OK`，凭据缺失／未登录 `LOGIN`，上游不可达 `SRC`，响应无效 `BAD`，过期 `STALE`，设备到 Agent 的链路断开 `OFF`。有旧快照时可保留数值和时间，但须显示相应失效状态；无旧快照时显示 `--`。两家状态与快照不得串页。
- 在 160 × 128 上用实际字体和定位验证四项内容无截断；空间不足时先压缩状态／页脚，不删除窗口的重置时间或括号倒计时。进度条不代替数字或状态文字。

## 已确定的数据与接口

1. PC Agent 在既有 API v1 下新增只读 `GET /api/v1/quotas/zhipu` 与 `GET /api/v1/quotas/codex`。HTTP handler 只读取内存快照，不同步查询上游；健康端点能力列表仅列实际已注册路由。各服务商独立调度、缓存、锁与停止路径。
2. 响应沿用 `schema_version: 1` 外层，顶层 `status` 为 `ok`、`auth_required`、`unavailable`、`invalid_data` 或 `stale`，`error_code` 不含秘密或原始上游内容。`data` 仅含 `provider_id`、可空 `plan`、两个目标 `windows` 与 `updated_at_epoch`；每个窗口只含 `label`、可空 `remaining_percent`、可空 `resets_at`、可空 `reset_at_local` 和可空 `reset_in_sec`。窗口部分缺失时其余窗口仍有效，设备按服务商 ID 与 label 匹配，不按数组顺序猜测。若具体外层字段需适配本仓库既有 API v1 约定，须在软件检查点前冻结并同步本 Goal 示例。
3. PC Agent 按电脑本地时区生成 `reset_at_local`（`MM-DD HH:MM`），从同一采样时刻计算 `reset_in_sec = max(resets_at - now_epoch, 0)`。设备记录快照接收时的单调时钟，在显示时递减剩余秒数；无需设备墙上时钟或 SNTP。每次有效更新重新校准。无有效时间时两个时间字段均为空，UI 显示 `--`。
4. Codex 查询参考 `../ai-quota-monitor/pc-agent/codex_client.py` 与 `quotas.py`：通过本机 `codex app-server --listen stdio://` 建立只读 JSON-RPC；用 `account/read` 且显式传 `refreshToken: false` 确认 ChatGPT 登录，随后 `account/rateLimits/read`，仅取 `rateLimitsByLimitId.codex` 的 primary／secondary。`windowDurationMins=300` → 5H，`10080` → 7D；剩余比例为 `clamp(round(100 - usedPercent), 0, 100)`。通知唤醒加 60 秒兜底查询；子进程失败独立退避并恢复，不要求用户重新登录。
5. 智谱查询参考 `../ai-quota-monitor/goals/002-zhipu-quota.md` 与仓库内的可运行只读查询脚本快照 `../ai-quota-monitor/goals/references/glm-query-usage.mjs`；来源项目记录了 2026-09-16 的真实查询成功与字段口径。生产实现复用其已确认的配置发现、请求端点、认证头和 unit 映射，用项目原生 Python 标准库接入统一 PC Agent，不运行该脚本或 Node.js。按来源项目已确认顺序只读发现本机配置，目标域名限制为已确认的 `open.bigmodel.cn`／`dev.bigmodel.cn`；向 `/api/monitor/usage/quota/limit` 发有限超时 HTTPS GET，`Authorization` 直接放 Token，不加 `Bearer`。只取 `TOKENS_LIMIT` 的 `unit=3` → 5H、`unit=6` → 1W；`percentage` 是已用比例，`nextResetTime` 是毫秒 Unix 时间，先转秒。未知 type／unit、非有限数值、无效时间显式降级，不靠顺序推断。
6. 两家各自 60 秒轮询；180 秒没有成功更新标 `stale`，保留上次有效快照。智谱缺少配置或 401／403、Codex 非 ChatGPT 登录为 `auth_required`；网络／超时／429／5xx 为 `unavailable`，目标窗口全部无效为 `invalid_data`。一方失败不阻塞另一方和 PC 指标。服务端不记录 Token、账号 ID、邮箱、原始响应或完整鉴权头。
7. 固件 Agent Service 复用现有发现得到的 host／port，增加独立的额度低频请求与有界解析、快照接口。网络 I/O 和 JSON 解析在 Worker；App 只读锁保护的快照。额度 180 秒新鲜度与 PC 指标既有 3 秒失效分别处理；额度 HTTP 错误不得误清除 PC 指标，也不得把一个服务商状态写入另一个。HTTP 响应大小、字符串长度、百分比范围与窗口数量均须限制。

## 施工检查点与验证

### CP1：PC Agent 两个额度源与 HTTP 契约

完成 Codex／智谱适配器、独立缓存、状态处理及路由。以构造响应验证 5H／7D、5H／1W 标签，百分比与毫秒转秒，0%、缺失窗口、未知 unit、异常数值、凭据缺失、401／403、429／5xx、子进程退出和一方失败时另一方仍可用。检查路由只读缓存、健康能力列表正确、无秘密出现在日志和响应。人工真实只读查询仅在具备本机授权登录／配置的环境执行，结果只记录字段口径，不保存动态额度或原始响应。

### CP2：设备额度快照与链路隔离

扩展 Agent Service，用现有发现地址请求两个端点；固定服务商 ID、两个窗口 label 和有界字段，保留各自 `reset_in_sec` 接收时刻。通过源码与可用的软件侧用例检查无效 JSON、超限包、重复／缺失窗口、离线、过期、恢复和独立性；PC 指标每秒轮询与 3 秒失效不回归。不得从 App 层发起 HTTP。

### CP3：四页 UI、文案与倒计时

把入口改为「监控」／`Monitor`，增加第 3 页智谱、第 4 页 Codex；页面复用现有 App 的 group、timer 与 close 生命周期，尽量不扩大常驻内存。静态检查页序和取模逻辑，核对 `reset_in_sec` 单调递减与格式边界：1 秒、59／60 秒、59／60 分钟、23／24 小时、0 秒、未知值。检查状态文字、0% 与 `--`、旧快照、中文／英文回退以及重复进入关闭后的资源清理。

### CP4：人工构建、烧录与目标板验收

由项目负责人执行 ESP-IDF 6.1 构建、烧录、串口监视及实机按键／目视验证；Agent 不执行 `idf.py build`、`set-target`、`flash`、`monitor`、`esptool`，也不占设备串口。建议从项目根目录、已加载 IDF 环境执行：

```text
idf.py --version
idf.py -p COM5 flash monitor
```

预期 `ESP-IDF v6.1`；Launcher 显示「监控」；四页按键顺序正确，尤其第 1 页按左键先到 Codex、再到智谱；两家各有两窗口的百分比、重置时间和括号倒计时；倒计时随时间推进；无缺字、截断或遮挡；PC 指标和其他 App 正常。分别观察 Codex 未登录、智谱配置缺失、Agent 停止与恢复、只一个服务商失败的降级行为。无法实际构造的上游故障以软件测试覆盖，不能写成实机通过。人工回传构建输出、关键日志或屏幕结果后再判定固件验收。

逐页预期（先启动 PC Agent，设备与电脑处于同一可互通局域网）：

| 位置 | 预期 |
| --- | --- |
| Launcher 第 1 页第 2 格 | 名称「监控」（英文回退时为 `Monitor`），其余入口与顺序不变 |
| 第 1 页 | `CPU / RAM` 标题 + 两项数值与 60 点图，页脚 `B 返回   < > 换页` |
| 第 2 页 | `GPU / 温度`，温度行按 GPU → CPU → `--` 降级 |
| 第 3 页 | 标题「智谱」+ 右上状态；两行 `5H`／`1W`，各有百分比、进度条、`R MM-DD HH:MM` 与括号倒计时；页脚右侧 `3/4` |
| 第 4 页 | 标题 `Codex` + 状态；窗口为 `5H`／`7D`；页脚右侧 `4/4` |
| 第 1 页按左键 | 直接到第 4 页 Codex（不是智谱）；再按左键到智谱，再按到第 2 页 |

降级预期（时间口径为源码约定值）：

| 场景 | 预期 |
| --- | --- |
| Codex CLI 缺失或非 ChatGPT 登录 | Codex 页状态 `LOGIN`／`SRC`、数值 `--`；智谱页与 PC 指标照常更新 |
| 智谱配置缺失或 Token 无效 | 智谱页状态 `LOGIN`、数值 `--`；Codex 页照常更新 |
| 停止 PC Agent | 额度页最长约 10 秒内转为 `OFF`（旧数值可保留但必须带 `OFF`），PC 指标 3 秒内回到 `--` |
| 重启 PC Agent | 两家额度最长约 30 秒内恢复 `OK` 与新数值，PC 指标立即恢复 |
| 上游窗口已重置但快照未更新 | 该行显示「待同步」，不出现 `0m`，也不自动跳到 100% |
| 重置时间字段缺失 | 该行重置时间显示 `--`，百分比照常显示（真实 `0%` 保持 `0%`） |

## 关键失败路径

- Codex CLI 不存在或非 ChatGPT 登录：Codex 页显示 `SRC`／`LOGIN` 与 `--`；智谱和 PC 指标仍更新。不得读取 Codex 凭据文件来绕过 App Server。
- 智谱配置缺失、Token 无效或接口字段变更：智谱页按 `LOGIN`／`BAD` 降级，未知窗口不冒充 5H／1W；Codex 与 PC 指标仍更新。实施前先核对当时的官方资料或真实只读响应，不把来源项目的未实施设计当作线上事实。
- Agent 断开、Wi-Fi 离线或 JSON 无效：旧额度可保留但必须标明 `OFF`／`STALE`；恢复后两页分别刷新，不把旧值当新值。PC 指标仍遵守原 3 秒失效。
- 重置时间已经过去但未获新快照：显示「待同步」，不无限显示 `0m` 或自动把额度重置为 100%。
- 页面重复打开关闭、对象部分创建失败：timer、焦点和对象引用按现有生命周期释放；Launcher 可恢复，不因额度页造成内存持续下降。

## 验收标准

- [x] PC Agent 同进程提供两个只读额度端点，字段、状态、时间口径符合本 Goal；现有 PC 指标、健康端点与动态发现行为不回归。（102 项 Python 用例含既有 PC 指标用例无回归、本机 HTTP 冒烟、两条口径用例）
- [x] 两服务商采集、缓存、过期与故障互相隔离；秘密只在 PC 本机使用，不出现在设备或日志。（隔离用例 + 响应无凭据用例 + 固件不接触凭据的源码核查）
- [x] 设备四页顺序、左／右循环及 B 返回正确；第 3 页智谱、第 4 页 Codex 各展示两个窗口的百分比、重置时间和括号剩余时长。（项目负责人实机确认；第 4 页另有照片佐证）
- [x] 0%、缺失字段、过期、离线、重置到点和恢复均按约定显示；中文与英文回退无截断或乱码。（显示逻辑与倒计时边界经宿主编译与 i18n/字库校验；整体由负责人确认，逐项降级场景未单独取照）
- [x] 软件侧相关用例与静态检查通过；人工 ESP-IDF 6.1 构建、烧录和目标板目视／按键验证已由负责人确认。（第一轮附实机照片；最终确认未附构建输出或串口日志）
- [x] 完成后更新 `README.md`、`docs/project-overview.md`、`ROADMAP.md` 和 `goals/ROADMAP-history.md`；在本 Goal 填写实际交付、验证时间、结果与未验证范围。

## 交付结果与未验证范围

状态：已完成（2026-09-27）。软件侧验证通过；项目负责人确认已构建、烧录，并在修正排版后完成目标板目视／按键复测。最终确认属于人工口述证据，未回传构建输出、串口日志或逐项截图；逐项故障注入的实机表现仍未单独验证。

- 2026-09-27 19:01：在可联网执行环境运行本机 `glm-stats` 只读查询，智谱实时返回 3 条额度记录，其中 `TOKENS_LIMIT unit=3` 与 `unit=6` 均存在，`percentage` 为有限数值，`nextResetTime` 为有效数值时间戳。受限环境首次尝试因 DNS 不通失败；联网重试成功。未记录 Token、原始响应或动态用量数值。此证据仅证明当前上游查询及目标字段可用。

### 实际交付（2026-09-27，Agent 侧）

- `pc-agent/quota_cache.py`（新）：冻结的 API v1 外层契约（`schema_version`／`status`／`data`／`error_code`）、`remaining_percent` 与本地时间换算、180 秒过期判定、失败保留上次有效值，以及简单低频轮询 Worker。
- `pc-agent/codex_quota.py`（新）：`codex app-server --listen stdio://` 只读 JSON-RPC 客户端（有限退避重建、stderr 排空、子进程退出唤醒等待者），`rateLimitsByLimitId.codex` 归一化（300 → `5H`、10080 → `7D`），`account/read` 且显式 `refreshToken:false` 的登录校验，以及 Codex Worker。
- `pc-agent/zhipu_quota.py`（新）：按冻结顺序只读发现本机 Token（`~/.qclaw/.../models.json` → `~/.claude/settings.json` → 环境变量），只接受 `open.bigmodel.cn`／`dev.bigmodel.cn`，向 `/api/monitor/usage/quota/limit` 发有限超时 HTTPS GET，`Authorization` 原样放 Token，`TOKENS_LIMIT` 的 `unit=3` → `5H`／`unit=6` → `1W`，毫秒转秒与状态映射。
- `pc-agent/monitor.py`：新增只读 `GET /api/v1/quotas/codex` 与 `/api/v1/quotas/zhipu`（只读内存快照，不在请求路径查询上游）、健康端点能力列表更新为 `pc.metrics`／`quotas.codex`／`quotas.zhipu`、两个服务商各自独立的缓存、调度线程与停止路径。
- `pc-agent/tests/`：新增 `test_quota_cache.py`、`test_codex_quota.py`、`test_zhipu_quota.py`，并扩展 `test_http_api.py` 覆盖两个额度端点、未知 provider 404、单侧失败隔离与既有 PC 指标不回归。
- `main/services/xiaomiao_agent_service.{h,c}`：共享「已发现 Agent host／port」链路（PC 指标 Worker 发布，额度 Worker 读取）、`xiaomiao_quota_provider_t`／`xiaomiao_quota_state_t`／`xiaomiao_quota_snapshot_t` 与 `xiaomiao_agent_get_quota_snapshot()`、额度 JSON 有界校验（1 KB 上限、provider ID 与固定 label 匹配、百分比 `0..100`、倒计时 `0..400` 天、`reset_at_local` 形状校验）、独立额度 Worker（健康 30 秒／退化 10 秒）、180 秒过期与链路丢失时两家读 `OFF`。
- `main/apps/pc_monitor/xiaomiao_pc_monitor.c`：第 3 页智谱（`5H`／`1W`）、第 4 页 Codex（`5H`／`7D`），每窗口一行显示 label／剩余百分比／进度条，下一行显示重置时间与括号倒计时；倒计时用设备单调时钟从 Service 记录的接收时刻递减，到点显示「待同步」，`0%` 保持数字，缺失显示 `--`；页首状态 `OK`／`LOGIN`／`SRC`／`BAD`／`STALE`／`OFF`，额度页页脚显示页码与导航提示，四页共用现有 timer／group／close 生命周期。**顺带调整**：标题、状态、页脚与页码改为在四个页面对象之后创建，因为页面是不透明全屏对象、按 LVGL 顺序会盖住早建的标签；此前两页图表页的标题行很可能被页面遮住，改后标题会正常显示，实机验收时请把「第 1／2 页标题行出现」当作预期行为而非回归。
- `main/framework/xiaomiao_i18n.{h,c}`：新增 10 个文案 ID（共 213 个：两个服务商标题、额度页页脚、待同步标记与六个状态词），入口显示名改为 `Monitor`／「监控」。
- 文档：`README.md`、`docs/project-overview.md`、`ROADMAP.md`、`docs/monitor-ai-quota-design.md`、本 Goal 与 `goals/ROADMAP-history.md`。

### 软件侧验证证据（2026-09-27）

- 语法与单测：`pc-agent/.venv/macos/bin/python -m py_compile monitor.py pc_metrics.py quota_cache.py codex_quota.py zhipu_quota.py` 通过；`... -m unittest discover -s tests -q` 共 102 项通过、1 项因本机无 `nvidia-smi` 跳过，既有 PC 指标用例无回归。额度新增用例除契约与降级路径外，另含四条口径抽查：额度路由只读缓存（连续 5 次 HTTP 请求不触发任何上游采集，只有显式 `run_once()` 才采集）、响应不含凭据（走真实归一化路径后响应正文既不含 Token 也不含 `Authorization`／`apiKey`／`baseUrl` 等字段名）、失败后恢复（失败保留旧值且下一次成功清掉错误状态并刷新数值）、同一时间采样（一次 `snapshot()` 内所有窗口的重置时间与倒计时共用同一个本机时间样本）。
- 本机 HTTP 冒烟（`monitor.py --host 127.0.0.1 --port 8799`）：`/api/v1/health` 能力列表正确；`/api/v1/quotas/codex` 与 `/api/v1/quotas/zhipu` 均 `status=ok`、`window_count=2`，Codex 为 `5H`／`7D`、智谱为 `5H`／`1W`，两窗口的 `remaining_percent`、`resets_at`、`reset_in_sec` 均为整数，`reset_at_local` 匹配 `MM-DD HH:MM`。未记录额度数值。
- 倒计时边界：把 App 的取整逻辑独立用宿主编译器编译执行，核对 0／1／59／60／61／3599／3600／86399／86400 秒与 400 天上限——0 秒显示「待同步」，1～59 秒显示 `(1m)`（不出现 `0m`），3599～3600 秒显示 `(1h0m)`，86399～86400 秒显示 `(1d0h)`，未知显示 `--`。
- 静态检查：修改过的 App／Service／i18n 文件花括号与圆括号平衡，App 与 Service 源文件无非 ASCII 行（中文仅存在于 i18n 表）。
- 接口一致性：脚本比对 App 与 Service 用到的 14 个额度符号全部在 `xiaomiao_agent_service.h` 中定义，`xiaomiao_agent_get_quota_snapshot()` 的声明与实现签名逐字一致；`XIAOMIAO_QUOTA_PROVIDER_*` 的枚举顺序与 Python 侧 provider 对应关系（智谱页→zhipu、Codex 页→codex）一致。
- 字体覆盖：`tools/font-pack/verify_font_pack.py assets/fonts/xiaomiao-zh-cn.xmf --expect-chars main/framework/xiaomiao_i18n.c` 输出 `VERIFY OK`（7,445 字形、CRC 通过）；新增汉字 智／谱／待／同／步／监／控 均在 XMF1 码点表内。

### 实机证据与随后的排版修正（2026-09-27 20:05）

- 项目负责人烧录后回传两张实机照片（第 1 页 CPU/RAM、第 4 页 Codex）。**第 4 页额度显示正确**：标题 `Codex` + 右上 `OK`、`5H 75%` 与进度条、`R 09-27 22:55` 与 `(2h54m)`、`7D 93%` 与进度条、`R 10-04 12:54` 与 `(6d17h)`、页脚 `B 返回  < >` 与 `4/4`，均无截断、无重叠。这是额度侧首次取得的实机显示证据；第 2、3 页、按键顺序与降级场景仍待确认。
- **第 1 页暴露排版缺陷**：`RAM` 被截成 `RA`，`CPU` 与数值粘连为 `CPU86.9 %`。根因是节点 6 遗留的 28 px 名称框：中文环境下 ASCII 由 XMF1 12 px 字体的 Montserrat 回退渲染，`RAM` 实测 ≈27.8 px 顶到 28 px 边界后按字符折行，第二行被 16 px 行高裁掉；`TEMP`（≈34 px）与 `GPU T`／`CPU T`（≈37 px）同理，温度页同样受影响。
- **修正**（`main/apps/pc_monitor/xiaomiao_pc_monitor.c`、`main/framework/xiaomiao_i18n.c`）：每项指标合并为一个 78 px 宽、12 px 字号的标签承载「名称 + 数值 + 单位」（`CPU 86.9%`、`GPU 55.0°C`），单位不再前置空格，全部设 `LV_LABEL_LONG_MODE_CLIP`；温度来源名英文改为 `GPU`／`CPU`（中文仍为「温度」，单位承担含义）；额度页状态标签右边界收到 x=136，避开 top layer 的全局 Wi-Fi 图标（140..158），并把两窗口行距略作分散。新宽度按 Montserrat 12 与 XMF1 12 px 的真实字形步进逐串核算（最长 `RAM 100.0%` ≈75.7 px < 78 px，`温度 55.0°C` ≈65 px）。
- **标题被裁的第二处根因（负责人 2026-09-27 第二轮反馈）**：标题框只有 14 px 高，而标题字体（16 px 档）行高为 19 px，于是标题折行且首行底部被裁、末行越界不可见，四页标题都受影响。修正：标题框改为 y=1、h=20 并设 `CLIP`；同样把 y 轴 `100`／`0` 与额度页重置行的高度从 12／13 px 补到 14 px（12 px 档行高），避免同类亚像素裁切。
- 截至这轮修正记录时**尚未再次烧录**；随后项目负责人完成复测并确认通过，证据范围见下节。

### 人工验收确认（2026-09-27）

- 第二轮反馈（额度页状态文字与右上 Wi-Fi 图标重合、四页标题底部被裁）经上述修正后由项目负责人复测，确认「这个功能没问题了，都验证过了」，本 Goal 据此收口。
- 2026-09-27 20:30：本次收尾在 `pc-agent/` 目录、允许本机回环端口绑定的环境复跑 `.venv/macos/bin/python -m unittest discover -s tests -q`，102 项通过，1 项因本机无 `nvidia-smi` 跳过；`git diff --check` 通过。未由 Agent 执行 ESP-IDF 构建、烧录或串口监视。
- 覆盖范围：Launcher 显示名、四页顺序与左右循环、B 返回；第 3／4 页两窗口的百分比、进度条、`R MM-DD HH:MM` 与括号倒计时；四页标题完整性与右上状态显示；排版修正后的第 1／2 页指标行。
- 证据类型：第一轮反馈附实机照片（第 1、4 页），第二轮反馈与最终确认为人工口述确认，未附串口日志或逐项截图；按项目惯例在本 Goal 与 `ROADMAP.md` 中标注证据类型。

### 残余说明（不影响收口）

- 逐项上游故障形态（Codex 未登录／CLI 缺失、智谱配置缺失或 401／403、429／5xx、Agent 停止与恢复、重置到点后的「待同步」）未单独做实机观察取照，仅由 Python 用例覆盖并由负责人整体确认；如需逐项证据可后续补录。
- 固件构建与运行日志未回传：编译、烧录由负责人执行，Agent 未运行 `idf.py build`／`set-target`／`flash`／`monitor`，也未占用串口。
- 单显示 DMA 缓冲下的刷新表现与既有四个 App 的回归由负责人整体确认，未附独立日志。
