# Xiaomiao Firmware v0.1 设计文档

> 基于 `xueersi-idf` / ESP-IDF / LVGL 的可扩展掌机固件架构\
> 目标平台：学而思小喵掌机（ESP32）

> 状态说明：本文是目标架构与验收方案，不是当前实现说明。App Framework、Navigation 与 Launcher 已落地，15 页 Hardware Dashboard 已注册为 `Hardware Test` App，Service 与 BSP 分层尚未落地；各节点实现与验证进度一律以根目录 `ROADMAP.md` 为准，本文不重复维护该状态。

## 1. v0.1 目标

第一版不追求功能数量，而是优先建立稳定、清晰、可持续扩展的"掌机系统骨架"。

核心目标：

> 将 `xueersi-idf` 当前直接启动的 Hardware Dashboard 改造成 **Launcher +
> App Framework**，同时保留原项目已有硬件能力和 15 项测试。

``` text
开机
  ↓
ESP-IDF / FreeRTOS
  ↓
硬件初始化
  ↓
LVGL
  ↓
App Framework
  ↓
Launcher
  ↓
Games / PC Monitor / Tools / Settings / Hardware Test
```

## 2. 核心架构原则

-   **业务 App 不直接操作具体 GPIO、SPI、I²C 等底层硬件接口。**
-   App 只表达"要做什么"；Service 决定"怎么做"；BSP / Driver
    知道具体硬件和 GPIO。
-   Launcher 不针对每个 App 写死逻辑，App 通过统一注册机制进入系统。
-   SD 卡属于可选能力，初始化失败不能阻塞系统启动。
-   原有 15 项硬件测试不删除，封装为 `Hardware Test App`。
-   优先复用原仓库已经验证的驱动和 LVGL
    适配，不为了目录漂亮进行无意义的大规模重构。

``` text
App
 ↓  input_is_pressed(BUTTON_A)
System Service
 ↓
BSP / Driver
 ↓  GPIO XX
ESP-IDF
 ↓
真实硬件
```

## 3. 分层设计

  -----------------------------------------------------------------------------------------------------
  层                      职责                                  示例
  ----------------------- ------------------------------------- ---------------------------------------
  Apps                    具体业务功能，不感知 GPIO             Games、PC Monitor、Settings、Hardware
                                                                Test

  App Framework           App 注册、生命周期、导航、Launcher    App Registry、App Manager、Navigation

  System Services         提供业务语义 API                      Input、Audio、WiFi、Storage、Settings

  BSP / Driver            掌机硬件适配，唯一知道具体引脚/总线   Button、TFT、Buzzer、SD

  ESP-IDF + FreeRTOS      官方 SDK、驱动能力、任务调度          GPIO、SPI、Wi-Fi、Task
  -----------------------------------------------------------------------------------------------------

## 4. Launcher 设计

屏幕分辨率为 `160 × 128`，第一版采用简洁的两列
Launcher，优先保证可读性、焦点反馈和按键操作稳定。

``` text
┌────────────────────────┐
│ Xiaomiao          WiFi │
├────────────────────────┤
│  [ GAME ]   [  PC  ]   │
│    游戏       监控      │
│                        │
│  [ TOOL ]   [ SET ]    │
│    工具       设置      │
│                        │
│       Hardware Test    │
└────────────────────────┘
```

操作规则：

-   `← / →`：在当前行内换列；在右列再按 `→` 翻到下一页，在左列再按 `←` 翻回上一页
-   `↑ / ↓`：在当前页内换行，不会翻页
-   `A`：打开当前 App
-   `B`：返回上一级 / Launcher
-   App 增多后按每页 4 格（2 列 × 2 行）分页，翻页用左右键
-   v0.1 不做复杂动画

## 5. App Framework

所有 App 使用统一描述结构：

``` c
typedef struct {
    const char *id;
    const char *name;
    const char *icon;

    void (*init)(void);
    void (*open)(void);
    void (*close)(void);
} xiaomiao_app_t;
```

