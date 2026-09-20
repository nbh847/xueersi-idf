# 项目概览与开发导航

## 项目定位

本仓库包含学而思小喵掌机的两套固件与硬件资料：ESP32-WROVER-B 负责 UI、传感器和主要外设；GD32F350G8 负责 USB 串口桥及 ESP32 自动下载控制。目标硬件、引脚和底层协议以根目录 `README.md` 与 `xueersi-xiaomiao-schematic.pdf` 为准。

## 当前实现

ESP32 工程基于 ESP-IDF 6.1 和 LVGL 9.5。15 页 Dashboard 业务代码仍位于 `main/main.c`；App Framework 运行时（App 描述、Registry、Manager、Navigation、Launcher）位于 `main/framework/`；业务 App 位于 `main/apps/`，当前有占位 `Games`（`main/apps/games/`）、静态骨架 `PC Monitor`（`main/apps/pc_monitor/`）、菜单 `Tools`（`main/apps/tools/`）与菜单 `Settings`（`main/apps/settings/`）；首个 System Service 位于 `main/services/`，当前只有 `Settings Service`（`main/services/xiaomiao_settings_service.{h,c}`），负责默认值、校验、NVS 读写与安全回退。普通固件已接入框架：开机进入 Launcher，按注册顺序显示 `Games`、`PC Monitor`、`Tools`、`Settings` 与 `Hardware Test` 五个入口。当前启动链为：

```text
app_main
  -> Settings Service 初始化（失败只记警告，继续启动）
  -> 按键、LCD、ADC、I2C、LEDC、SD 初始化
  -> LVGL 显示与 keypad 输入注册
  -> lvgl FreeRTOS 任务
  -> Registry 按 Games、PC Monitor、Tools、Settings、Hardware Test 顺序注册
  -> App Manager init_all
  -> Launcher（左右键按注册顺序移动焦点，越过本页第 4 格即翻页；按 A 打开焦点 App，Games／PC Monitor 短按 B 返回、Tools／Settings 两级 B、Hardware Test 长按 B 800 ms 返回）
```

Dashboard 页面依次覆盖光照、热敏、MPU6050、两路 LED、蜂鸣器、两路电机、MicroSD、GPIO25/26 PWM、GPIO32/33 ADC、系统状态和 About。左右键切页，上下键调节当前值，A 执行动作，B 短按停止或取消、长按 800 ms 返回 Launcher。硬件状态集中在 `board_state_t`，UI 引用集中在 `ui_state_t`。

`Games` 是占位 App：不获取 LVGL 输入焦点、不注册按键、不访问硬件，页面上只有 `Games`／`Coming soon`／`B Back` 三段文本，返回由 Launcher 现有的 App 打开态 B 转发分支完成。它同时也是首个独立业务目录的范例，后续业务 App 按同样结构新增。

`PC Monitor` 是同结构的第二个业务 App（`main/apps/pc_monitor/`），只交付静态 UI 骨架：标题、CPU／RAM／GPU／TEMP 四行指标与页脚返回提示，四项值均为编译期占位 `-- %`／`-- %`／`-- %`／`-- °C`，名称列与值列用独立标签加固定列宽对齐。同样不取得输入焦点、不访问硬件、不创建 Task 或 timer，也不接数据源——真实 PC 指标通信按设计文档第 15 节安排在节点 11。

