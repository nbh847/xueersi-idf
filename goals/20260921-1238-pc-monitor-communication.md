# 节点 11：统一 Agent 基线与 PC Monitor 通信

## 元信息

- 对应节点：节点 11“PC Monitor 通信”
- 状态：已完成（2026-09-21；CP1～CP4 软件与静态验证通过，CP5 由项目负责人确认人工构建、烧录和手动测试通过）
- 创建时间：2026-09-21 12:38（北京时间）
- 前置条件：节点 0～10 均已完成；节点 10 已提供异步 Wi-Fi Service、连接状态快照与断线重连
- 前置施工文档：`goals/20260920-2210-wifi-service.md`
- 参考实现：`D:/workSpace/hardware/esp32s3-lab/pc-monitor/` 与 `D:/workSpace/hardware/esp32s3-lab/ai-quota-monitor/`
- 后续目标：AI 用量监控复用本节点建立的统一 Agent 地址、HTTP 基础能力和领域路由，不另起后端或端口

## 目标与预期行为

建立一个运行在电脑上的统一 `Xiaomiao Agent` HTTP 后端，并在固件中建立通用 `Agent Service`。节点 11 首版只接入 PC Monitor：电脑端后台采集 CPU、内存、GPU 与温度，ESP32 通过节点 10 的 Wi-Fi Service 每秒拉取一次缓存快照，PC Monitor App 在 LVGL 线程读取 Service 快照并刷新现有四行指标。

设备未配置 Agent 地址、Wi-Fi 未连接、电脑未启动 Agent、HTTP 超时、响应损坏或可选指标不可用时，固件仍须正常进入 Launcher，其他 App 不受影响。连续 3 秒没有取得有效 PC 指标后，页面不再把旧值伪装成实时值，对应指标恢复为 `--`；链路恢复后自动继续刷新，无需重启设备。

本节点同时冻结统一 Agent 的首版 API 命名与公共响应语义，为后续 PC 系统信息、进程、磁盘和 AI 额度功能保留兼容扩展入口，但不得提前实现这些后续业务。

## 已确认决策

1. **一个后端、一个地址、一个端口。** 电脑只运行一个统一 `Xiaomiao Agent`，默认监听 `0.0.0.0:8766`。PC 指标、后续 AI 额度及其他功能通过不同的 `/api/v1/...` 路由区分，不为每个 App 启动独立后端。
2. **HTTP 负责正式业务数据。** 节点 11 不使用 UDP、WebSocket、MQTT 或串口传输 PC 指标。
3. **首版使用固定 IPv4。** 固件从本地构建配置读取 Agent IPv4 与端口；不实现 mDNS、UDP 发现、多电脑选择、自动 IP 迁移或设备配对。建议人工在路由器中为电脑设置 DHCP 地址保留。
4. **API 按业务领域拆分。** 本节点实现 `/api/v1/health` 与 `/api/v1/pc/metrics`；不实现面向某个屏幕的 `/dashboard` 巨型接口。
5. **请求只读取缓存。** PC Agent 在后台采集并保存 PC 指标；HTTP 请求线程只序列化缓存快照，不在请求路径中阻塞调用 `nvidia-smi`、硬件监控程序或未来的云服务商接口。
6. **固件只消费归一化字段。** ESP32 不感知 NVIDIA、AMD、Intel、`psutil`、`nvidia-smi` 或其他采集来源，不解析各厂商原始输出。
7. **字段缺失不伪造为零。** 不可获取的 GPU 或温度以 JSON `null` 表达，设备显示 `--`。真实的 `0%` 与 `0 °C` 必须和“不可用”严格区分。
8. **温度字段不合并。** API 分别提供 CPU 与 GPU 温度。现有 `TEMP` 行优先显示 GPU 温度；GPU 温度不可用时显示 CPU 温度；两者均不可用时显示 `--`。标签同步显示 `GPU T`、`CPU T` 或 `TEMP`，不得让用户误判温度来源。
9. **Service 拥有后台任务。** App 不直接发 HTTP、不直接读取 Wi-Fi 驱动，也不在网络任务中操作 LVGL。`Agent Service` 独立持有 Worker、HTTP 生命周期和线程安全快照。
10. **后续 AI 额度复用基础设施。** 后续路由使用 `/api/v1/quotas` 与 `/api/v1/quotas/{provider_id}`；本节点只冻结命名，不移植 Codex、智谱或其他服务商逻辑。
11. **可信局域网边界。** 首版 HTTP 不提供 TLS、鉴权或设备配对，不得暴露到公网或不受信任网络；响应只包含显示所需的归一化系统指标，不包含 Token、Cookie、密码或原始命令输出。

## 背景与文件入口

