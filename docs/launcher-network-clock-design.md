# Launcher 联网时钟设计

状态：设计已确认，固件已实施并完成实机验收。2026-10-01 16:57 用户接受设计并生成 Goal 文档；同日用户授权实施，CP0～CP3 完成（Time Service、Launcher 标签、启动集成、SNTP 配置，宿主检查 PASS），18:21 用户确认构建、烧录及实机场景全部通过。实施与验收记录见 `goals/20261001-1657-launcher-network-clock.md`。

## 目标与边界

将 Launcher 左上角 `Xiaomiao` 替换为联网校准的当前时间；没有有效时间时，该位置留空，不回退品牌文字，不显示 `--:--`、1970 年时间或同步提示。

用户已确认一行完整显示年月日与时分：北京时间（UTC+8）、24 小时制 `YYYY-MM-DD HH:MM`，例如 `2026-10-01 16:35`；不显示秒、闪烁冒号，不增加设置项。只覆盖 Launcher 两页；进入 App 后沿用原页面标题，待机时由现有覆盖层遮住。电池显示方案继续暂缓。

## 实施前源码依据（设计基线）

- `main/framework/xiaomiao_launcher.c` 的 `launcher_build_ui()` 创建局部 `title` 标签并写入 `Xiaomiao`；位置为 `(5, 2)`，尺寸为 `150 × 14`，卡片从 `y=19` 开始。标题目前没有刷新机制。
- Launcher 的创建、销毁由 Framework 管理；销毁时已有先停止光带动画再删除根对象的顺序。新增时钟 timer 必须遵循同样生命周期约束。
- `main/services/xiaomiao_wifi_service.h` 提供复制式快照，`CONNECTED` 与非零 IPv4 可作为网络可用条件。Service 切到非连接状态会清空旧 IPv4；关联 AP 或启动 SoftAP 不等于互联网可达。
- 普通启动链在 `main/main.c` 初始化 Wi-Fi 后初始化 Agent Service；联网时钟应接在 Wi-Fi 初始化之后，不能等待 DHCP、DNS 或首次校时。
- `main/CMakeLists.txt` 已依赖 `esp_netif`、`lwip`、`esp_event` 与 `esp_timer`。不需要新增第三方依赖、硬件、NVS 字段或 PC Agent 接口。当前未发现固件已有校时模块。

## 显示与有效性规则

“拿不到时间”解释为没有有效的联网校时结果，而非每次 Wi-Fi 断线都立即清空。规则如下：

| 情况 | 左上角行为 |
| --- | --- |
| 开机尚未成功校时，包括无凭据、离线、仅有局域网 | 留空 |
| 首次成功接收合法 SNTP 时间 | 最迟 1 秒内显示当前 `YYYY-MM-DD HH:MM` |
| 校时成功后短暂断网、关闭 Wi-Fi 或忘记网络 | 继续根据系统时钟走时 |
| 上次有效校时距今不足 24 小时 | 保持显示，后台按网络条件继续校时 |
| 满 24 小时未再次有效校时，或时间转换失败、时间非法 | 隐藏标签；后台继续尝试，成功后恢复 |
| 重启或断电 | 重置本次开机有效标志，重新校时成功前留空 |
| SNTP 校正导致时间前跳或回退、跨小时、午夜、月末或年末 | 直接显示新的当前时间，无过渡动画 |

24 小时是首版保守的显示有效期，并非经过测量的精度保证；是否需要延长，可根据实机漂移再决定。有效期使用 `esp_timer_get_time()` 的 64 位单调钟计算，不能使用被 SNTP 校正的墙上时间计算。

只在本次开机收到合法同步通知后授予有效性，不能仅凭系统年份看似正确就显示。合法 UTC 范围为 `[2026-01-01, 2100-01-01)`，检查秒与微秒字段；非法同步立即使显示无效，直到下一次合法同步恢复。范围检查不等于认证：普通 SNTP 不提供可信身份保证，本功能只服务屏幕时钟，不能用作安全判断。

## 实现方案

### Time Service

新增 `main/services/xiaomiao_time_service.{h,c}`，作为系统时间与 SNTP 生命周期的唯一所有者。Wi-Fi Service 继续负责无线网络，Time Service 不直接操作 `esp_wifi_*`，Framework 不直接调用 SNTP。

已实现接口：

```c
esp_err_t xiaomiao_time_service_init(void);
void xiaomiao_time_service_poll(void);
esp_err_t xiaomiao_time_get_snapshot(xiaomiao_time_snapshot_t *out);
```