新增 App 的目标开发流程：

``` text
创建 App
   ↓
实现统一接口
   ↓
注册 App
   ↓
Launcher 自动出现入口
```

Launcher 从 App Registry 获取应用列表，而不是针对每个 App 写死代码。

## 6. v0.1 应用范围

### Games

v0.1 只提供占位页面，用于验证 App 生命周期、进入和返回。

后续可以加入：

-   Snake
-   Tetris
-   2048
-   其他小游戏

### PC Monitor

v0.1 提供监控 UI 骨架：

``` text
┌──────────────────────┐
│ PC Monitor           │
│                      │
│ CPU        -- %      │
│ RAM        -- %      │
│ GPU        -- %      │
│ TEMP       -- °C     │
│                      │
└──────────────────────┘
```

后续实现：

``` text
PC 数据采集
   ↓
Wi-Fi
   ↓
ESP32
   ↓
PC Monitor App
```

### Tools

第一版提供：

-   WiFi
-   System Info
-   About

`System Info` 可以展示：

``` text
ESP32-D0WD
240 MHz
Flash 4 MB
PSRAM 8 MB
ESP-IDF 6.1
Firmware v0.1
```

### Settings

第一版建立 UI 骨架：

``` text
Settings
├── WiFi
├── Display
├── Sound
└── System
```

后续通过：

``` text
Settings App
     ↓
Settings Service
     ↓
NVS
```

实现持久化配置。

### Hardware Test

完整保留原项目当前的 15 项硬件测试。

从：

``` text
开机
 ↓
15 项 Hardware Dashboard
```

调整为：

``` text
开机
 ↓
Launcher
 ↓
Hardware Test
 ↓
原有 15 项测试
```

## 7. System Services

v0.1 先建立边界和基础接口，不要求一次实现全部能力。

``` text
services/
├── input_service
├── audio_service
├── display_service
├── wifi_service
├── storage_service
└── settings_service
```

业务 App 应使用具有业务语义的接口：

``` c
input_is_pressed(BUTTON_A);

audio_beep(100);

wifi_is_connected();

settings_get(...);
```

禁止业务 App 直接调用：

``` c
gpio_get_level(...);
gpio_set_level(...);
spi_device_transmit(...);
```

核心原则：

> App 描述"我要做什么"，Service 决定"怎么做"，BSP 知道"具体是哪根 GPIO /
> 哪条总线"。

## 8. Hardware Test 策略

原项目当前 15 项测试：

-   不删除
-   不作为默认首页
-   尽量保持原测试逻辑
-   封装为 `Hardware Test App`

以后如果出现按钮、屏幕、蜂鸣器、PSRAM 等异常，可以快速判断：

``` text
业务功能异常
   ↓
Hardware Test
   ↓
硬件测试正常？
   ├── 是 → App / Service 软件问题
   └── 否 → Driver / BSP / 硬件问题
```

## 9. SD 卡策略

早期运行日志曾出现：

``` text
sdmmc_card_init failed
gpio: conflict found for GPIO[22]
```

其中 GPIO22 冲突警告的根因已定位为 ESP-IDF 6.1 SDSPI 失败清理时的 GPIO 占用位图泄漏。固件现已手动拆分 SDSPI 初始化与 FATFS 挂载，在失败清理和成功卸载时先复位 GPIO22，再移除 SDSPI 设备；2026-09-21 连续 9 次 `sdmmc_card_init failed (0x107)` 实机重试均未再出现冲突警告。节点 14 又于 2026-09-22 完成正常卡挂载、卸载、重新挂载、带卡冷启动及连续 10 次无卡重试，确认无卡时的 `0x107` 是卡不响应超时，不是共享 SPI 总线问题。

v0.1 不将 SD 卡作为系统启动的必需条件。

设计为：

``` text
storage_service_init()
  │
  ├── 成功
  │     ↓
  │  SD_AVAILABLE
  │
  └── 失败
        ↓
     SD_UNAVAILABLE
        ↓
     记录日志
        ↓
     继续启动 Launcher
```