- `main/apps/pc_monitor/xiaomiao_pc_monitor.c` 当前只创建 CPU／RAM／GPU／TEMP 四项编译期占位标签，没有标签引用、timer、数据源或后台任务。
- `main/services/xiaomiao_wifi_service.{h,c}` 已拥有 Wi-Fi 生命周期，并通过 `xiaomiao_wifi_get_snapshot()` 提供线程安全连接快照；Agent Service 只消费该公开接口，不直接调用 `esp_wifi_*`。
- `main/main.c::app_main()` 当前先初始化 Settings Service 和 Wi-Fi Service，再初始化硬件、LVGL 与 App Framework；Agent Service 应在 Wi-Fi Service 之后异步初始化，不等待 Wi-Fi、HTTP 或首次数据。
- `main/CMakeLists.txt` 当前已声明 `lwip`，但尚未声明 `esp_http_client` 与 `json`；实施时按实际使用增加最小组件依赖。
- `sdkconfig.defaults` 与 `sdkconfig.ci` 保存可复现配置，本地 `sdkconfig` 被忽略。真实局域网 IP 不得写入可提交的默认配置。
- `.gitignore` 已排除根目录及子目录的 `.venv/`、`__pycache__/` 和 `.tmp/`，满足 PC Agent 虚拟环境与任务临时文件边界。

## 参考实现结论

### `esp32s3-lab/pc-monitor`

- 已验证 Python `ThreadingHTTPServer` + JSON、ESP32 HTTP 拉取、1 秒指标周期、500 ms 连接超时、1500 ms 总超时和失败退避。
- 已验证网络任务与 UI 快照分离、Mutex 保护、Wi-Fi 与 Agent 在线状态分离，以及失败响应不提交半截样本。
- 现有 `/api/v1/stats` 包含磁盘、每核 CPU 等本节点不需要的数据，不能原样作为 160 × 128 页面的一秒高频接口。
- 现有实现尚未落地 GPU 与温度；其 v0.6 只有 Provider 规划，不能当成已实现能力直接复制。

### `esp32s3-lab/ai-quota-monitor`

- 已验证不同服务商通过适配器归一化为统一额度快照，固件显示层不依赖上游原始字段。
- 已验证 `status`、`updated_at_*`、`age_sec` 和 `stale` 语义，以及“保留最后成功快照但明确标记过期”的处理。
- HTTP 请求只读取后台缓存，不在设备请求到达时现场查询 Codex 或智谱；该边界必须沿用。
- 现有 AI Agent 使用独立端口 `8767`。小喵后续接入时应并入本节点建立的统一 Agent，不在设备中配置第二个后端地址。

## 范围边界

### 本节点必须完成

- 在仓库根目录新增 `pc-agent/`，提供统一 Agent 的首版运行入口、PC 指标采集模块、HTTP 路由和自动化测试。
- 使用 Python 标准库提供 HTTP server；PC 指标采集使用经核对后的 `psutil`，GPU 首版优先支持可用的 NVIDIA `nvidia-smi`，其余硬件来源按不可用安全降级。
- 实现 `GET /api/v1/health` 与 `GET /api/v1/pc/metrics`，冻结 v1 字段、状态与错误语义。
- Agent 后台采集与 HTTP 请求线程解耦；并发请求只能读取同一份自洽缓存快照。
- 新增通用 Agent Service，负责固定地址配置、Wi-Fi 在线判断、HTTP 拉取、响应大小限制、JSON 校验、失败退避和 PC 指标快照。
- Agent Service 初始化和全部网络操作异步执行，不阻塞 `app_main()`、Launcher 或 LVGL。
- PC Monitor App 保存四项值标签引用，创建专用 LVGL timer 读取 Service 快照并更新页面；关闭 App 时先删除 timer，再清空标签引用。
- 对未配置、Wi-Fi 离线、Agent 离线、HTTP 非 200、超时、空响应、超长响应、非法 JSON、错误 schema、字段类型错误、越界数值、可选字段 `null` 和数据超时分别定义确定性行为。
- 增加 PC Agent 的 Python 自动化测试，并完成固件源码、配置、diff 和资源边界静态检查。
- 实施和验证完成后同步更新 `README.md`、`docs/project-overview.md`、`ROADMAP.md`、本 Goal 与历史索引。

### 本节点禁止实现或修改

- 不实现 mDNS、UDP 服务发现、广播、自动 IP 迁移、多 Agent 选择、`agent_id` 绑定或配对流程。
- 不实现 WebSocket、MQTT、TLS、Bearer Token、远程访问、云中转、反向代理或公网部署。
- 不实现 Codex、智谱、商汤日日新、OpenRouter 或其他 AI 服务商额度查询。
- 不实现 `/api/v1/pc/system`、`/api/v1/pc/processes`、`/api/v1/pc/disks` 的正式业务数据；这些名称只作后续规划，不得返回伪数据或空壳成功响应。
- 立项范围不增加磁盘、网络、进程、历史曲线或多页面 PC Monitor UI；当前交付的两页指标与滚动图属于实施阶段保留的 UI 扩展，差异及验收结论见“口径差异与实现决定”。
- 不修改 Wi-Fi Service 的凭据格式、配网页流程、自动连接策略或全局图标。
- 不让 PC Monitor App include `esp_http_client.h`、`esp_wifi.h`、`cJSON.h` 或 NVS 头文件。
- 不在固件或可提交文档中写入真实局域网 IP、电脑名、账号信息或服务商凭据。
- 不修改 GD32 工程、硬件引脚、分区表、Assets 分区、Dashboard 外设或节点 12～16 的功能。
- 不为未来功能创建通用插件系统、动态路由注册框架、事件总线或依赖注入框架。

