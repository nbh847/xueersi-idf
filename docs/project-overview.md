# 项目概览与开发导航

## 项目定位

本仓库包含学而思小喵掌机的两套固件与硬件资料：ESP32-WROVER-B 负责 UI、传感器和主要外设；GD32F350G8 负责 USB 串口桥及 ESP32 自动下载控制。目标硬件、引脚和底层协议以根目录 `README.md` 与 `xueersi-xiaomiao-schematic.pdf` 为准。

## 当前实现

ESP32 工程基于 ESP-IDF 6.1 和 LVGL 9.5。15 页 Dashboard 业务代码仍位于 `main/main.c`；App Framework 运行时（App 描述、Registry、Manager、Navigation、Launcher、全局 Wi-Fi 图标）位于 `main/framework/`；业务 App 位于 `main/apps/`，当前有占位 `Games`（`main/apps/games/`）、`PC Monitor`（`main/apps/pc_monitor/`，节点 11 起显示 Agent Service 快照的真实 PC 指标）、菜单 `Tools`（`main/apps/tools/`）与菜单 `Settings`（`main/apps/settings/`）；System Service 位于 `main/services/`，当前有 `Settings Service`（`main/services/xiaomiao_settings_service.{h,c}`，负责默认值、校验、NVS 读写与安全回退）、`Wi-Fi Service`（`main/services/xiaomiao_wifi_service.{h,c}`，连同私有实现 `xiaomiao_wifi_provisioning.c`、`xiaomiao_wifi_dns.{h,c}` 与 `xiaomiao_wifi_internal.h`）和 `Agent Service`（`main/services/xiaomiao_agent_service.{h,c}`，节点 11，连同电脑端 `pc-agent/` Python 后端）。普通固件已接入框架：开机进入 Launcher，按注册顺序显示 `Games`、`PC Monitor`、`Tools`、`Settings` 与 `Hardware Test` 五个入口。当前启动链为：

```text
app_main
  -> Settings Service 初始化（失败只记警告，继续启动）
  -> Wi-Fi Service 初始化（失败只记警告，继续离线启动；不等待扫描、关联或 DHCP）
  -> Agent Service 初始化（地址为空则保持 UNCONFIGURED；失败只记警告，不等待 Wi-Fi、HTTP 或首次数据）
  -> 按键、LCD、ADC、I2C、LEDC、SD 初始化
  -> LVGL 显示与 keypad 输入注册
  -> 全局 Wi-Fi 状态图标创建（LVGL top layer，右上角）
  -> lvgl FreeRTOS 任务
  -> Registry 按 Games、PC Monitor、Tools、Settings、Hardware Test 顺序注册
  -> App Manager init_all
  -> Launcher（左右键在行内换列并在外侧列翻页，翻页优先同一行、缺行时回退到目标页第一格；上下键在页内换行。按 A 打开焦点 App，Games／PC Monitor 短按 B 返回、Tools／Settings 两级 B、Hardware Test 长按 B 800 ms 返回）
```

Dashboard 页面依次覆盖光照、热敏、MPU6050、两路 LED、蜂鸣器、两路电机、MicroSD、GPIO25/26 PWM、GPIO32/33 ADC、系统状态和 About。左右键切页，上下键调节当前值，A 执行动作，B 短按停止或取消、长按 800 ms 返回 Launcher。硬件状态集中在 `board_state_t`，UI 引用集中在 `ui_state_t`。

`Games` 是占位 App：不获取 LVGL 输入焦点、不注册按键、不访问硬件，页面上只有 `Games`／`Coming soon`／`B Back` 三段文本，返回由 Launcher 现有的 App 打开态 B 转发分支完成。它同时也是首个独立业务目录的范例，后续业务 App 按同样结构新增。

`PC Monitor` 是同结构的第二个业务 App（`main/apps/pc_monitor/`），节点 11 起显示真实 PC 指标：CPU／RAM 与 GPU／TEMP 分为两页，每页保留两项数值和 60 点滚动图，左右键切页，B 返回。App 保存四项名称与值标签、图表与序列引用，打开时创建 20 ms LVGL timer（每秒读取一次）在 UI 线程读取 `xiaomiao_agent_get_snapshot()`、格式化标签并推进图表，关闭时按“删 timer → 移出 group → 清标签／图表引用 → 清容器引用”顺序释放；温度行按 GPU → CPU → 无的优先级显示并把名称同步为 `GPU T`／`CPU T`／`TEMP`。3 秒无有效快照时有效位由 Service 清除，数值与图表均不再显示旧数据，页面恢复 `-- %`／`-- °C` 占位。数值格式化不依赖 printf `%f`。App 不访问硬件、不创建网络 Task 或 mutex，也不 include `esp_http_client.h`、`esp_wifi.h`、`cJSON.h` 或 NVS 头。

