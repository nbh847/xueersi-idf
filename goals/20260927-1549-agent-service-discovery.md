# PC Agent 局域网动态发现

状态：已完成。PC 端与固件端实现及软件侧检查完成。首个局域网实机验收通过：ESP32 动态发现 Agent，CPU／内存指标正常显示；停止 Agent 后指标失效，重启后自动恢复。2026-09-28 项目负责人确认本 Goal 收口，更换 Wi-Fi 环境后的复验不再作为未完成项保留。每次使用都要求电脑与设备处于同一可互通局域网。

创建时间：2026-09-27 15:49（北京时间）

## 目标

ESP32 在 Wi-Fi Service 报告 STA 已取得 DHCP IPv4 后，自动发现当前局域网里运行的 `Xiaomiao Agent`，再读取 `/api/v1/pc/metrics`。更换 Wi-Fi 环境并重新联网后，应重新发现 Agent，不要求把 PC 的 IPv4 写入 `sdkconfig`、NVS 或固件源码。

Mac 后端已现场返回 API v1 `degraded` 快照，包含有效 CPU／内存字段、缺失 GPU／温度字段。固件已接受 `degraded` 且仅要求有效 CPU／内存字段，因此 Mac 平台的可选指标缺失不是本任务的传输阻塞原因。

## 边界

- PC Agent 保留现有 HTTP API、只读语义和默认 `0.0.0.0:8766`；`--port` 指定的实际 HTTP 端口必须由发现响应提供。
- 固定 UDP 发现端口为 `8767`，不引入第三方依赖。PC Agent 监听 `0.0.0.0:8767`，ESP32 STA 在拿到 DHCP 地址后向 IPv4 limited broadcast 发送发现请求，Agent 以 UDP 单播回应。
- 发现响应中的 HTTP 端口使用网络字节序；ESP32 使用 UDP 响应的源 IPv4 与端口建立 HTTP URL，不使用 ESP32 自己的 DHCP 地址作为 Agent 地址。
- Agent Service 在 Wi-Fi 离线时清除当前服务地址；重新联网或 HTTP 链路失效后重新发现。发现失败采用有间隔的重试，不阻塞固件启动或 UI。
- 每次使用都要求设备与电脑处于同一可互通局域网；每个广播域只支持一个 Xiaomiao Agent。不实现多 Agent 选择、配对、认证、TLS、跨子网发现、mDNS、全网 IP 扫描或互联网中继。
- HTTP 8766 与 UDP 8767 均需允许局域网入站。Wi-Fi AP 的客户端隔离、防火墙或广播过滤会阻断发现／直连，属于网络环境限制。
- 节点 11 的“禁止服务发现”只作为原节点历史边界保留；本补充 Goal 授权实现 PC Agent 动态发现，并取代其中对本任务的限制。

## 已确定的发现协议

- 请求：ASCII `XIAOMIAO_AGENT_DISCOVER_V1`，发往 `255.255.255.255:8767`。
- 响应：恰好 6 字节；前 4 字节为 ASCII `XMA1`，后 2 字节为 HTTP TCP 端口的无符号大端整数。
- Agent 仅对完整匹配的请求响应，并向请求源地址与端口单播返回。ESP32 忽略格式错误、端口为 0 或来源不是 IPv4 的响应。
- ESP32 限时等待发现响应；首个有效响应确定当前 Agent。单个局域网只运行一个 Agent 是本 Goal 的使用前提。
- Worker 使用既有 Agent Service 任务串行处理发现与 HTTP；指标快照、API v1 JSON 契约和 3 秒失效语义不变。

## 实施范围

- `pc-agent/monitor.py`：新增标准库 UDP 发现 responder 生命周期，响应中通告命令行实际 HTTP 端口，并在启动日志中报告 UDP 发现监听地址。
- `main/services/xiaomiao_agent_service.{c,h}`：增加发现状态与 UDP 广播发现；删除编译期固定 Agent IPv4／端口依赖；缓存并刷新动态 IPv4／端口。
- `main/Kconfig.projbuild`、`sdkconfig.defaults`：移除不再使用的静态 Agent IPv4／端口配置。忽略的本地 `sdkconfig` 不手工修改。
- `README.md`、`docs/project-overview.md`、`ROADMAP.md`、本 Goal 与 `goals/ROADMAP-history.md`：同步实际行为、进行中状态、验收证据和未验证范围。
- 不改 PC Monitor UI、Wi-Fi Service 的 DHCP 行为或现有 Python 依赖。

## 检查点与验收