## 总体架构

```text
PC
┌──────────────────────────────────────────────────────────────┐
│ Xiaomiao Agent :8766                                        │
│  ├─ PC Metrics Collector ── 后台缓存                         │
│  ├─ GET /api/v1/health                                      │
│  └─ GET /api/v1/pc/metrics                                  │
│                                                             │
│  后续：AI Quota Providers ── /api/v1/quotas/...              │
└──────────────────────────────┬───────────────────────────────┘
                               │ HTTP / JSON
ESP32                          ▼
┌──────────────────────────────────────────────────────────────┐
│ Wi-Fi Service ── 连接快照                                   │
│        │                                                     │
│        ▼                                                     │
│ Agent Service ── Worker／HTTP／校验／PC Metrics Snapshot     │
│        │                                                     │
│        ▼                                                     │
│ PC Monitor App ── LVGL timer ── CPU／RAM／GPU／温度标签      │
└──────────────────────────────────────────────────────────────┘
```

职责约束：

- PC Agent 负责平台差异、外部程序调用、指标归一化、缓存和 API；固件不承担 PC 采集逻辑。
- Wi-Fi Service 继续是网络连接状态的唯一事实来源；Agent Service 不创建第二套 Wi-Fi 状态机。
- Agent Service 是 HTTP 链路与 PC 指标快照的唯一所有者；App 只读取公开快照。
- 网络 Worker 不持有 LVGL 对象，不调用任何 LVGL API。
- LVGL timer 只在 UI 线程格式化和写标签，不能执行 HTTP、DNS、阻塞等待或外部锁内 I/O。

## 固定地址配置

首版通过 Kconfig 提供本地构建配置，建议命名：

```text
CONFIG_XIAOMIAO_AGENT_HOST=""
CONFIG_XIAOMIAO_AGENT_PORT=8766
```

约束：

- `HOST` 首版只接受 IPv4 文本，不接受 URL、路径、用户名、密码或查询串。
- 空 `HOST` 表示未配置，Agent Service 进入 `UNCONFIGURED`；不得反复请求、报错刷屏或阻塞启动。
- `PORT` 必须在 `1～65535`，默认 `8766`。
- `sdkconfig.defaults` 与 `sdkconfig.ci` 只保留空地址和安全默认端口；人工验证使用被忽略的本地 `sdkconfig` 设置真实地址。
- Service 内部确定性拼接 `http://<host>:<port>/api/v1/...`，App 不保存或拼接 URL。
- 节点 11 不把 Agent 地址写入 NVS，也不修改 Settings Service schema；在线配置与服务发现另立后续节点。

## API v1 公共契约

### 通用规则

- Content-Type 固定为 `application/json; charset=utf-8`。
- 正常响应必须带正确 `Content-Length`；固件对响应体设置硬上限，建议首版 `2048` 字节，超限整包拒绝。
- 所有业务端点使用 `/api/v1/` 前缀；破坏性字段变更必须进入新的 API 主版本。
- `schema_version` 是具体响应数据结构版本，首版固定为整数 `1`。
- HTTP `200` 表示返回了结构合法的业务快照，即使 `status` 为 `degraded`、`unavailable` 或 `stale`。
- 未知路径返回 `404`；Agent 未处理异常返回 `500`，响应不得包含 traceback、命令行、环境变量或敏感原文。
- `updated_at_epoch` 是缓存最后成功采集时刻的 Unix 秒；没有成功数据时为 `null`。
- `age_sec` 是当前响应时刻相对最后成功采集的年龄秒数；没有成功数据时为 `null`。
- `error_code` 只返回稳定、无敏感信息的机器可读代码；正常为 `null`。

### `GET /api/v1/health`

成功示例：

```json
{
  "schema_version": 1,
  "status": "ok",
  "data": {
    "agent_name": "xiaomiao-agent",
    "agent_version": "0.1.0",
    "api_version": 1,
    "capabilities": ["pc.metrics"]
  },
  "error_code": null
}
```

约束：

- 健康接口不触发即时采集，只证明 HTTP Agent 正在运行并声明已实现能力。
- 本节点 `capabilities` 只能包含实际可用的 `pc.metrics`，不得提前宣称 quota、system、processes 或 disks。
- 固件节点 11 的周期轮询不需要每秒先请求 health；health 只用于人工检查和必要的链路诊断。

### `GET /api/v1/pc/metrics`

完整数据示例：

```json
{
  "schema_version": 1,
  "status": "ok",
  "updated_at_epoch": 1790000000,
  "age_sec": 0.2,
  "data": {
    "cpu_percent": 23.4,
    "memory_percent": 61.2,
    "gpu_percent": 72.0,
    "cpu_temperature_c": null,
    "gpu_temperature_c": 64.0
  },
  "error_code": null
}
```

可选指标缺失示例：

```json
{
  "schema_version": 1,
  "status": "degraded",
  "updated_at_epoch": 1790000000,
  "age_sec": 0.3,
  "data": {
    "cpu_percent": 23.4,
    "memory_percent": 61.2,
    "gpu_percent": null,
    "cpu_temperature_c": null,
    "gpu_temperature_c": null
  },
  "error_code": "hardware_metrics_unavailable"
}
```