`Tools` 是第三个业务 App（`main/apps/tools/`），也是首个带 App 内菜单和两级返回的独立 App：菜单为 `Wi-Fi`、`System Info`、`About`，上下键在 0～2 之间移动并在边界停止，A 进入详情页。`System Info` 每次进入时用只读 API（`esp_chip_info()`、`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`、`esp_flash_get_size()`、`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`、`esp_get_idf_version()`、`esp_app_get_description()`）采集一次芯片／核心数、CPU、Flash、PSRAM、ESP-IDF 与固件版本，读取失败显示 `Unknown`，PSRAM 缺失显示 `None`，无周期刷新；`Wi-Fi` 页读取 Wi-Fi Service 的真实快照，显示 Status、SSID、Signal（`dBm / Strong|Good|Fair|Weak`）与 IPv4；进入视图时读取一次，打开期间由专用 LVGL timer 每 1 秒刷新，离开视图与关闭 App 时删除该 timer，未连接时不显示上一次的 IP 与 RSSI。Tools 不直接调用任何 `esp_wifi_*`，也不读取或显示密码；`About` 显示项目、实际固件版本、作者与仓库标识。输入焦点由 Tools 自己的根对象通过 `lv_group_get_default()` 取得，B 使用按下锁存加一个 20 ms LVGL timer 的释放检测，因此详情页按住 B 不会连续退出两层；菜单页的关闭动作在该 timer 中执行，避免在焦点对象自己的按键回调里删除它。Tools 不访问 GPIO／SPI／I2C／ADC／LEDC，不创建 FreeRTOS Task、队列或锁，`close` 中按“删 timer → 移出 group → 清空引用”的顺序释放，内容根仍由 Navigation 在回调返回后删除。

`Settings` 是第四个业务 App（`main/apps/settings/`），也是首个让 Launcher 分页的入口：注册五个 App 后焦点索引固定为 Games 0、PC Monitor 1、Tools 2、Settings 3、Hardware Test 4，第 1 页仍是 2 列 × 2 行满页，Hardware Test 落到第 2 页。它交付设置界面：菜单为 `Wi-Fi`、`Display`、`Sound`、`System`，上下键在 0～3 之间移动并在边界停止，A 进入详情页。`Wi-Fi` 是唯一的可操作页面：Status 行显示 Wi-Fi Service 的真实状态，`Auto connect` 先经 Settings Service 持久化再通知 Wi-Fi Service 应用（保存失败则显示错误且不改 UI 值），`Configure` 启动配网并进入配网状态页（显示热点 SSID、本次会话的 8 位临时密码、`http://192.168.4.1` 与试连结果，B 取消配网并回到 Wi-Fi 页），`Forget network` 需二次确认。配网页每秒刷新一次，刷新复用 App 现有的 20 ms B 释放检测 timer，不新增计时器；会话在别处结束（成功或超时）时页面自动回到 Wi-Fi 列表。其余三页仍为只读——`Sound` 显示 `Unavailable` 并标注实现节点（节点 12），因为对应偏好要等节点 12 消费后才会生效；`Display` 陈述固定背光的硬件事实（`Brightness fixed`／`Backlight tied to VCC`），因为背光直连 VCC，不存在可保存的亮度；`System` 是唯一读取 Service 的页面，每次进入通过 `xiaomiao_settings_service_get()` 读取一次配置、通过 `xiaomiao_settings_source()` 与 `xiaomiao_settings_last_error()` 读取来源与错误摘要，显示为 `Loaded from NVS`／`Defaults applied`／`Defaults restored`／`Not persisted` 与 `NVS error 0x…`，不创建 timer、不提供重置动作。Settings 只调用 Settings Service 的公开接口，不 include `nvs.h`／`nvs_flash.h`。输入焦点与 B 语义实现方式与 Tools 相同：焦点由自己的根对象经 `lv_group_get_default()` 取得，B 使用按下锁存加 20 ms LVGL timer 释放检测，因此详情页按住 B 不会连续退出两层；`close` 按“删 timer → 移出 group → 清空引用”释放，内容根仍由 Navigation 删除。Settings 不访问 GPIO／SPI／I2C／ADC／LEDC／NVS，不创建 FreeRTOS Task、队列或锁，也不调用任何 `esp_wifi_*`。