快照最小字段为 `bool valid` 与 `char datetime[17]`（16 个可见 ASCII 字符加 NUL）。`get_snapshot(NULL)` 返回 `ESP_ERR_INVALID_ARG`；未初始化或初始化失败时保证输出无效、空字符串。输出总是 NUL 结尾，不把内部指针交给 UI。

初始化幂等：创建必要同步保护、注册 SNTP 同步通知，调用 `esp_netif_sntp_init()` 并设 `config.start=false`。Wi-Fi 栈初始化失败时返回错误，启动只记警告并继续；不调用同步等待接口。失败清理已注册通知及已创建资源，不留下半初始化服务。

`poll()` 放入普通 UI 主循环，内部每 1 秒才读一次 Wi-Fi 快照并处理网络状态变化；只调用非等待型控制接口，不在 UI 线程解析 DNS、收发 socket 或等待同步。首次发现 `CONNECTED` 且 IPv4 非零时调用 `esp_netif_sntp_start()`；网络失效时使用 `esp_netif_sntp_deinit()` 停止并释放 SNTP，恢复网络时重新初始化并启动。SNTP 实例状态与“上次校时仍有效”分开保存，停止网络组件不清空尚在有效期内的时间。

启动失败后按 30 秒间隔再次尝试初始化／启动，禁止每秒重复打印或循环重启。IP 变化触发一次 SNTP 重建；普通扫描、配网期间出现非连接快照时暂停校时，配网收尾重新取得 IPv4 后自动恢复。网络恢复不依赖进入 Launcher 或用户按键。

同步通知只校验并提交有效标志和单调时间戳，使用短临界区或互斥锁保护，不调用 LVGL，不格式化、不做网络 I/O。读取快照时在短临界区复制有效性与最后同步单调时间，随后在锁外读取单调钟和系统时间并格式化。宿主桩验证状态与时间边界，不代表已完成真实 lwIP 回调与 UI 并发压力验证。

系统保留 UTC，显示层在 Service 内将 UTC 秒数加 `8 × 3600` 后通过 `gmtime_r()` 转换并生成 `YYYY-MM-DD HH:MM`，不修改进程全局 `TZ`。这足以处理固定 UTC+8，避免影响其他 Service。读取失败、越界、过期或转换失败一律返回无效快照。

### 校时配置与重试

使用 ESP-IDF 内置 SNTP，立即校正模式，不使用平滑校正。首版候选服务器为 `ntp.aliyun.com` 与 `pool.ntp.org`，两个名称作为编译期常量；它们在目标网络的实际可达性未验证，不承诺任一个必然可用。DNS 和 UDP 123 不可达时按 lwIP SNTP 自带超时与退避继续尝试，不另写联网探测、HTTP 时间接口或重试 Task。

在 `sdkconfig.defaults` 与 `sdkconfig.ci` 中对齐 `CONFIG_LWIP_SNTP_MAX_SERVERS=2` 与 `CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`（每小时校时）。实施前核对实际 ESP-IDF 6.1 Kconfig 与默认配置，避免依赖本地 `sdkconfig`。服务器切换、失败重试上限与恢复延迟需在实际 6.1 实现及目标网络验证；首次同步没有固定成功时限。

不新增 FreeRTOS Task、专用软件 timer 或队列，网络工作复用 lwIP。服务日志只记录初始化失败、有效性变化与成功同步，不每秒输出，不记录 Wi-Fi 凭据。

### Launcher UI

将局部 `title` 替换为持久成员 `s_clock_label`，复用现有起点、小字体和颜色；标签尺寸建议为 `131 × 16`，覆盖 `x=5..135`，与 `x=140` 开始的 Wi-Fi 图标至少间隔 4 px；底边为 `y=18`，留在卡片上方。必须在中文字体及 Montserrat 回退下测量完整字符串的实际宽度与行高，不允许折行、省略号、滚动或裁掉日期。若现有小字体装不下，使用能够完整容纳 16 个字符的紧凑数字字体，并核对已启用字体配置；不得未经确认缩短年份或移除日期。只使用数字、半角连字符、空格与冒号，无需增加中文字库或 i18n 文案。

创建时先设空文本并隐藏，立即读一次快照，随后使用单个 1 秒 LVGL timer 刷新。只有有效性或字符串变化才改文本、显隐，避免每秒重复重绘。标签不加入输入 group，不接焦点动画。App 覆盖 Launcher 时不要把标签挂到 top layer；待机覆盖层继续遮住它，唤醒后最迟下一次 timer 更新。