字段与状态约束：

- `cpu_percent` 与 `memory_percent` 是首版核心字段；成功快照中必须是有限数值且在 `0～100`。
- `gpu_percent`、`cpu_temperature_c`、`gpu_temperature_c` 是可选字段，可为有限数值或 `null`。
- `gpu_percent` 有值时范围为 `0～100`。
- 温度单位固定为摄氏度。Agent 应过滤明显无效、非有限或采集失败的值并返回 `null`；固件仍须执行自己的范围与有限性校验。
- 核心字段有效且全部可选字段有效时为 `ok`；核心字段有效但任一可选字段不可用时为 `degraded`。
- 尚无任何成功核心快照或核心采集失败且没有可用旧快照时为 `unavailable`，核心字段返回 `null`。
- 缓存超过 Agent 设定的新鲜度仍未成功更新时为 `stale`，可以保留最后成功值，但必须返回真实 `age_sec`。
- 固件只把 `ok` 或 `degraded` 且核心字段有效、`age_sec` 未超限的响应视为新有效快照；`unavailable`、`stale`、错误 schema 或非法核心字段不刷新设备的成功时间。

## PC Agent 设计

### 建议目录

```text
pc-agent/
├─ monitor.py                 # 进程入口与 HTTP 路由
├─ pc_metrics.py              # 后台采集、Provider 与线程安全缓存
├─ requirements.txt
└─ tests/
   ├─ test_http_api.py
   └─ test_pc_metrics.py
```

保持模块最少、职责清晰；后续 AI 额度接入时再增加 `quotas/` 或等价业务模块，不在节点 11 预建空目录和抽象层。

### 采集与缓存

- CPU 与内存优先复用 `esp32s3-lab/pc-monitor` 已验证的 `psutil` 采集方式。
- GPU 首版优先使用可用的 NVIDIA `nvidia-smi` 查询利用率和温度；命令不存在、超时、退出码非零、输出缺失或多 GPU 口径尚未满足时返回 `null`，不得让采集线程退出。
- CPU 温度通过当前平台和 `psutil` 实际可用能力采集；Windows 上不可用是允许的降级结果，不为节点 11 强制引入 LibreHardwareMonitor、WMI 包装库或驱动组件。
- 第三方依赖首版仅允许 `psutil`。实施前按项目规则核对其官方发布、维护状态与当前 Python 兼容性，再在 `requirements.txt` 中给出可复现约束；不得直接照抄参考项目的开放上界而不核对。
- Collector 在独立后台线程以约 1 秒周期更新一次不可变快照。HTTP 请求获取锁内拷贝后，在锁外 JSON 编码和写 socket。
- 外部命令必须设置明确超时，不使用 `shell=True`，不把 stderr、命令路径或原始输出返回给设备。
- 采集线程捕获单轮异常并在下一周期继续，不能因某个 Provider 失败静默退出。
- Agent 使用单调时钟计算缓存年龄，使用系统墙钟生成 `updated_at_epoch`；系统时间变化不得让 `age_sec` 变成负数。

### 运行与安全

- 默认绑定 `0.0.0.0:8766`；允许命令行覆盖 host／port 以便测试，但固件默认端口保持 `8766`。
- 只提供只读 GET 路由，不提供执行命令、修改设置、上传文件或转发任意 URL 的接口。
- 首版只允许在可信局域网运行；README 必须提示 Windows 防火墙仅放行“专用网络”，不得建议公网端口映射。
- 后续 AI 凭据只能留在 PC Agent 本地受控配置或现有登录态，绝不进入固件、HTTP 响应或普通日志。

## Agent Service 设计

### 建议文件

```text
main/services/xiaomiao_agent_service.h
main/services/xiaomiao_agent_service.c
```

如果 HTTP 事件处理明显影响可读性，可增加一个同目录私有实现文件；没有明确需要时不拆分。

### 状态模型

```c
typedef enum {
    XIAOMIAO_AGENT_UNINITIALIZED = 0,
    XIAOMIAO_AGENT_UNCONFIGURED,
    XIAOMIAO_AGENT_WIFI_OFFLINE,
    XIAOMIAO_AGENT_CONNECTING,
    XIAOMIAO_AGENT_ONLINE,
    XIAOMIAO_AGENT_DEGRADED,
    XIAOMIAO_AGENT_RETRY_WAIT,
    XIAOMIAO_AGENT_ERROR,
} xiaomiao_agent_state_t;
```

建议公开快照：

```c
typedef struct {
    xiaomiao_agent_state_t state;
    bool has_valid_metrics;
    bool cpu_valid;
    bool memory_valid;
    bool gpu_valid;
    bool cpu_temperature_valid;
    bool gpu_temperature_valid;
    float cpu_percent;
    float memory_percent;
    float gpu_percent;
    float cpu_temperature_c;
    float gpu_temperature_c;
    int64_t last_success_us;
    uint32_t consecutive_failures;
    int http_status;
    esp_err_t last_error;
} xiaomiao_agent_snapshot_t;
```

最终字段可以按实现需要等价微调，但必须保留以下语义：