`Settings Service`（`main/services/xiaomiao_settings_service.{h,c}`）是首个 System Service，只做被调用式的同步能力，不创建 Task、队列、timer，也不依赖 LVGL、Launcher、Navigation 或任何具体 App。公开结构只含业务字段 `wifi_auto_connect` 与 `sound_enabled`（首版默认均为 `true`），运行时配置以内存快照保存，读取复制整份快照、不暴露内部指针。持久化用固定宽度版本化 blob 写在默认 `nvs` 分区的 `xiaomiao/settings`：`schema_version`（当前 1）、`payload_size`（2）、两个显式归一化为 0／1 的布尔字节与 2 字节清零保留位，公开结构不按内存布局直接序列化，写入前先校验、`nvs_commit()` 成功后才替换内存快照。缺失 key 时装载并尝试写入默认值；长度、版本或字段越界时只重写 `xiaomiao/settings`、不动 namespace 内其他 key；`nvs_flash_init()`／`nvs_open()`／读取失败时保留内存默认值并把来源标记为降级，`ESP_ERR_NVS_NO_FREE_PAGES` 与 `ESP_ERR_NVS_NEW_VERSION_FOUND` 按原错误码记录，源码中不存在 `nvs_flash_erase()`，也不引入自定义分区表。`init()` 幂等，重复调用返回首次结果且不重写 Flash。启动链中它最早执行，失败只记一条警告并继续启动。

`Wi-Fi Service`（`main/services/xiaomiao_wifi_service.{h,c}`）是第二个 System Service，拥有全部网络生命周期：STA 启动与扫描、异步连接、1／2／5／10／30 秒退避重连、断开与忘记网络、SoftAP 配网会话，以及每 5 秒一次的 RSSI 采样（读取关联 AP，不用扫描）。它不依赖 LVGL、Navigation、Launcher 或具体 App；App 与 Framework 只通过公开接口读快照与下命令，`main/` 下除本 Service 的私有实现外没有 `esp_wifi_*`／`esp_netif_*`／HTTP server 调用。状态由 Wi-Fi／IP 事件驱动，事件回调只更新受互斥锁保护的状态、安排重连或提交轻量事件，从不调用 LVGL；`get_snapshot()` 复制一份自洽视图，未连接时清空旧 IP 与旧 RSSI，快照中不含密码、临时热点密码或表单内容。初始化只做栈与驱动启动并触发一次异步连接尝试，不等待扫描、关联或 DHCP，任何失败都只记录警告并让启动继续到 Launcher。

`Agent Service`（`main/services/xiaomiao_agent_service.{h,c}`）是第三个 System Service，也是节点 11 的固件侧核心：它拥有到统一 PC Agent（仓库 `pc-agent/`，Python，默认端口 8766）的全部 HTTP 链路——固定地址配置（`CONFIG_XIAOMIAO_AGENT_HOST` 仅接受 IPv4 文本，可提交配置保持为空、UNCONFIGURED 状态不发请求）、后台 Worker（先读 Wi-Fi Service 公开快照判断在线，成功后 1 秒轮询、失败至少等 2 秒）、流式限长响应读取（2048 字节上限，超限整包拒绝）、API v1 JSON 校验（schema v1、`ok`/`degraded` 且核心字段有限数值才提交；`unavailable`/`stale` 不更新成功时间；可选 GPU／温度字段独立降级）与线程安全快照（每指标独立有效位，3 秒无新有效快照自动失效；锁内只提交与复制，网络 I/O、JSON 解析与日志均在锁外）。地址为空或 init 失败都不阻塞启动。

凭据存储：Wi-Fi 驱动全程使用 `WIFI_STORAGE_RAM`，SSID 与密码由 Service 自己写入默认 `nvs` 分区的 `xiaomiao/wifi_creds` key（固定宽度版本化 blob，schema v1，104 字节）。原因是 ESP-IDF 6.1 在 `WIFI_STORAGE_FLASH` 下会由 `esp_wifi_set_config()` 立即把 STA 配置写进 `nvs.net80211`，而 `esp_wifi_set_storage()` 没有运行态切换保证，两者都会破坏“取得 IPv4 后才提交、失败保留旧凭据”的要求。配网页提交的凭据只进 RAM，只有收到 `IP_EVENT_STA_GOT_IP` 后才写入 Flash；写入失败时界面显示“connected but not saved”，重启仍回到旧网络。`Forget network` 只调用 `nvs_erase_key()` 删除该 key，源码中没有 `nvs_flash_erase()`，也不修改分区表。

