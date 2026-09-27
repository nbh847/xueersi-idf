# 监控 App 第三、四页：智谱与 Codex 额度设计草案

状态：设计已实施，软件侧验证通过；项目负责人已确认 ESP-IDF 构建、烧录与实机目视验收通过。证据范围与未逐项实机验证的故障场景见 `goals/20260927-1855-monitor-ai-quotas.md`。

## 结论与依据

- 立项时小喵「电脑监控」App 有两页：第 1 页 CPU／RAM，第 2 页 GPU／温度，左右键循环切页，B 返回 Launcher。名称由 `XM_TEXT_APP_PC_MONITOR` 的中英双表提供，内部 App ID 为 `pc_monitor`。
- 立项时 `../ai-quota-monitor` **只实现 Codex 页面**，同页显示 5H 与 7D。智谱额度曾由只读查询脚本成功取到；仓库保存了该脚本的代码快照 `goals/references/glm-query-usage.mjs`，可参考其中的配置发现、请求与字段映射。其 `goals/002-zhipu-quota.md` 计划把智谱 5H／1W 做成**另一页**，通过 BOOT 切换；本项目 Python PC Agent 的智谱适配器、额度路由和设备页面当时尚未实现。
- 本项目在现有监控 App 后新增两个服务商页面：**第 3 页智谱，第 4 页 Codex**。每页显示该服务商的两个额度窗口，给重置时间和括号里的剩余时长留出空间。左／右键四页循环；在第 1 页按左键先到第 4 页 Codex，再按左键到第 3 页智谱。

## 目标与边界

1. Launcher 中文入口从「电脑监控」改为「监控」，英文入口从 `PC Monitor` 改为 `Monitor`。App ID、图标、注册位置和前两页指标行为保留，以免影响已有入口和数据链路。内部目录与 C 符号暂沿用 `pc_monitor`，后续业务扩展不依赖显示名称。
2. 第 3 页标题为「智谱」，第 4 页标题为 `Codex`。右键按 CPU／RAM → GPU／温度 → 智谱 → Codex → CPU／RAM 循环，左键反向循环；B 返回 Launcher。首次打开仍从 CPU／RAM 开始。
3. 智谱显示 Token 5H、1W；Codex 显示 5H、7D。每个窗口均显示**剩余百分比、重置日期时间，以及括号内距离重置的剩余时长**。百分比为 0 时须显示 `0%`，缺失时显示 `--`。两家各自显示状态，不共享错误或缓存。
4. 仅复刻本机 PC Agent 的额度查询、归一化与状态逻辑。设备不保存、读取或传输 Codex／智谱凭据，也不直连服务商 API。智谱 MCP 月度额度、模型／工具统计、历史曲线、告警、多账号和新服务商不在本次范围。

## 页面草图

```text
第 3 页：智谱                 第 4 页：Codex
┌────────────────────┐       ┌────────────────────┐
│ 智谱           OK   │       │ Codex          OK   │
│ 5H  64%  [████░░]   │       │ 5H  80%  [█████░]  │
│ R 09-27 21:00 (2h13m)│      │ R 09-27 19:30 (43m)│
│ 1W  28%  [██░░░░]   │       │ 7D  12%  [█░░░░░]  │
│ R 10-01 08:00 (3d13h)│      │ R 09-30 10:00 (2d5h)│
│      ←  3/4  →  B   │       │      ←  4/4  →  B  │
└────────────────────┘       └────────────────────┘
```

草图数值与时间是构造示例，框线并非像素级尺寸。每页两块窗口，各有百分比、进度条和 `MM-DD HH:MM` 重置时间；括号内用紧凑格式显示剩余时长：小于 1 小时用 `Nm`，小于 1 天用 `NhNm`，其余用 `NdNh`。按剩余秒数向上取整到显示单位，避免尚有几十秒时提前显示 `0m`；到点后、下一快照尚未更新时显示「待同步」，不再显示过期窗口的倒计时。时间或倒计时字段无效时显示 `--`。页面用固定列宽或定位，不靠空格对齐；160 × 128 实机需确认 12 px 字体下无截断，若空间不足优先缩短页脚和状态文案，不删重置时间或倒计时。

每页状态独立显示 `OK`、`LOGIN`、`SRC`、`BAD`、`STALE`、`OFF`；离线时可保留最近有效数字和时间，但必须标为 `STALE`／`OFF`，不能冒充实时值。无历史快照时显示 `--`。时间由 PC Agent 按电脑本地时区生成，倒计时按同一时间点计算；设备端用单调时钟推进已收到的剩余秒数，不依赖设备墙上时钟。

## 数据链路与接口

```text
Codex App Server ─┐
                  ├─ Xiaomiao Agent 独立采集与缓存 ─ HTTP API v1
智谱额度 API ───────┘                               │
                                                   ▼
                                      Agent Service 独立额度快照
                                                   │
                                                   ▼
                                      监控 App 第 3、4 页
```