- 每个指标都有独立有效位，不能用数值 `0` 表达缺失。
- `get_snapshot()` 复制整份自洽快照，不暴露内部指针、HTTP body 或 cJSON 对象。
- `last_success_us` 使用设备单调时钟，只在通过全部核心校验的新快照提交时更新。
- 失效后可在 Service 内保留最后数值用于诊断，但公开有效位必须按 3 秒规则清除，UI 不得继续显示旧值。
- 初始化失败或地址未配置时仍能安全读取快照。

建议公开接口：

```c
esp_err_t xiaomiao_agent_service_init(void);
esp_err_t xiaomiao_agent_get_snapshot(xiaomiao_agent_snapshot_t *out_snapshot);
```

首版不提供 App 主动触发刷新、修改地址或停止 Worker 的接口。Service 随固件运行，App 的打开／关闭不拥有其生命周期。

### Worker 调度

- `xiaomiao_agent_service_init()` 幂等，创建同步原语和一个后台 Worker 后立即返回。
- Worker 通过 `xiaomiao_wifi_get_snapshot()` 判断是否已取得 IPv4；未连接时不创建 HTTP 请求，以短等待周期继续观察。
- 成功拉取后约 1 秒再次拉取；失败后至少等待 2 秒，避免 Agent 停止时忙循环。
- HTTP 连接超时目标 500 ms，总超时目标 1500 ms；实施时按 ESP-IDF 6.1 `esp_http_client` 实际语义核对并记录最终值。
- 响应接收使用固定上限，不允许无界增长；所有退出路径清理 client、body 和 cJSON 资源。
- 网络 I/O、JSON 解析和日志均在 Mutex 外执行；锁内只提交或复制小型快照。
- Worker 不主动调用 `esp_wifi_*`，不改变 Wi-Fi 自动连接和退避策略。
- 任务栈、优先级和分配能力必须结合节点 10 已确认的内部 RAM 紧张现状确定，并在实机记录空闲 Heap 与栈高水位；不得复制 ESP32-S3 参考项目的 10 KB 栈大小。

## PC Monitor App 接入

- 将四项值标签保存为独立 `lv_obj_t *`，不能再像节点 6 一样创建后即丢弃引用。
- App 打开时创建一个专用 LVGL timer，建议周期 500～1000 ms；timer 只调用 `xiaomiao_agent_get_snapshot()`、格式化短字符串并更新标签。
- App 关闭时按“删除 timer → 清空 timer 指针 → 清空四项标签引用 → 清空容器引用”的顺序释放；内容根继续由 Navigation 删除。
- CPU、RAM、GPU 有效时显示一位小数或与屏幕宽度相容的确定性格式，无效时显示 `-- %`。
- 温度优先级为 GPU → CPU → 无；值有效时显示 `NN °C` 或一位小数的等价紧凑格式，并把左侧标签同步为 `GPU T`／`CPU T`，无值时恢复 `TEMP` 与 `-- °C`。
- App 不获取输入焦点，不改变现有短按 B 返回语义，不新增 FreeRTOS Task、Queue、Mutex 或网络对象。
- 页面继续只显示四项指标和 `B Back`，节点 11 不增加配置、状态详情或其他页面。

## 失败路径与边界条件

| 场景 | 预期结果 |
| --- | --- |
| Agent host 为空 | Service 为 `UNCONFIGURED`，不发请求；Launcher 与 App 正常，指标为 `--` |
| Wi-Fi 未连接／断线 | Service 为 `WIFI_OFFLINE`，不发 HTTP；3 秒后清有效位；Wi-Fi 恢复后自动拉取 |
| PC Agent 未启动／端口不可达 | 请求按超时返回并进入 `RETRY_WAIT`，无忙循环、无启动阻塞 |
| HTTP 非 200 | 整次失败，不解析业务 body，不更新成功时间 |
| 空 body／超过上限 | 整次失败并释放资源 |
| 非法 JSON／错误 schema | 整次失败，不提交半截指标 |
| `status=unavailable/stale` | 不作为新有效快照，不更新设备成功时间 |
| CPU／内存缺失、为 `null`、非数值、NaN、Inf 或越界 | 核心快照失败，整次不提交 |
| GPU／温度缺失或为 `null` | 对应有效位清除，其他有效指标正常提交 |
| GPU 百分比或温度异常值 | 只拒绝对应可选字段；核心 CPU／内存仍可提交并标记降级 |
| 连续 3 秒没有新有效核心快照 | 所有 PC Monitor 显示值失效为 `--`，不显示旧值冒充实时数据 |
| Agent 恢复 | 下一次有效响应原子替换快照，页面自动恢复 |
| App 关闭时 Worker 正在请求 | Service 继续运行；App 不持有网络资源，不访问已删除标签 |
| 多个设备同时请求 Agent | HTTP 线程只读取缓存，不重复触发采集，不产生互相覆盖的速率基准 |

## 施工检查点

### CP1：PC Agent 基线与 API 契约

目标：建立可独立运行和测试的统一 Agent 首版。

实施：