未来以下功能可能需要 SD 卡：

-   大量图片和图标
-   音乐
-   游戏资源
-   ROM
-   日志
-   文件管理器
-   用户下载内容

节点 14 已在现有驱动修复基础上完成 Storage Service 分层与实机验收。MicroSD 生命周期现由 `main/services/xiaomiao_storage_service.{h,c}` 统一管理，固定挂载点为 `/sdcard`，失败不阻塞 Launcher；Hardware Test 通过 Service 快照及挂载／卸载接口操作，不再直接持有 SDSPI 或 FATFS 资源。

## 10. FreeRTOS 策略

v0.1 避免"一个 App 一个 Task"。

优先使用少量系统级任务：

``` text
FreeRTOS
├── UI / LVGL Task
├── Input Task
└── System / Background Task
```

只有确实需要持续后台运行的功能，后续才创建 Worker Task。

例如：

``` text
PC Monitor
     ↓
Network Worker
     ↓
后台接收 PC 数据
```

## 11. 推荐目录结构

``` text
xueersi-idf/
│
├── main/
│   └── app_main.c
│
├── bsp/
│   └── xiaomiao/
│       ├── board.h
│       ├── display.c
│       ├── input.c
│       ├── audio.c
│       └── storage.c
│
├── framework/
│   ├── app.h
│   ├── app_manager.c
│   ├── app_registry.c
│   ├── launcher.c
│   └── navigation.c
│
├── services/
│   ├── input_service.c
│   ├── audio_service.c
│   ├── wifi_service.c
│   ├── storage_service.c
│   └── settings_service.c
│
├── apps/
│   ├── games/
│   ├── pc_monitor/
│   ├── tools/
│   ├── settings/
│   └── hardware_test/
│
└── assets/
    ├── icons/
    └── fonts/
```

实际改造时不要求一次把原仓库全部移动成上述结构。

原则是：

> **能复用原代码就复用，不为了目录结构而重构。**

## 12. 启动流程

``` text
app_main()
   ↓
BSP 初始化
   ↓
System Services 初始化
   ↓
LVGL 初始化
   ↓
App Registry 注册应用
   ↓
App Manager 启动
   ↓
Launcher
   ↓
用户选择 App
   ↓
app.open()
   ↓
App 运行
   ↓
B 返回
   ↓
app.close()
   ↓
Launcher
```

## 13. v0.1 验收标准

-   [ ] 开机默认进入 Xiaomiao Launcher，而不是直接进入 Hardware
    Dashboard。
-   [ ] 方向键可以稳定移动焦点。
-   [ ] A 键可以进入 App。
-   [ ] B 键可以返回。
-   [ ] Launcher 包含 Games、PC Monitor、Tools、Settings、Hardware
    Test。
-   [ ] Games 至少具有可进入、可返回的占位页面。
-   [ ] PC Monitor 至少具有可进入、可返回的 UI 骨架。
-   [ ] 原有 15 项 Hardware Test 可以从 Launcher 进入并继续使用。
-   [ ] App 通过统一 App Registry 注册。
-   [ ] Launcher 不针对单个 App 写死入口逻辑。
-   [ ] 业务 App 不直接操作具体 GPIO / SPI / I²C。
-   [ ] SD 卡不存在或初始化失败时，系统仍然可以正常进入 Launcher。
-   [ ] 当前 ESP-IDF v6.1 环境可以正常 Build。
-   [ ] 可以正常 Flash 到小喵掌机。
-   [ ] Monitor 无致命启动错误。

## 14. v0.1 明确不做

-   不追求一次实现大量游戏。
-   不一次实现全部网络功能。
-   不实现类似 Android APK 的动态 App 安装。
-   不要求 SD 卡作为系统启动依赖。
-   不进行与功能无关的大规模目录重构。
-   不为每个 App 创建独立 FreeRTOS Task。
-   不允许业务 App 硬编码具体 GPIO。