- 复用本仓库 `pc-agent/` 的统一进程、HTTP `8766` 和 UDP `8767` 发现；增加 `GET /api/v1/quotas/codex` 与 `GET /api/v1/quotas/zhipu`，不另开服务进程／端口。两端点只返回内存中的白名单快照，HTTP 线程不执行上游查询。`/api/v1/health` 只列出实际已提供的能力。
- 两端点沿用本仓库 API v1 的 `schema_version` 外层约定；额度 `data` 含 `provider_id`、`plan`（可空）、`windows`、`updated_at_epoch`。每个窗口仅含 `label`、`remaining_percent`（可空）、`resets_at`（可空）、`reset_at_local`（可空，格式 `MM-DD HH:MM`）与 `reset_in_sec`（可空）。`status` 区分 `ok`、`auth_required`、`unavailable`、`invalid_data`、`stale`，错误码不包含上游响应。设备按固定 provider ID 与窗口 label 匹配，不依赖数组顺序。PC Agent 从同一次本机时间采样计算本地重置文本和 `max(resets_at - now_epoch, 0)`；设备收到快照后用单调时钟递减，不要求设备新增时钟同步。若 `reset_in_sec=0` 且上游窗口未更新，显示「待同步」。
- Codex 采集参考来源项目 `pc-agent/codex_client.py` 与 `quotas.py`：启动本机 `codex app-server --listen stdio://`，先用 `account/read` 且显式传 `refreshToken: false` 确认 ChatGPT 登录，再读 `account/rateLimits/read` 中 `rateLimitsByLimitId.codex` 的 primary／secondary。按时长 300／10080 分钟映射 5H／7D；`remaining_percent = clamp(round(100 - usedPercent), 0, 100)`。仅查询，不触发登录或刷新凭据。Codex CLI 不存在、未登录、返回不完整或子进程退出时只影响 Codex 快照。
- 智谱**立项时已有可运行的只读查询参考代码，需接入本项目的 Python PC Agent**：参考来源项目 `goals/002-zhipu-quota.md` 与 `goals/references/glm-query-usage.mjs` 的配置发现、HTTP 请求和字段映射；生产运行不依赖该脚本或 Node.js。按已确认配置来源顺序只读定位 Token，向对应智谱域名的 `/api/monitor/usage/quota/limit` 发有限超时 HTTPS GET，`Authorization` 直接放 Token。仅取 `TOKENS_LIMIT` 的 `unit=3`（5H）和 `unit=6`（1W），`percentage` 是已使用比例；`nextResetTime` 为毫秒 Unix 时间，先转秒。未知类型／unit 不猜测；缺少配置或 401／403 为 `auth_required`，网络／429／5xx 为 `unavailable`，两个目标窗口均无效为 `invalid_data`。
- 两家各有采集线程／调度、状态和锁保护缓存。Codex 保留通知唤醒与 60 秒兜底查询；智谱按 60 秒轮询；各自 180 秒无成功更新标 `stale`，保留最后有效值。PC 指标继续每秒采集与现有 3 秒失效；额度的 180 秒新鲜度不可套用 PC 指标的 3 秒规则。
- 固件 `Agent Service` 复用已发现的主机与端口，分别请求额度端点并限制响应大小、字段长度、窗口数量及数值范围。网络请求与 JSON 解析留在 Worker，App 只读独立快照；任一额度请求失败不得重置 PC 指标连接或另一家额度。进入第 3／4 页时显示对应服务商缓存，随后尽快更新；关闭 App 不停止服务端缓存。避免把三条 HTTP 请求永久叠在现有每秒 PC 指标轮询上，可为两家额度设置独立低频计划。

## 实施范围与检查点

1. **PC Agent**：在 `pc-agent/` 增加 Codex／智谱采集适配器、独立缓存和两个只读路由；保留 `/api/v1/pc/metrics`、健康端点及发现协议。静态与 Python 合约检查覆盖两个服务商的正常、部分窗口、0%、无效值、凭据缺失、失败后恢复、重置倒计时和互不干扰。
2. **固件 Service**：扩展 `main/services/xiaomiao_agent_service.{h,c}` 的额度快照和低频请求，不让 `main/apps/` 直接访问 HTTP／JSON。检查旧 PC 指标的 1 秒刷新与 3 秒失效，额度的 180 秒过期及两个服务商独立性。
3. **App 与文案**：扩展 `main/apps/pc_monitor/` 第 3 页智谱、第 4 页 Codex；仅修改对应 i18n 项与新增额度文案，中文表写「监控」，英文表写 `Monitor`。检查第 1 页左键到 Codex、第 4 页左键到智谱、右键正向循环、B 返回、每个窗口数值／重置时间／倒计时、无数据／过期状态、重复进入关闭和英文回退。
4. **文档与人工验收**：实现时更新 `README.md`、`docs/project-overview.md`、`ROADMAP.md` 和施工 Goal。Agent 只做静态检查及 PC Agent 软件验证；ESP-IDF 构建、烧录、串口与实机目视由人工执行。无人工证据时固件及页面保持「未验证」，不得写为完成。

## 实施前需复核的外部事实

- 智谱上游接口目前以来源项目的本机查询参考和设计为依据；本项目实施时仍需按当时可用的官方资料或真实只读响应逐字段复核，尤其是域名、鉴权、`unit` 和时间戳。