配网实现（`main/services/xiaomiao_wifi_provisioning.c`、`xiaomiao_wifi_dns.{h,c}`）在会期切到 `WIFI_MODE_APSTA`，开启 `Xiaomiao-XXXX` 单客户端 WPA2 热点（后缀取 AP MAC 末两字节，密码每次会话由硬件随机源生成 8 位数字），启动最小 DNS 响应器把所有 A 查询指向热点地址，再由 HTTP server 提供 `/`（页面）、`/api/scan`、`/api/connect`、`/api/status`，未知路径 302 跳回页面以配合手机的 captive portal 探测。表单体上限 512 字节，超长字段与非法转义一律拒绝而不截断，试连期间拒绝并发提交。会话在成功、用户取消、上报失败或 10 分钟空闲超时后结束，资源按“HTTP → DNS → AP netif → Wi-Fi 模式”逆序释放；离开 APSTA 会重启 Wi-Fi 驱动，因此收尾时会显式把已保存网络重新写回驱动并重连。

`Wi-Fi Indicator`（`main/framework/xiaomiao_wifi_indicator.{h,c}`）挂在 LVGL top layer 右上角，不属于任何 App 内容根、不加入输入 group、清除 `CLICKABLE`，由一个 200 ms 的 LVGL timer 在 UI 线程读取快照后更新。已连接时按 RSSI 显示档位与颜色（≥ -55 与 -56～-67 为绿色、-68～-75 为黄色、< -75 为橙红），连接中蓝色逐格点亮；未点亮的格用接近背景的暗色填充（`#2A2F3A`）表达"空槽"，而不是靠描边，因此单格只需 2 px、整体保持 18 × 12 @ (140, 2)；未连接为暗槽加浅灰 `x`，已关闭为暗槽加深灰 `x`，失败为暗槽加红色 `!`（约 3 秒）。形状与颜色同时表达状态，并带 40% 黑色衬底以便在 Hardware Test 的黄色页面上也能看清；Hardware Test 的页码宽度相应由 150 改为 128 以让出右上角。

显示屏为 160 × 128 ST7735 兼容面板，使用 SPI2、RGB565 交换字节格式与 2 个全屏 DMA 缓冲（每块 40 KB，只能来自内部 RAM；原设计为三块，加入 Wi-Fi 后第三块无法分配，2026-09-21 确认采用双缓冲，代码保留“尝试第三块、失败降级”的自适应写法，详见 `goals/20260920-2210-wifi-service.md`）；LVGL tick 为 1 ms，UI 目标刷新周期为 16 ms。TFT 与 MicroSD 共用 SPI2，通过独立 CS 分时访问。背光引脚直连 VCC，无法调节亮度，因此 Settings 的 Display 页只陈述该硬件事实。

## 两颗 MCU 的边界

- ESP32 端已按 I2C 地址 `0x40` 实现 GD32 探测、LED 寄存器和双电机 PWM 命令，也按 `0x68` 接入 MPU6050。
- `GD32_firmware/Project/src/app.c` 当前实现 USB CDC 与 USART1 双向环形缓冲，并用 DTR/RTS 控制 ESP32 IO0/EN。
- 仓库内 GD32 源码尚未实现 I2C 从机、LED 和电机协议；README 中的 `0x40` 协议是 ESP32 端沿用的硬件协议资料，联调前必须确认 GD32 实际固件版本。

## 构建与配置