`Tools` 是第三个业务 App（`main/apps/tools/`），也是首个带 App 内菜单和两级返回的独立 App：菜单为 `Wi-Fi`、`System Info`、`About`，上下键在 0～2 之间移动并在边界停止，A 进入详情页。`System Info` 每次进入时用只读 API（`esp_chip_info()`、`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`、`esp_flash_get_size()`、`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`、`esp_get_idf_version()`、`esp_app_get_description()`）采集一次芯片／核心数、CPU、Flash、PSRAM、ESP-IDF 与固件版本，读取失败显示 `Unknown`，PSRAM 缺失显示 `None`，无周期刷新；`Wi-Fi` 页如实显示 Service 在节点 10 前不可用，不初始化或调用任何 `esp_wifi_*`／网络能力；`About` 显示项目、实际固件版本、作者与仓库标识。输入焦点由 Tools 自己的根对象通过 `lv_group_get_default()` 取得，B 使用按下锁存加一个 20 ms LVGL timer 的释放检测，因此详情页按住 B 不会连续退出两层；菜单页的关闭动作在该 timer 中执行，避免在焦点对象自己的按键回调里删除它。Tools 不访问 GPIO／SPI／I2C／ADC／LEDC，不创建 FreeRTOS Task、队列或锁，`close` 中按“删 timer → 移出 group → 清空引用”的顺序释放，内容根仍由 Navigation 在回调返回后删除。

`Settings` 是第四个业务 App（`main/apps/settings/`），也是首个让 Launcher 分页的入口：注册五个 App 后焦点索引固定为 Games 0、PC Monitor 1、Tools 2、Settings 3、Hardware Test 4，第 1 页仍是 2 列 × 2 行满页，Hardware Test 落到第 2 页。它交付设置界面：菜单为 `Wi-Fi`、`Display`、`Sound`、`System`，上下键在 0～3 之间移动并在边界停止，A 进入详情页。四个详情页均为只读且不提供任何开关——`Wi-Fi` 与 `Sound` 显示 `Unavailable` 并标注实现节点（节点 10、12），因为对应偏好要等节点 10、12 消费后才会生效；`Display` 陈述固定背光的硬件事实（`Brightness fixed`／`Backlight tied to VCC`），因为背光直连 VCC，不存在可保存的亮度；`System` 是唯一读取 Service 的页面，每次进入通过 `xiaomiao_settings_service_get()` 读取一次配置、通过 `xiaomiao_settings_source()` 与 `xiaomiao_settings_last_error()` 读取来源与错误摘要，显示为 `Loaded from NVS`／`Defaults applied`／`Defaults restored`／`Not persisted` 与 `NVS error 0x…`，不创建 timer、不提供重置动作。Settings 只调用 Settings Service 的公开接口，不 include `nvs.h`／`nvs_flash.h`。输入焦点与 B 语义实现方式与 Tools 相同：焦点由自己的根对象经 `lv_group_get_default()` 取得，B 使用按下锁存加 20 ms LVGL timer 释放检测，因此详情页按住 B 不会连续退出两层；`close` 按“删 timer → 移出 group → 清空引用”释放，内容根仍由 Navigation 删除。Settings 不访问 GPIO／SPI／I2C／ADC／LEDC／NVS，不创建 FreeRTOS Task、队列或锁，也不调用任何 `esp_wifi_*`。

`Settings Service`（`main/services/xiaomiao_settings_service.{h,c}`）是首个 System Service，只做被调用式的同步能力，不创建 Task、队列、timer，也不依赖 LVGL、Launcher、Navigation 或任何具体 App。公开结构只含业务字段 `wifi_auto_connect` 与 `sound_enabled`（首版默认均为 `true`），运行时配置以内存快照保存，读取复制整份快照、不暴露内部指针。持久化用固定宽度版本化 blob 写在默认 `nvs` 分区的 `xiaomiao/settings`：`schema_version`（当前 1）、`payload_size`（2）、两个显式归一化为 0／1 的布尔字节与 2 字节清零保留位，公开结构不按内存布局直接序列化，写入前先校验、`nvs_commit()` 成功后才替换内存快照。缺失 key 时装载并尝试写入默认值；长度、版本或字段越界时只重写 `xiaomiao/settings`、不动 namespace 内其他 key；`nvs_flash_init()`／`nvs_open()`／读取失败时保留内存默认值并把来源标记为降级，`ESP_ERR_NVS_NO_FREE_PAGES` 与 `ESP_ERR_NVS_NEW_VERSION_FOUND` 按原错误码记录，源码中不存在 `nvs_flash_erase()`，也不引入自定义分区表。`init()` 幂等，重复调用返回首次结果且不重写 Flash。启动链中它最早执行，失败只记一条警告并继续启动。