- 创建 `pc-agent/.venv/` 前确认 `.gitignore` 已排除 `.venv/`（当前已满足）。
- 核对 `psutil` 官方维护与兼容性，建立 `requirements.txt`。
- 实现后台采集、线程安全缓存、`health` 和 `pc/metrics` 两个路由。
- 为正常、降级、不可用、过期、并发请求、外部命令超时和异常恢复编写测试。

验收：

- Python 语法检查通过。
- 自动测试通过。
- 本机启动后两个端点字段、类型、状态码与本文契约一致。
- 无 GPU／无温度 Provider 时 Agent 仍正常运行，返回 `null` 和 `degraded`。
- 20 路并发读取不触发重复采集、异常或撕裂快照。

### CP2：Agent Service 与固定地址

目标：固件建立不阻塞启动的通用 HTTP 数据入口。

实施：

- 新增 Kconfig 地址与端口配置，默认地址为空。
- 新增 Agent Service、状态快照、Worker、HTTP 拉取、响应上限、JSON 校验与失败退避。
- 在 Wi-Fi Service 初始化之后启动 Agent Service；失败只记一条明确警告并继续启动。
- 更新 `main/CMakeLists.txt` 的源码和最小组件依赖。

验收：

- 静态确认 App／Framework 不直接使用 HTTP、cJSON、Wi-Fi 或 NVS API。
- 地址为空、Wi-Fi 离线、HTTP 失败和解析失败均有确定性状态且不阻塞 Launcher。
- Mutex 临界区不包含网络、JSON、日志或 LVGL 操作。
- 所有 HTTP 与 JSON 退出路径可静态证明资源释放完整。

### CP3：PC Monitor UI 接入

目标：用真实快照替换节点 6 的编译期占位值。

实施：

- 保存四项标签引用并建立 LVGL timer。
- 实现有效值、可选字段缺失、温度来源和 3 秒失效显示。
- 实现关闭路径清理并保持节点 6 的 Navigation 与短按 B 语义。

验收：

- App 源码不创建网络任务或直接发请求。
- timer 只在 App 打开期间存在，关闭后不访问释放对象。
- CPU／RAM／GPU／温度显示与 Agent 响应一致；不可用字段为 `--`。
- 重复进入／返回后 open／close 配对，`screen children` 不增长。

### CP4：软件验证与静态复核

目标：在人工固件构建前清除接口、资源和文档不一致。

实施与验收：

- 运行 PC Agent 的语法检查、单元测试、接口契约测试和并发读取测试。
- 对固件执行精确检索、diff、括号／静态引用、组件依赖、配置默认值、响应上限、资源释放和敏感字面量检查。
- 确认仓库不包含真实 IP、Token、Cookie、密码、Agent 原始命令输出、`.venv/` 或缓存文件。
- 更新 README、项目概览和人工验证命令，但不得把未构建、未烧录或未实测项写成已通过。

### CP5：人工构建与实机验收

目标：由项目负责人完成项目规则要求的 ESP-IDF 6.1 构建、烧录、串口与页面验证。

至少覆盖：

- 固件使用本地真实 Agent IPv4 与端口构建、烧录和启动。
- PC Agent 未启动时 Launcher 正常，PC Monitor 显示 `--`，无 watchdog、panic 或重启。
- Agent 启动后 CPU／RAM 在 2 秒内出现并持续更新。
- 当前电脑若能提供 GPU／温度，显示值与 Agent JSON 一致；无法提供的字段正确显示 `--`，不阻塞验收。
- 停止 Agent 超过 3 秒后值恢复 `--`；重启 Agent 后自动恢复。
- 关闭／恢复 Wi-Fi 后自动恢复数据，不需要重启 App 或设备。
- 至少覆盖一次非法 JSON、错误 schema、核心字段缺失或越界响应，设备拒绝该响应且保持可操作。
- PC Monitor 累计至少 5 次进入／返回，open／close 严格配对、`screen children` 稳定、无 timer 残留。
- Games、Tools、Settings、Hardware Test 均至少完成一次进入／返回；三套 B 键语义与 Hardware Test 15 页保持可用。
- 记录 Agent Worker 的任务栈高水位、连接前后空闲 Heap 或等价资源证据，确认未加剧节点 10 已知的内部 RAM 风险。

## 验证命令

### PC Agent 软件验证

在仓库根目录执行，必须使用 `pc-agent/.venv/`：

```powershell
python -m venv pc-agent/.venv
pc-agent/.venv/Scripts/python -m pip install -r pc-agent/requirements.txt
pc-agent/.venv/Scripts/python -m py_compile pc-agent/monitor.py pc-agent/pc_metrics.py
pc-agent/.venv/Scripts/python -m unittest discover -s pc-agent/tests -q
pc-agent/.venv/Scripts/python pc-agent/monitor.py
```

另开终端检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8766/api/v1/health
Invoke-RestMethod http://127.0.0.1:8766/api/v1/pc/metrics
```

实施时如文件名发生经说明的等价调整，同步修正文档命令，不保留失效入口。

### 固件人工验证

Agent 不执行以下命令；由项目负责人在加载 ESP-IDF 6.1 环境、设置本地 Agent IPv4 后执行：

```powershell
python "$env:IDF_PATH\tools\idf.py" --version
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