ESP32 目标由根目录 `CMakeLists.txt` 定义，组件依赖见 `main/idf_component.yml` 和 `main/CMakeLists.txt`，分区布局由根目录 `partitions.csv` 定义（`nvs` 24 KB、`phy_init` 4 KB、`factory` app 2 MB、`assets` data/spiffs 1.5 MB，末尾约 384 KB 未分配；背景见 `goals/20260920-2210-wifi-service.md`）。`main/Kconfig.projbuild` 提供 Agent Service 的本地构建配置：`CONFIG_XIAOMIAO_AGENT_HOST`（默认空 = 未配置）与 `CONFIG_XIAOMIAO_AGENT_PORT`（默认 8766）；真实局域网 IPv4 只写入被忽略的本地 `sdkconfig`，可提交配置保持为空（见 `goals/20260921-1238-pc-monitor-communication.md`）。`main` 组件把自身目录加入 include 路径（`INCLUDE_DIRS "."`），因此该组件下任意源文件都可按 `framework/...` 或 `apps/...` 引用同组件头文件，新增业务 App 不需要各自维护相对路径。关键默认值位于 `sdkconfig.defaults`：240 MHz CPU、80 MHz QIO Flash、4 MB Flash、80 MHz Quad PSRAM、FreeRTOS 1000 Hz 和 LVGL RGB565。常用流程：

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

环境加载使用官方 `export.bat` / `export.ps1`（需在 CMD/PowerShell 中执行，MSYS shell 会因 `MSYSTEM` 被拒绝）。当安装布局非默认（Python venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下）时，用进程级 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 指向实际位置，不改系统环境；确认版本用 `python "$env:IDF_PATH\tools\idf.py" --version`（PATH 中的 `idf.py.exe` 包装器会显示自身版本）。不依赖现有 `build/` 缓存的可复现基线构建命令见 `AGENTS.md`；2026-09-19 已在隔离构建目录完成全新配置构建与二次构建验证，记录见 `goals/20260919-1834-build-baseline.md`。

GD32 使用 Keil 工程 `GD32_firmware/Project/MDK-ARM/cdc_acm.uvprojx`，目标器件为 GD32F350G8，依赖 GigaDevice DFP 3.4.0。`go.py` 仅用于原厂 MicroPython 固件环境下的硬件探查，不参与 ESP-IDF 构建。

## 目标架构与演进约束

`xiaomiao_firmware_v0.1_design.md` 规划将 Dashboard 封装为 Hardware Test App，并逐步引入 BSP、Service、App Framework 和 Launcher。当前 `main/framework/` 下已实现 App 描述、Registry、Manager、Navigation、Launcher 与全局 Wi-Fi 图标；`main/apps/` 存放占位 `Games`、接入 Agent 快照的 `PC Monitor`、菜单 `Tools` 与菜单 `Settings`；`main/services/` 已实现 `Settings Service`（配置模型、NVS schema v1、安全回退与状态查询）、`Wi-Fi Service`（STA、扫描、自动连接、断线重连、网页配网与凭据持久化）与 `Agent Service`（固定地址、HTTP 拉取、JSON 校验与 PC 指标快照，电脑端为 `pc-agent/`）。普通固件已把 15 页 Dashboard 注册为 `Hardware Test` App，并默认启动包含五个入口的 Launcher。BSP 分层仍未实现；`wifi_auto_connect` 已由 Wi-Fi Service 消费并产生真实效果，`sound_enabled` 仍要等节点 12 才会生效。实施继续遵守小步迁移：保留 15 页硬件测试；SD 或网络缺失不得阻塞启动；业务 App 不直接操作 GPIO、SPI、I2C、NVS 或 `esp_wifi_*`，也不直接依赖 Launcher。

## 验证入口与已知缺口