显示屏为 160 × 128 ST7735 兼容面板，使用 SPI2、RGB565 交换字节格式与 3 个全屏 DMA 缓冲；LVGL tick 为 1 ms，UI 目标刷新周期为 16 ms。TFT 与 MicroSD 共用 SPI2，通过独立 CS 分时访问。背光引脚直连 VCC，无法调节亮度，因此 Settings 的 Display 页只陈述该硬件事实。

## 两颗 MCU 的边界

- ESP32 端已按 I2C 地址 `0x40` 实现 GD32 探测、LED 寄存器和双电机 PWM 命令，也按 `0x68` 接入 MPU6050。
- `GD32_firmware/Project/src/app.c` 当前实现 USB CDC 与 USART1 双向环形缓冲，并用 DTR/RTS 控制 ESP32 IO0/EN。
- 仓库内 GD32 源码尚未实现 I2C 从机、LED 和电机协议；README 中的 `0x40` 协议是 ESP32 端沿用的硬件协议资料，联调前必须确认 GD32 实际固件版本。

## 构建与配置

ESP32 目标由根目录 `CMakeLists.txt` 定义，组件依赖见 `main/idf_component.yml` 和 `main/CMakeLists.txt`。`main` 组件把自身目录加入 include 路径（`INCLUDE_DIRS "."`），因此该组件下任意源文件都可按 `framework/...` 或 `apps/...` 引用同组件头文件，新增业务 App 不需要各自维护相对路径。关键默认值位于 `sdkconfig.defaults`：240 MHz CPU、80 MHz QIO Flash、4 MB Flash、80 MHz Quad PSRAM、FreeRTOS 1000 Hz 和 LVGL RGB565。常用流程：

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

环境加载使用官方 `export.bat` / `export.ps1`（需在 CMD/PowerShell 中执行，MSYS shell 会因 `MSYSTEM` 被拒绝）。当安装布局非默认（Python venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下）时，用进程级 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 指向实际位置，不改系统环境；确认版本用 `python "$env:IDF_PATH\tools\idf.py" --version`（PATH 中的 `idf.py.exe` 包装器会显示自身版本）。不依赖现有 `build/` 缓存的可复现基线构建命令见 `AGENTS.md`；2026-09-19 已在隔离构建目录完成全新配置构建与二次构建验证，记录见 `goals/20260919-1834-build-baseline.md`。

GD32 使用 Keil 工程 `GD32_firmware/Project/MDK-ARM/cdc_acm.uvprojx`，目标器件为 GD32F350G8，依赖 GigaDevice DFP 3.4.0。`go.py` 仅用于原厂 MicroPython 固件环境下的硬件探查，不参与 ESP-IDF 构建。

## 目标架构与演进约束

`xiaomiao_firmware_v0.1_design.md` 规划将 Dashboard 封装为 Hardware Test App，并逐步引入 BSP、Service、App Framework 和 Launcher。当前 `main/framework/` 下已实现 App 描述、Registry、Manager、Navigation（统一打开／返回、App 内容根对象所有权）与 Launcher（2 列 × 2 行动态入口、左右键按注册顺序移动焦点并翻页、经 Navigation 进入／返回）；`main/apps/` 存放业务 App，当前有占位 `Games`、静态骨架 `PC Monitor`、菜单 `Tools` 与菜单 `Settings`；`main/services/` 已建立 Service 层并实现首个 System Service `Settings Service`（配置模型、NVS schema v1、安全回退与状态查询，App 不直接访问 NVS），后续 Wi-Fi、Audio、Storage 等 Service 按设计文档第 7、15 节依次补入；普通固件已把 15 页 Dashboard 注册为 `Hardware Test` App、把 `Games`、`PC Monitor`、`Tools` 与 `Settings` 排在它之前，并默认启动 Launcher（五个入口，第 2 页只有一个）。BSP 分层仍未实现，Settings 的 Wi-Fi／Sound 偏好要等节点 10、12 消费后才会产生业务效果。实施时以小步迁移为原则：先建立可验证边界，再移动功能；保留 15 页硬件测试；SD 缺失不得阻塞启动；业务 App 不直接操作 GPIO、SPI 或 I2C，也不直接依赖 Launcher。