## 15. 分阶段开发节点

后续严格按节点顺序开发、验证和提交。一个节点未达到验收结果时，不并入下一个节点；GD32 协议补全可以并行，但依赖 LED、电机的正式功能必须等待其完成。节点状态统一记录在根目录 `ROADMAP.md`。

| 节点 | 版本 | 功能 | 主要交付与验收结果 |
| --- | --- | --- | --- |
| 0 | 前置 | 构建基线恢复 | 修复 ESP-IDF Python 环境；清空构建缓存后仍能执行 `idf.py build`。 |
| 1 | v0.1 | App Framework | 实现 App 描述、Registry、Manager 和 `init/open/close` 生命周期；测试 App 可注册和打开。 |
| 2 | v0.1 | Navigation | 实现当前 App 状态和统一返回机制；A 进入、B 返回且 LVGL 对象完整释放。 |
| 3 | v0.1 | Launcher | 实现 160 × 128 两列首页、焦点移动和基础分页；开机默认进入 Launcher。 |
| 4 | v0.1 | Hardware Test App | 将现有 15 页 Dashboard 接入 App Framework；全部原有测试可进入、操作和返回。 |
| 5 | v0.1 | Games 占位 App | 提供可进入、可返回的 Games 页面，不创建独立 FreeRTOS Task。 |
| 6 | v0.1 | PC Monitor UI | 提供 CPU、RAM、GPU、温度 UI 骨架；无数据时统一显示 `--`。 |
| 7 | v0.1 | Tools App | 提供 System Info、About 和 Wi-Fi 状态入口，显示真实系统信息。 |
| 8 | v0.1 | Settings UI | 建立 Wi-Fi、Display、Sound、System 菜单；未实现选项明确标记状态。 |
| 9 | v0.2 | Settings Service 与 NVS | 实现默认值、设置读写和持久化；配置缺失或损坏时安全回退。 |
| 10 | v0.2 | Wi-Fi Service | 实现扫描、连接、断线重连和状态查询；无网络不阻塞启动。 |
| 11 | v0.3 | PC Monitor 通信 | 定义 PC 数据协议并接收真实指标，持续更新监控页面。 |
| 12 | v0.4 | Audio Service | 统一蜂鸣器接口、音效开关和音量语义，App 不直接调用 LEDC。 |
| 13 | v0.4 | 首个正式游戏 | 优先实现 Snake；支持暂停和退出，返回 Launcher 后完整释放资源。 |
| 14 | v0.5 | Storage Service | 实现 MicroSD 探测、挂载、卸载和错误状态；失败不影响系统启动。 |
| 15 | v0.5 | Assets 与文件系统 | 加载图标、字体、音乐和游戏资源；资源缺失时安全降级。 |
| 16 | 硬件支线 | GD32 `0x40` 协议补全 | 实现 I2C 从机、LED 和双电机控制，并与 ESP32 当前命令格式完成实机联调。 |

版本边界：v0.1 形成 Launcher、App Framework 和 Hardware Test；v0.2 完成持久化设置与 Wi-Fi；v0.3 打通 PC Monitor 数据；v0.4 加入 Audio 和首个游戏；v0.5 完成 SD、Assets 与文件系统。

节点拆分补充：节点 3 先交付 Launcher 运行时代码，普通固件默认入口保持不变；把默认入口切换为 Launcher 放到节点 4 与 Hardware Test App 一起完成，避免节点 3 与节点 4 之间出现现有 Dashboard 不可达的中间状态。v0.1“开机默认进入 Launcher”的验收要求不变。

------------------------------------------------------------------------

## 核心设计原则

> **App 描述"我要做什么"，Service 决定"怎么做"，BSP
> 知道"具体硬件怎么接"。**

v0.1 最重要的交付不是功能数量，而是让后续开发模式从：

> "继续修改一个 ESP32 程序"

转变为：

> **"给 Xiaomiao Firmware 开发一个新的 App"。**