固件当前没有自动化测试套件；最低验证为 `idf.py build`，硬件改动还需烧录实机，检查启动日志、六个按键、屏幕刷新及受影响外设。PC Agent 另有独立的 Python 语法检查和单元测试，命令与结果记录在节点 11 Goal 中。`main/framework/` 的 Framework、Navigation、Launcher 分别提供 `XIAOMIAO_FRAMEWORK_SELF_TEST`、`XIAOMIAO_NAVIGATION_SELF_TEST`、`XIAOMIAO_LAUNCHER_SELF_TEST` 三个默认关闭的自测构建选项，编译、烧录与按键验证由人工执行。Hardware Test App、`Games` 与 `PC Monitor` 占位／骨架 App 的可执行人工回归均已完成（`Games` 含双入口顺序、短按 B 返回、10 轮生命周期、双 App 焦点保持与 `Hardware Test` 15 页回归；`PC Monitor` 含三入口顺序、短按 B 返回、8 轮生命周期、`screen children` 恒为 2 与两种 B 语义并存，轮次口径差异已由人工确认接受，均见 `goals/ROADMAP-history.md`）。`Tools` 的可执行人工回归已于 2026-09-20 完成（四入口顺序与满页布局、三项菜单与三个详情页、两级 B 与长按隔离、6 次 open／close 严格配对、`screen children` 恒为 2、Hardware Test 焦点索引 3、三套 B 语义并存；Tools 往返数 3 < 条款要求的 10，轮次口径差异已由人工确认接受，见 `goals/ROADMAP-history.md`）。其 System Info 等视图内容为人工目视确认：视图切换使用 `ESP_LOGD`，默认日志级别不输出。`Settings` 的可执行人工回归已于 2026-09-20 完成（五入口注册、第 1 页四格与第 2 页单入口、页码 2/2 由 `launcher focus=4 page=1` 数值证明、12 次 open／close 严格配对、`screen children` 恒为 2、Settings 累计 10 次往返且 6 s 内连续 5 轮、三套 B 语义并存；首轮 Monitor 日志未覆盖 Games 与 PC Monitor 的进入／返回，2026-09-20 14:08 已由人工补充确认全部手动测试通过，但未提供这两项的补充串口日志，见 `goals/ROADMAP-history.md`）。其四项菜单与四个状态页的内容与布局为人工目视确认：视图切换使用 `ESP_LOGD`，默认日志级别不输出，Settings 按设计不打印 Launcher 焦点索引。

`Settings Service` 提供 `XIAOMIAO_SETTINGS_SERVICE_SELF_TEST` 自测构建选项（默认关闭，与前述三个选项互斥）。该自测不需要 LCD 或 LVGL，通过 `main/services/xiaomiao_settings_service_selftest.c` 分 6 步覆盖缺失 key 安装默认值、非默认值写入与重启恢复、未知 schema 版本、越界布尔值、短 blob 与长 blob 的恢复路径，步骤之间用 `esp_restart()` 自动重启并靠 `xiaomiao/selftest_stage` 键推进，最后恢复干净状态并输出 `SETTINGS_SERVICE_SELF_TEST: PASS`。节点 9 已于 2026-09-20 完成人工验证：普通构建（`App version: 7088147-dirty`）与烧录通过，首启为 `source=defaults`、断电重启后为 `source=nvs`，两次均到达 `Launcher ready, 5 app(s) registered`；6 步自测固件输出 `PASS`；Settings 四页与五 App 回归由人工确认通过。唯一未验证项是 `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败两条路径——不引入自定义分区表就无法构造，只按源码检查确认，已在 `goals/20260920-1429-settings-service-nvs.md` 中记录。当前待解决事项以 `ROADMAP.md` 为准，主要包括 GD32 `0x40` 协议源码缺失、相应 LED／电机／MPU6050 实机行为尚未验证，以及已复现但未定位根因的 MicroSD／GPIO22 冲突。

节点 10“Wi-Fi Service”已于 2026-09-21 完成并由项目负责人确认人工构建、烧录与手动测试全部通过。逐项证据覆盖启动不阻塞、扫描、SoftAP + DNS + HTTP 网页配网、取得 IPv4 后再提交凭据、错误凭据保护、换网与跨断电持久化、自动连接、退避重连、10 分钟超时、Forget network、Settings／Tools 页面、全局图标和既有 App 回归；新增确认未附日志的项目在 Goal 中明确标记为人工确认。凭据由 Service 以版本化 blob 保存到 `xiaomiao/wifi_creds`，Wi-Fi 驱动全程使用 `WIFI_STORAGE_RAM`。加入 Wi-Fi 后项目改用自定义 `partitions.csv`：NVS 与 `phy_init` 的大小和偏移保持不变，factory app 扩到 2 MB，并预留 1.5 MB `assets` 分区供节点 15 使用。实现、验证证据、已接受取舍与仅静态复核的故障路径见 `goals/20260920-2210-wifi-service.md`。

节点 11“PC Monitor 通信”已于 2026-09-21 完成。统一 PC Agent、固件 Agent Service、固定 IPv4 HTTP API 与 PC Monitor 实时指标刷新已实现；PC Agent 的 Python 测试 18/18 通过，固件静态复核通过，项目负责人确认 ESP-IDF 构建、烧录及 CP5 手动场景全部通过。该确认未附新增串口日志、截图或资源数值；不可构造的底层分配／网络栈故障仍按源码检查记录为未验证。实现、验证口径与后续 AI 额度路由边界见 `goals/20260921-1238-pc-monitor-communication.md`。