标签或 timer 分配失败只放弃时钟显示，保留 Launcher 卡片、导航与启动。销毁时先删除 timer，再删除根对象，最后置空标签、timer 与缓存；重复创建／销毁不得重复注册网络回调或累积 timer。Time Service 生命周期独立于 Launcher。

番茄计时、蜂鸣器与空闲待机继续使用单调钟，SNTP 校正不得改变计时剩余时长和待机判定。

## 预计修改范围

| 文件 | 未来实施内容 |
| --- | --- |
| `main/services/xiaomiao_time_service.h`、`.c` | 校时所有权、网络状态处理、线程安全快照与有效期 |
| `main/framework/xiaomiao_launcher.c` | 替换品牌标签、刷新与销毁管理 |
| `main/main.c` | 普通固件服务初始化与主循环 poll，自测分支保持隔离 |
| `main/CMakeLists.txt` | 注册新增 Service 源码 |
| `sdkconfig.defaults`、`sdkconfig.ci` | SNTP 服务器数量与同步间隔 |
| `README.md`、`docs/project-overview.md`、`ROADMAP.md` | 实施后更新真实功能、Service 边界与验证状态 |

设计轮仅交付设计及记录。实施（2026-10-01，`goals/20261001-1657-launcher-network-clock.md`）已按本节范围完成：Time Service、Launcher 标签、启动集成与两份 SNTP 配置落地；`AGENTS.md` 已明确 Time Service 对 SNTP／系统时间的所有权，Wi-Fi Service 头注释中「仅 Wi-Fi Service 调用所有 esp_netif 接口」的过宽表述已修正为通用 `esp_netif_*` 归 Wi-Fi Service、`esp_netif_sntp_*` 专用接口归 Time Service。宿主检查通过；实机验证状态见 Goal 文档。

## 实施检查点与验收

1. Service：初始化失败不阻塞；未同步返回空；合法同步可读；无效输入、非法时间、24 小时阈值、重新同步恢复通过宿主状态检查或最小桩测试。
2. UI：两页一致、完整 16 字符排版、分钟更新、午夜／月末／闰日／年末边界、字体回退、无效隐藏与恢复；创建／销毁顺序、失败释放和 timer 数量经静态检查。
3. 集成：确认所有倒计时仍使用单调钟，Wi-Fi 配网／重连不被校时影响；执行 `git diff --check` 与相关源码检查。
4. 人工构建与实机验收：使用加载 ESP-IDF 6.1 的终端执行 `idf.py build`，或 `idf.py -p COM5 flash monitor`（串口按实际替换）。Agent 不主动编译、烧录或访问串口。

人工验收覆盖：无网开机留空且 Launcher 正常；取得 IP 但无互联网仍留空；联网成功显示北京时间且跨分钟更新；首次失败后网络恢复自动出现；成功后断网继续走时；断电重启重新留空；服务器不可达时不阻塞导航与配网；App 往返、两页导航、待机唤醒、五 App 与光带动画无回归；校时前后番茄剩余时长不跳变。对照手机自动时间，稳定联网时显示年月日与分钟一致，分钟边界允许 1 秒刷新延迟。24 小时过期用可控单调钟桩验证，避免等待一天；服务器故障切换和网络恢复用实网验证。

构建、烧录及核心实机场景已按施工记录中的用户口头确认收口，无串口日志或照片。状态机、24 小时边界与日期转换有宿主检查；英文回退实机演示、备用服务器实网切换、长期内存趋势与显示延迟量化仍未单独取证，不能将源码检查或 lwIP 内建机制记为实网验证。完整证据边界见 Goal 文档。

## 官方依据与记录

已核对 ESP-IDF `release/v6.1` 的 [esp_netif_sntp.h](https://github.com/espressif/esp-idf/blob/release/v6.1/components/esp_netif/include/esp_netif_sntp.h) ，该接口提供延迟启动、同步通知、启动与释放能力。[6.1 系统时间说明](https://docs.espressif.com/projects/esp-idf/en/release-v6.1/esp32s31/api-reference/system/system_time.html) 说明 ESP-NETIF 封装、周期校时和立即／平滑模式；该可访问页面对应 ESP32-S31，仅参考通用 SNTP 接口，不引用其硬件时钟参数。ESP32 同路径页面本次访问失败，实施时以本机 ESP32 目标的 6.1 头文件、Kconfig 和源码复核。

设计施工记录：`goals/20261001-1634-launcher-network-clock-design.md`。

实施施工清单：`goals/20261001-1657-launcher-network-clock.md`。用户已接受每小时校时、短暂离线走时及 24 小时过期隐藏规则；固件已实施并验收，施工清单保留实现与证据记录。