预期：版本为 `ESP-IDF v6.1`；构建和烧录成功；无 Agent 时 Launcher 不受阻塞；Agent 启动后 PC Monitor 显示真实指标；断网、Agent 停止、异常响应与恢复路径符合 CP5。

## 验收标准

- [x] 统一 PC Agent 只监听一个端口，并实现契约一致的 `/api/v1/health` 与 `/api/v1/pc/metrics`。
- [x] Agent 采集在后台执行，HTTP 请求只读取缓存；并发请求不会重复触发采集或产生撕裂快照。
- [x] CPU／内存为有效核心字段，GPU／CPU 温度／GPU 温度不可用时以 `null` 安全降级。
- [x] 固件 Agent Service 异步初始化并复用 Wi-Fi Service，不阻塞启动、不直接调用 `esp_wifi_*`。
- [x] 固定 IPv4 与端口来自本地构建配置，可提交配置中不含真实局域网地址。
- [x] HTTP 非 200、超时、超长响应、非法 JSON、错误 schema、字段缺失和越界值均按本文边界拒绝。
- [x] PC Monitor App 只读取 Service 快照，四项指标持续更新，温度来源明确，失效值显示 `--`。
- [x] 连续 3 秒无有效数据后不再显示旧值，Agent 或 Wi-Fi 恢复后自动恢复。
- [x] App 重复进入／返回后 timer、对象和 screen child 数量稳定，无崩溃、panic、重启或输入失效。
- [x] PC Agent 自动测试、固件静态检查、ESP-IDF 6.1 人工构建、烧录和实机场景均已取得自动化、静态或人工确认。
- [x] README、项目概览、ROADMAP、Goal 与历史索引已同步，明确 AI 额度和服务发现仍未实现。

## 预期交付文件

```text
pc-agent/monitor.py
pc-agent/pc_metrics.py
pc-agent/requirements.txt
pc-agent/tests/test_http_api.py
pc-agent/tests/test_pc_metrics.py
main/services/xiaomiao_agent_service.h
main/services/xiaomiao_agent_service.c
main/apps/pc_monitor/xiaomiao_pc_monitor.c
main/CMakeLists.txt
main/main.c
Kconfig.projbuild 或等价项目 Kconfig
sdkconfig.defaults
sdkconfig.ci
README.md
docs/project-overview.md
ROADMAP.md
goals/ROADMAP-history.md
goals/20260921-1238-pc-monitor-communication.md
```

文件列表是当前设计预期，不要求为了对齐列表创建无用途文件；实施中若以更少文件满足职责和验收，应采用更简单实现并在本 Goal 记录差异。

## 完成判定与未验证范围

- 节点 11 已满足完成条件：PC Agent 软件验证、固件静态复核、人工 ESP-IDF 6.1 构建、烧录及 CP5 实机场景均通过。人工部分由项目负责人确认，未附新增串口日志、截图或资源数值；该证据边界不改变功能验收结论。
- GPU 或温度在验证电脑上确实没有可用 Provider 时，可以把对应字段记为“不可用降级已验证”，不要求伪造硬件数据；但必须证明 Agent、协议和 UI 的 `null`／`--` 路径正确。
- 无法构造的底层内存分配失败、DNS 栈故障或路由器异常只能标记为未验证，不得根据源码推测写成实机通过。
- 本节点完成后保留本施工文档，在文档末尾追加实际改动、验证证据、口径差异和未验证范围，并同步 `ROADMAP.md` 当前状态。

## 立项时未验证事实（历史记录）

- 尚未创建或运行本项目的 PC Agent，本文 API 是已确认施工契约，不是当前已实现能力。
- 尚未核对并固定本项目最终使用的 `psutil` 版本。
- 尚未验证 ESP-IDF 6.1 `esp_http_client` 在本项目内存布局下的实际 Heap、任务栈和连续运行表现。
- 尚未确认当前验证电脑能否提供 NVIDIA GPU、GPU 温度或 CPU 温度；对应字段允许按协议降级。
- 尚未执行任何固件构建、烧录、Monitor 或实机操作。

## 实施记录（2026-09-21，CP1～CP4）

### 实际交付