1. **协议与 PC responder**：PC 端只响应精确发现请求，响应固定 6 字节且通告实际 HTTP 端口；默认启动日志同时报告 HTTP 与 UDP 监听端口。静态检查确认没有新增依赖。
2. **固件发现链路**：无静态 host 配置时 Agent Worker 等待 Wi-Fi；Wi-Fi 在线后发现服务并拉取当前 API。离线、无 Agent、格式错误响应和 HTTP 失败均保持可重试；恢复 Wi-Fi／Agent 后可恢复指标。
3. **不同 Wi-Fi 环境实机验收**：Mac 与 ESP32 在同一可互通局域网时 CPU／内存显示有效值；换到另一 Wi-Fi 后仍让电脑与设备处于同一可互通局域网，无需配置或重刷 PC 地址，Agent 能再次发现并更新数据。
4. **可选指标降级**：Mac 的 `gpu_percent`、`cpu_temperature_c`、`gpu_temperature_c` 为 `null` 时，CPU／内存仍显示有效值，GPU／温度按既有行为显示空值。
5. **人工部署边界**：Agent 可从 Mac shell 与 Windows Git Bash 启动；目标板构建、烧录、串口日志和按键／屏幕验收由项目负责人执行，Agent 不运行 `idf.py build`、`flash` 或 `monitor`。

## 人工验证命令与预期

PC 端从仓库根目录启动：

```bash
bash pc-agent/start.sh
```

预期日志包含 HTTP `0.0.0.0:8766` 与 UDP `0.0.0.0:8767`。设备和电脑须处于同一可互通局域网，电脑防火墙允许 TCP `8766` 和 UDP `8767` 入站。

目标板沿用项目既有人工流程：

```text
idf.py -p COM5 flash monitor
```

验收时记录首次发现、CPU／内存显示、切换到另一 Wi-Fi 环境后在该局域网重新发现、停止 Agent 后失效，以及重启 Agent 后恢复的串口日志与屏幕结果。每个 Wi-Fi 环境内电脑和设备都须保持可互通，发现日志不得记录敏感环境数据。

## 关键失败路径

- UDP 发现端口绑定失败：PC Agent 启动失败并给出明确端口错误，避免 HTTP 看似正常但设备永远发现不到服务。
- UDP 请求无回应：固件保持等待／重试状态，继续正常 Launcher，不访问旧缓存地址。
- 广播响应无效：丢弃并继续等待，绝不把任意 LAN IPv4 拼成 Agent URL。
- HTTP 连接或 API 响应失败：清除已发现地址并重新发现；旧指标按既有 3 秒规则失效。
- AP 客户端隔离、网络防火墙或广播过滤：发现不成功；记录为网络可达性限制，不增加全网扫描绕过。

## 外部依据

ESP-IDF v6.1 官方 lwIP 文档的 BSD Sockets API 章节说明该版本支持常用 BSD socket 操作，列出 `SO_BROADCAST`，并链接 UDP client／server 示例。本实现只使用已支持的 BSD sockets API，不直接调用未经支持的 lwIP Netconn／Raw API。文档由 OpenCLI 浏览器只读核对：

<https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/lwip.html>

## 交付结果

- PC Agent 已在 `0.0.0.0:8767/udp` 响应精确发现请求，并通告 HTTP 实际端口；启动日志显示 HTTP 与 UDP 监听地址。
- ESP32 Agent Service 已移除静态 PC 地址配置，Wi-Fi 联网后通过广播发现并轮询；离线、发现失败或 HTTP 失败后按间隔重试。
- Python 自动化测试共运行 20 项，19 项通过、1 项因本机没有 `nvidia-smi` 跳过。既有 HTTP 并发压力用例暴露默认监听队列 5 小于其 20 个并发客户端，HTTP 服务现把队列调整为 64。
- 2026-09-27 实机验收：PC Agent 启动日志同时报告 HTTP `0.0.0.0:8766` 与 UDP `0.0.0.0:8767/udp`；ESP32 DHCP 地址为 `192.168.0.105/24`，串口报告发现 Agent `192.168.0.104:8766`。PC Agent 连续收到 `/api/v1/pc/metrics` 的 HTTP 200，设备 PC Monitor 显示 CPU／内存数值。Mac API 的 GPU／温度可选字段缺失，显示为空符合已确认的降级行为。停止 Agent 后指标失效，重启 Agent 后 HTTP 200 与 CPU／内存显示恢复。
- 2026-09-28 收口：项目负责人确认本 Goal 标记为已完成，更换 Wi-Fi 环境后的复验不再保留为未完成项；该确认未附新增串口日志或截图。