## 验证入口与已知缺口

仓库目前没有自动化测试。最低验证为 `idf.py build`；硬件改动还需烧录实机，检查启动日志、六个按键、屏幕刷新及受影响外设。`main/framework/` 的 Framework、Navigation、Launcher 分别提供 `XIAOMIAO_FRAMEWORK_SELF_TEST`、`XIAOMIAO_NAVIGATION_SELF_TEST`、`XIAOMIAO_LAUNCHER_SELF_TEST` 三个默认关闭的自测构建选项，编译、烧录与按键验证由人工执行。Hardware Test App、`Games` 与 `PC Monitor` 占位／骨架 App 的可执行人工回归均已完成（`Games` 含双入口顺序、短按 B 返回、10 轮生命周期、双 App 焦点保持与 `Hardware Test` 15 页回归；`PC Monitor` 含三入口顺序、短按 B 返回、8 轮生命周期、`screen children` 恒为 2 与两种 B 语义并存，轮次口径差异已由人工确认接受，均见 `goals/ROADMAP-history.md`）。`Tools` 的可执行人工回归已于 2026-09-20 完成（四入口顺序与满页布局、三项菜单与三个详情页、两级 B 与长按隔离、6 次 open／close 严格配对、`screen children` 恒为 2、Hardware Test 焦点索引 3、三套 B 语义并存；Tools 往返数 3 < 条款要求的 10，轮次口径差异已由人工确认接受，见 `goals/ROADMAP-history.md`）。其 System Info 等视图内容为人工目视确认：视图切换使用 `ESP_LOGD`，默认日志级别不输出。`Settings` 的可执行人工回归已于 2026-09-20 完成（五入口注册、第 1 页四格与第 2 页单入口、页码 2/2 由 `launcher focus=4 page=1` 数值证明、12 次 open／close 严格配对、`screen children` 恒为 2、Settings 累计 10 次往返且 6 s 内连续 5 轮、三套 B 语义并存；首轮 Monitor 日志未覆盖 Games 与 PC Monitor 的进入／返回，2026-09-20 14:08 已由人工补充确认全部手动测试通过，但未提供这两项的补充串口日志，见 `goals/ROADMAP-history.md`）。其四项菜单与四个状态页的内容与布局为人工目视确认：视图切换使用 `ESP_LOGD`，默认日志级别不输出，Settings 按设计不打印 Launcher 焦点索引。

`Settings Service` 提供 `XIAOMIAO_SETTINGS_SERVICE_SELF_TEST` 自测构建选项（默认关闭，与前述三个选项互斥）。该自测不需要 LCD 或 LVGL，通过 `main/services/xiaomiao_settings_service_selftest.c` 分 6 步覆盖缺失 key 安装默认值、非默认值写入与重启恢复、未知 schema 版本、越界布尔值、短 blob 与长 blob 的恢复路径，步骤之间用 `esp_restart()` 自动重启并靠 `xiaomiao/selftest_stage` 键推进，最后恢复干净状态并输出 `SETTINGS_SERVICE_SELF_TEST: PASS`。节点 9 已于 2026-09-20 完成人工验证：普通构建（`App version: 7088147-dirty`）与烧录通过，首启为 `source=defaults`、断电重启后为 `source=nvs`，两次均到达 `Launcher ready, 5 app(s) registered`；6 步自测固件输出 `PASS`；Settings 四页与五 App 回归由人工确认通过。唯一未验证项是 `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败两条路径——不引入自定义分区表就无法构造，只按源码检查确认，已在 `goals/20260920-1429-settings-service-nvs.md` 中记录。当前待解决事项以 `ROADMAP.md` 为准，主要包括 GD32 `0x40` 协议源码缺失、相应 LED／电机／MPU6050 实机行为尚未验证，以及已复现但未定位根因的 MicroSD／GPIO22 冲突。