- CP1：新增 `pc-agent/monitor.py`（HTTP 路由与进程入口）、`pc-agent/pc_metrics.py`（后台采集、Provider、线程安全缓存）、`pc-agent/requirements.txt`、`pc-agent/tests/test_http_api.py`、`pc-agent/tests/test_pc_metrics.py`。`requirements.txt` 固定 `psutil>=7.0,<8`：经核对，psutil 7.2.2 为当前稳定版（PyPI 2026-01-28 发布，BSD-3-Clause，官方维护活跃，CPython 3.6～3.14 全覆盖，含 Windows wheel），未照抄参考项目的开放上界。
- CP2：新增 `main/Kconfig.projbuild`（`CONFIG_XIAOMIAO_AGENT_HOST` 默认空、`CONFIG_XIAOMIAO_AGENT_PORT=8766`）、`main/services/xiaomiao_agent_service.{h,c}`（8 状态机、Worker、流式限长 HTTP 读取、cJSON schema 校验、锁内快照提交/复制）；`main/CMakeLists.txt` 增加源文件与 `esp_http_client`、`json` 组件依赖；`main/main.c` 在 Wi-Fi Service 之后接入 Agent Service init（失败只记警告）；`sdkconfig.defaults` 追加空地址与默认端口。
- CP3：`main/apps/pc_monitor/xiaomiao_pc_monitor.c` 保存四行名称与值标签引用，open 时创建 1 秒专用 LVGL timer 并立即刷新一次，close 按“删 timer → 清标签引用 → 清容器引用”顺序释放；温度行按 GPU T → CPU T → TEMP 切换名称；数值格式化为手工一位小数（不依赖 printf `%f`，规避 newlib 浮点格式化被裁剪的风险）。
- CP4 静态复核结果：7 个改动文件括号／圆括号配平、无制表符、无行尾空白；`apps/`、`framework/` 无 `esp_http_client`／`cJSON`／`esp_wifi_*`／NVS 引用（命中项均为注释文本或既有代码）；`esp_http_client_init`／`cJSON_Parse` 全仓库仅存在于 `xiaomiao_agent_service.c`；`agent_fetch()` 所有退出路径均执行 `esp_http_client_close()` + `esp_http_client_cleanup()`；Kconfig help 中的示例地址改为 `192.168.1.x` 形式，仓库不含具体局域网 IP、凭据或 Token。

### 软件验证证据（CP1 验收）

- `pc-agent/.venv/Scripts/python -m py_compile pc-agent/monitor.py pc-agent/pc_metrics.py` 通过。
- `pc-agent/.venv/Scripts/python -m unittest discover -s pc-agent/tests -v`：实施阶段 17 个测试全部通过（首个版本 14/17，3 个失败均为测试样本默认 `cpu_temp=None` 却断言 `ok` 的自相矛盾，修正样本后 17/17；实现未改动）。最终收尾复测为 18/18，覆盖：ok／degraded／unavailable／stale 四态、核心字段失败保留旧快照、Provider 异常不致崩溃、非法值拒绝、真实 0 不当缺失、快照私有拷贝、8 线程并发读一致性、HTTP 契约（Content-Length／Content-Type／404／405）、20 线程 × 10 次并发请求及 NVIDIA 采集路径。

### 口径差异与实现决定

1. **HTTP 超时**：ESP-IDF 6.1 `esp_http_client` 无独立连接超时配置项，最终采用 `timeout_ms = 1500` 覆盖整个请求的读写预算；“连接超时 500 ms”未能单独实现，按本文“按实际语义核对并记录最终值”条款记录。
2. **ok/degraded 判定**：本文 API 契约的“完整数据示例”（`cpu_temperature_c: null` 且 `status: ok`）与判定文字“全部可选字段有效时为 ok”矛盾。实现以文字规则为准：任一可选字段缺失即 `degraded`；示例视为形状演示。
3. **可选字段类型错误**：失败路径表未列“GPU／温度字段为非数值类型”的场景。实现按“只拒绝对应可选字段”处理（清除有效位、其余照常提交），不整次失败。
4. **Worker 栈**：4096 字节（本文禁止照抄 S3 参考项目的 10 KB）；栈上仅持指针，2 KB 响应缓冲为静态分配（内部 RAM BSS）。CP5 功能回归已通过，但未提供高水位与空闲 Heap 的具体数值。
5. **响应年龄上限**：设备侧拒绝 `age_sec` ≥ 3.0 的响应（与 3 秒显示规则对齐）；Agent 侧新鲜度为 10 秒，超时返回 `stale`。两层独立生效。
6. **URL 日志**：init 成功时打印完整 URL（含用户本地配置的 IP），用于实机诊断，与 Wi-Fi Service 打印 SSID 同级；仓库源码不含任何真实地址。
7. **PC Monitor 页面形态**：当前交付实现把四项指标分为 CPU／RAM 与 GPU／TEMP 两页，并增加 60 点滚动图、左右切页和 App 内焦点处理；这比立项时“单页四行”的文字方案更丰富，但不改变 Agent API、Service 快照和 3 秒失效语义。项目负责人已确认本节点手动回归通过，本收尾保留现状。

### CP5 人工验证证据（2026-09-21）

- 项目负责人确认节点 11 已完成 ESP-IDF 6.1 构建、烧录，并完成 CP5 所列 PC Agent／固件联调与手动回归：未启动 Agent 时 Launcher 与 `--` 降级、Agent 启停与 3 秒失效／恢复、Wi-Fi 恢复、异常响应拒绝、PC Monitor 重复进入／返回及既有 App 回归均通过。
- 该确认未附新增串口日志、截图、Worker 栈高水位或空闲 Heap 数值；Agent 未重复执行项目规则禁止的构建、烧录和串口监视。上述项目以人工确认作为验收证据，不把缺失的数值或日志补写为已观测事实。

### 当前剩余未验证范围

- 目标电脑的底层内存分配失败、DNS／网络栈故障等不可安全构造的路径仍只能按源码检查确认。
- Worker 栈高水位与空闲 Heap 的具体数值未随人工确认提供；功能回归通过，但资源基线未形成可复核的数值记录。
