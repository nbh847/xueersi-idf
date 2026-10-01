# 小喵掌机 ESP-IDF 固件与硬件资料

这是给学而思小喵掌机移植的 ESP-IDF / LVGL 固件工程，同时整理了屏幕、按键、传感器、GD32 协处理器和底层协议等硬件资料。

## 固件下载与刷入

已经编译好的 merged bin 会放在本项目的 [Releases](https://github.com/ZyoungInc/xueersi-idf/releases/latest) 页面。普通用户可以直接下载 release 里的 `xiaomiao-merged.bin` 并从 `0x0` 地址刷入，不需要自己搭建 ESP-IDF 编译环境。

示例命令：

```bash
esptool.py --chip esp32 -b 460800 write_flash 0x0 xiaomiao-merged.bin
```

刷入前请确认目标硬件是 ESP32-WROVER-B 版本的小喵掌机，并确认串口连接正常。

## 当前状态

- ESP32 侧固件已经移植到 ESP-IDF 6.1，使用 LVGL 9.5 驱动 ST7735 SPI 屏幕；开机默认进入 `main/framework/` 的 Launcher，按注册顺序显示 `Monitor`（显示名「监控」）、`Tools`、`Games`、`Settings` 与 `Hardware Test` 五个入口：前四项占满第 1 页的 2 列 × 2 行网格（监控、工具、游戏、设置，启动焦点在监控），`Hardware Test` 进入第 2 页。`Hardware Test` 是位于 `main/main.c` 的 15 页硬件状态 Dashboard，按 A 进入、长按 B 800 ms 返回；`Games` 是占位 App；`Monitor` 通过固件 Agent Service 每秒拉取电脑端 `pc-agent/` 的真实 CPU／RAM／GPU／温度快照并刷新（3 秒无有效数据自动恢复 `--`；Agent 在 Wi-Fi 取得 DHCP IPv4 后通过局域网发现），GPU／温度缺失时按可选指标降级；`Monitor` 另有第 3 页智谱与第 4 页 Codex 两个额度页，显示各窗口的剩余百分比、进度条、重置时间与括号内剩余时长（额度快照 180 秒过期，与 PC 指标的 3 秒规则相互独立）；`Tools` 在信息归位后是「暂无工具」过渡页（Wi-Fi、系统与资源诊断已迁入 Settings，番茄时钟为下一步独立任务）；`Settings` 提供 Wi-Fi 配网／自动连接／忘记网络（状态行可进入连接详情，显示状态、SSID、信号与 IPv4 实时快照），以及只读的 Display、Sound 页面和 System 子菜单（系统信息、资源状态——SD 已挂载时显示容量 MB、配置状态、关于），其中 `sound_enabled` 要等节点 12 才会产生业务效果。App Framework、Navigation、Launcher 与全局 Wi-Fi 图标位于 `main/framework/`，Settings Service、Wi-Fi Service、Agent Service、Storage Service、Assets Service 与 Font Service 位于 `main/services/`。
- 全系统简体中文化（节点 15C）已通过实机目视回归：全部用户可见文案经 `main/framework/xiaomiao_i18n` 的 217 个文案 ID 渲染，完整 GB2312（7,445 字符）12／16 px A2 字库以私有 XMF1 格式存放于本机 1.5 MB `assets` SPIFFS 分区（随 `idf.py flash` 烧录，不依赖 SD），由 Font Service 校验后整包驻留 PSRAM；字库不可用时整体回退英文文案与 Montserrat，损坏注入场景未做实机验证。串口日志、配网网页与技术缩写有意保留英文。
- 屏幕使用 160 × 128、60 MHz SPI 与单块全屏 DMA 缓冲；此前双缓冲固件曾实机验证，但当前为给 Wi-Fi 配网释放内部 RAM 改成单缓冲。当前固件已在实机连续完成两次网页配网、热点 DHCP 和目标 Wi-Fi 凭据保存；单缓冲下的刷新性能待复验。
- 由于屏幕的TE引脚没有连接到MCU，无法做垂直同步。抗撕裂。由于背光引脚直连cc，无法调节背光亮度。
- 光照、热敏、蜂鸣器、按键、MicroSD、I2C 设备探测等功能已经接入 ESP32 侧固件。
- MicroSD 重复挂载失败时出现的 GPIO22 冲突警告已修复，并于 2026-09-21 通过连续 9 次失败重试实机验证；Hardware Test 关闭、重开和返回 Launcher 均正常。SD 卡已于 2026-09-22 到货，节点 14 已完成完整实机验收（2026-09-22 15:38）：无卡冷启动与 10 次重试无 GPIO22 冲突、插卡挂载成功（SD 29818MB）、带卡冷启动直接进 MOUNTED、卸载与幂等重挂、15 页往返与五 App 烟测无回归。
- MicroSD 生命周期已迁移到独立 Storage Service（`main/services/xiaomiao_storage_service.{h,c}`，节点 14，已完成实机验收）：普通启动在共享 SPI2 建立后自动尝试挂载一次，失败不阻塞进入 Launcher；`Hardware Test` 的 MicroSD 页读取 Service 快照显示状态，A 重试挂载、B 安全卸载；挂载点固定 `/sdcard`，禁止自动格式化，GPIO22 修复时序保留在 Service 内部。
- ESP32 侧固件已包含并完成实机验证的 Wi-Fi Service：进入 `Settings → Wi-Fi` 可启动配网，设备会开启受密码保护的临时热点 `Xiaomiao-XXXX`（密码每次会话随机生成），手机连接后在浏览器打开 `http://192.168.4.1` 选网并输入密码；凭据只在试连成功取得 IPv4 后才会保存，密码错误或超时不会覆盖原有网络。保存后支持开机自动连接与断线退避重连，`Configure` 可更换网络，`Forget network` 只删除 Wi-Fi 凭据。`Tools → Wi-Fi` 显示真实的连接状态、SSID、信号档位与 IPv4 地址，屏幕右上角在所有页面显示四档 Wi-Fi 状态图标。详细验证记录见 `goals/20260920-2210-wifi-service.md`。
- GD32 固件仍在开发中，当前仓库源码主要完成 USB CDC、UART 桥和 ESP32 自动下载控制，未包含下文所述的 I2C `0x40` 从机处理代码。
- ESP32 侧已按原有 `0x40` 协议实现 LED、电机命令。项目负责人于 2026-09-27 报告可在 Hardware Test 中实机控制 LED1／LED2；但板上 GD32 固件与仓库源码的对应关系待查。电机目前未接入，实机行为未验证。欢迎大家测试或在 Issues 里提出建议。

## PC Agent（电脑端，节点 11）

设备上的 Monitor 数据来自运行在电脑上的统一 `Xiaomiao Agent`（`pc-agent/`，Python 3，仅标准库 + `psutil`，GPU 优先 `nvidia-smi`）：

首次运行会在 `.venv/<system>/` 下创建对应系统的虚拟环境并安装依赖。启动脚本会按当前系统选择 Python：macOS／Linux 使用 `python3`，Windows 使用 `python`（通过 Git Bash 运行）：

```bash
bash pc-agent/start.sh                                      # 默认 0.0.0.0:8766
```

- API：`GET /api/v1/health`、`GET /api/v1/pc/metrics`、`GET /api/v1/quotas/codex`、`GET /api/v1/quotas/zhipu`；HTTP 只读取后台缓存快照，不在请求路径中现场采集。
- 额度端点各返回一个服务商的两个窗口（Codex `5H`／`7D`，智谱 `5H`／`1W`），字段为 `label`、`remaining_percent`、`resets_at`、`reset_at_local`（`MM-DD HH:MM`）与 `reset_in_sec`；`status` 区分 `ok`／`auth_required`／`unavailable`／`invalid_data`／`stale`。两家各自 60 秒轮询、180 秒无成功更新标 `stale`，一方失败不影响另一方或 PC 指标。
- Codex 额度经本机 `codex app-server` 只读查询（先 `account/read` 且 `refreshToken: false`，再读 `account/rateLimits/read`）；智谱额度按已确认顺序只读发现本机 Token（`~/.qclaw/agents/main/agent/models.json` → `~/.claude/settings.json` → 环境变量），只接受 `open.bigmodel.cn` 与 `dev.bigmodel.cn`，按 `TOKENS_LIMIT` 的 `unit=3`→`5H`、`unit=6`→`1W` 映射。凭据只在电脑本机使用，不进入固件、HTTP 响应或日志。
- HTTP 默认监听 `0.0.0.0:8766`，UDP 服务发现监听 `0.0.0.0:8767`。设备取得 Wi-Fi DHCP IPv4 后向局域网广播发现请求，Agent 用单播回复；设备从回复来源读取电脑的当前 IPv4，并从响应读取 HTTP 端口。`0.0.0.0` 是电脑端的监听地址，不是设备请求地址。
- 电脑与设备需处于可互通的同一局域网；电脑防火墙需允许 TCP `8766` 和 UDP `8767` 入站。暂不支持跨子网或互联网发现。该 Agent 无 TLS 与鉴权，仅面向可信局域网。
- GPU 与温度依赖本机硬件：无 NVIDIA GPU 或温度源时对应字段为 `null`，设备显示 `--`，属正常降级。
- 设备侧额度页与额度端点已实现并完成实机验收（2026-09-27，含两轮排版修正：两项指标合并为单标签、状态文字避开右上 Wi-Fi 图标、标题框加高到 20 px）；逐项上游故障形态仍以软件用例覆盖为主。

## 开发文档导航

- [`AGENTS.md`](AGENTS.md)：Agent 与贡献者进入仓库时的执行规则、构建命令和验证要求。
- [`ROADMAP.md`](ROADMAP.md)：当前状态、当前开发节点、有序开发节点、下一步与已知风险，是项目状态源。
- [`goals/ROADMAP-history.md`](goals/ROADMAP-history.md)：逐条施工与验证流水的日期索引（结论 + 对应 Goal 路径）；证据细则在各 Goal 文档。
- [`docs/project-overview.md`](docs/project-overview.md)：当前代码结构、启动链、双 MCU 边界和开发入口。
- [`docs/xiaomiao_firmware_v0.1_design.md`](docs/xiaomiao_firmware_v0.1_design.md)：Launcher 与 App Framework 的目标设计，尚未全部实现。

## 原理图与鸣谢

原理图文件已整理为 [`xueersi-xiaomiao-schematic.pdf`](xueersi-xiaomiao-schematic.pdf)。

感谢 ID「我为电波狂」对硬件进行测量并制作原理图，这部分资料对后续移植和维护非常关键。

## 参与项目

如果这个项目对你有帮助，欢迎 Star。遇到问题、发现硬件差异、或者有协议兼容建议，可以提交 Issue。也欢迎提交 PR，我会审核后合并。
本项目针对官方原厂硬件，不考虑对硬件的魔改因素。对硬件改动的支持需要另外开branch或复制到自己的仓库（但需要注意下一章的要求）。

## 使用与署名要求

本项目由ZYoungInc（wechat/tel：15657325738）完全用爱发电并完全免费提供给爱好者们学习和交流等非营利目的。如有侵权，请联系本人。二次开发、转载、分发、商用或以任何形式使用本项目内容时，请务必保留并明确引用原作者与本项目来源，以尊重劳动成果。违反者将依法追究责任；本人保留所有权利。也欢迎举报滥用。

## 1. 总体架构

```text
PC USB
  │
  │ USB CDC / 下载串口
  ▼
GD32F350G8
  ├─ USB 转 ESP32 UART0
  ├─ ESP32 自动下载 / 自动复位控制
  ├─ I2C 从机地址 0x40
  ├─ 控制双路电机驱动 HR8833 / DRV8833
  └─ 控制板载 LED1 / LED2

ESP32-WROVER-B
  ├─ SPI TFT 显示屏
  ├─ MicroSD 卡
  ├─ 6 个按键
  ├─ 蜂鸣器 PWM
  ├─ 光照 ADC
  ├─ 热敏 ADC
  ├─ I2C 主机
  │   ├─ GD32F350G8：0x40
  │   └─ MPU6050：0x68
  └─ 预留扩展 IO
```

核心关系：

```text
ESP32 = 主控 / UI / Python 运行环境 / 屏幕 / SD / 按键 / 传感器
GD32  = USB 串口桥 / ESP32 自动烧录控制 / 通过 I2C 接收 LED 与电机命令并驱动对应输出
0x40  = GD32 的 I2C 从机地址
```

这是板级职责分工，不代表仓库中的 GD32 固件已经实现了全部功能。ESP32 是 I2C 主机，向地址 `0x40` 发送 LED／电机命令；LED 与电机输出由 GD32 一侧负责。因此 Hardware Test 能控制 LED，说明板上的控制链路可用，但不等于 ESP32 直接驱动了 LED，也不能证明仓库里的 GD32 源码就是当前板载固件。

***

## 2. ESP32 引脚分配

### 2.1 TFT 显示屏

| 功能             | ESP32 引脚   |
| -------------- | ---------- |
| SPI SCK        | GPIO18     |
| SPI MOSI       | GPIO23     |
| SPI MISO       | GPIO19     |
| TFT DC         | GPIO4      |
| TFT CS         | GPIO5      |
| TFT RES / 相关复用 | GPIO19     |

显示对象信息：

```text
显示分辨率：160 × 128
SPI：SPI2
SPI 频率：40 MHz
SCK：GPIO18
MOSI：GPIO23
MISO：GPIO19
DC：GPIO4
```

屏幕底层通过 `FrameBuffer` 和 `SCREEN` 对象刷新。

***

### 2.2 MicroSD 卡

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| SPI SCK    | GPIO18     |
| SPI MOSI   | GPIO23     |
| SPI MISO   | GPIO19     |
| SD CS      | GPIO22     |

TFT 与 MicroSD 共用 SPI2，通过不同 CS 分时使用。

***

### 2.3 按键

| 按键         | ESP32 引脚   |
| ---------- | ---------- |
| 上          | GPIO2      |
| 下          | GPIO13     |
| 左          | GPIO27     |
| 右          | GPIO35     |
| A          | GPIO34     |
| B          | GPIO12     |

注意：

```text
GPIO34、GPIO35 是输入专用脚。
GPIO12 是启动相关敏感脚。
```

***

### 2.4 ADC 传感器

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| 光照传感器      | GPIO36     |
| 热敏电阻       | GPIO39     |

已确认：

```text
sensor.getLight() = ADC(GPIO36).read()
sensor.getTemp()  = ADC(GPIO39) 后换算
```

***

### 2.5 蜂鸣器

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| 无源蜂鸣器      | GPIO14     |

底层对象：

```text
PWM(14, freq=440, duty=0)
```

也就是：

```text
GPIO14 → PWM → 无源蜂鸣器
```

***

### 2.6 I2C 总线

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| I2C SCL    | GPIO15     |
| I2C SDA    | GPIO21     |

I2C 设备：

| 地址         | 设备         |
| ---------- | ---------- |
| 0x40       | GD32F350G8 |
| 0x68       | MPU6050    |

当前已确认：

```text
GD32：0x40
MPU6050：0x68，未安装时不会出现在 scan 结果中
```

***

### 2.7 UART0

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| UART0 TX   | GPIO1      |
| UART0 RX   | GPIO3      |

该 UART0 通过 GD32 转 USB 与电脑通信，用于 Python 终端、程序上传、ESP32 自动下载。

***

## 3. GD32F350G8 连接关系

原理图标注的完整芯片型号为 `GD32F350G8U6TR`。GigaDevice 官方 [GD32F350 产品选型页](https://www.gigadevice.com/product/mcu/main-stream-mcus/gd32f3-series/gd32f350)列出的规格为 Cortex-M4 内核、最高 108 MHz、64 KB Flash、8 KB SRAM、QFN28 封装。

按原理图，GD32 在板级承担以下功能：

```text
1. USB CDC 串口桥
2. ESP32 自动复位 / 自动下载控制
3. I2C 从机 0x40
4. LED1 / LED2 控制
5. 双路电机控制
6. HR8833 / DRV8833 控制信号输出
```

已知相关连接：

| 功能          | 连接对象                     |
| ----------- | ------------------------ |
| USB D+ / D- | USB 接口                   |
| UART 桥      | ESP32 GPIO1 / GPIO3      |
| 自动下载控制      | ESP32 IO0 / 复位相关线路       |
| I2C         | ESP32 GPIO15 / GPIO21    |
| 电机 PWM      | HR8833 / DRV8833         |
| LED 控制      | LED1 / LED2              |
| SWD         | TMS / TCK / RST / GND 焊盘 |

### 固件源码与板载行为核对（2026-09-27）

仓库 [GD32_firmware 工程](https://github.com/ZyoungInc/xueersi-idf/tree/main/GD32_firmware)当前实现 USB CDC、USART1 串口桥，以及 DTR／RTS 对 ESP32 IO0／EN 的自动下载控制；源码中没有 I2C `0x40` 从机处理。查阅公开仓库对应目录也未找到该协议实现。项目负责人报告 Hardware Test 已能控制 LED1／LED2，但该实机行为与当前可见 GD32 源码之间的对应关系尚未查明。

公开 Release 中未找到单独标注为 GD32 的固件镜像。虽然有名为 `letsgo.bin` 的资产，但其内容是否包含 GD32 固件未经确认，不能据此认定它实现了 `0x40` 协议。电机目前未接入，电机行为未验证。

***

## 4. I2C 地址与设备

### 4.1 I2C 总线

```text
I2C 控制器：ESP32 I2C(0)
SCL：GPIO15
SDA：GPIO21
频率：100 kHz
```

### 4.2 地址表

| I2C 地址     | 设备         | 说明         |
| ---------- | ---------- | ---------- |
| 0x40       | GD32F350G8 | LED、电机控制   |
| 0x68       | MPU6050    | 加速度计 / 陀螺仪 |

***

## 5. GD32 0x40 协议

## 5.1 LED 协议

GD32 的 LED 控制使用 I2C memory write 形式。

| 功能         | I2C 地址     | 寄存器        | 数据         |
| ---------- | ---------- | ---------- | ---------- |
| LED1 关闭    | 0x40       | 0xA0       | 0          |
| LED1 打开    | 0x40       | 0xA0       | 1          |
| LED2 关闭    | 0x40       | 0xA1       | 0          |
| LED2 打开    | 0x40       | 0xA1       | 1          |

LED 对象内部状态：

```text
LED1:
  reg = 0xA0

LED2:
  reg = 0xA1
```

***

## 5.2 电机协议概览

电机控制通过 I2C 向 0x40 写入一组 PWM 寄存器格式数据。

基本格式：

```text
I2C 地址：0x40

数据格式：
[
  起始寄存器,
  通道A_ON_L,
  通道A_ON_H,
  通道A_OFF_L,
  通道A_OFF_H,
  通道B_ON_L,
  通道B_ON_H,
  通道B_OFF_L,
  通道B_OFF_H
]
```

每个电机占两个 PWM 通道：

```text
一个通道控制一个方向输入。
另一个通道控制反方向输入。
```

方向逻辑：

```text
方向 1：
  IN_A = PWM
  IN_B = 0

方向 0：
  IN_A = 0
  IN_B = PWM
```

***

## 5.3 电机编号与寄存器

| 电机编号       | 起始寄存器      | 占用通道         |
| ---------- | ---------- | ------------ |
| Motor 2    | 0x06       | PWM 通道 0 / 1 |
| Motor 1    | 0x0E       | PWM 通道 2 / 3 |

***

## 5.4 速度映射

速度参数范围：

```text
speed = 0 ~ 255
```

转换关系：

```text
PWM_12bit = speed × 16
PWM_12bit = speed << 4
```

示例：

| speed      | PWM 十进制    | PWM 十六进制   | 低字节        | 高字节        |
| ---------- | ---------- | ---------- | ---------- | ---------- |
| 0          | 0          | 0x0000     | 0x00       | 0x00       |
| 1          | 16         | 0x0010     | 0x10       | 0x00       |
| 10         | 160        | 0x00A0     | 0xA0       | 0x00       |
| 50         | 800        | 0x0320     | 0x20       | 0x03       |
| 100        | 1600       | 0x0640     | 0x40       | 0x06       |
| 128        | 2048       | 0x0800     | 0x00       | 0x08       |
| 200        | 3200       | 0x0C80     | 0x80       | 0x0C       |
| 255        | 4080       | 0x0FF0     | 0xF0       | 0x0F       |

***

## 5.5 电机 1 数据格式

### Motor 1，方向 1

```text
[
  0x0E,
  0x00, 0x00, PWM_L, PWM_H,
  0x00, 0x00, 0x00, 0x00
]
```

### Motor 1，方向 0

```text
[
  0x0E,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, PWM_L, PWM_H
]
```

***

## 5.6 电机 2 数据格式

### Motor 2，方向 1

```text
[
  0x06,
  0x00, 0x00, PWM_L, PWM_H,
  0x00, 0x00, 0x00, 0x00
]
```

### Motor 2，方向 0

```text
[
  0x06,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, PWM_L, PWM_H
]
```

***

## 5.7 全部电机停止

全停命令：

```text
I2C 地址：0x40
数据：[0x00, 0x00, 0x00, 0x00, 0x00]
```

***

## 6. MPU6050

### 6.1 总线连接

| 功能         | ESP32 引脚   |
| ---------- | ---------- |
| SCL        | GPIO15     |
| SDA        | GPIO21     |

### 6.2 地址

```text
MPU6050 I2C 地址：0x68
```

### 6.3 传感器对象内部字段

```text
addr = 104 = 0x68
imuReady = True / False
imu = 14 字节缓存
```

### 6.4 数据类型

MPU6050 提供：

```text
accX
accY
accZ
gyroX
gyroY
gyroZ
pitch
roll
gesture
```

***

## 7. SugarASR 扩展占用

`sugar_asr.py` 使用：

```text
UART1
TX = GPIO21
RX = GPIO15
波特率 = 115200
```

这与板载 I2C 引脚重合：

```text
GPIO21 = I2C SDA
GPIO15 = I2C SCL
```

因此：

```text
使用 SugarASR 时，GPIO15 / GPIO21 会作为 UART1 使用。
使用 LED、电机、MPU6050 时，GPIO15 / GPIO21 会作为 I2C 使用。
```

***

## 8. 板载扩展 IO

板上预留扩展 IO：

| ESP32 GPIO | 类型               | 说明           |
| ---------- | ---------------- | ------------ |
| GPIO33     | GPIO / ADC       | 可作输入、输出、ADC  |
| GPIO32     | GPIO / ADC       | 可作输入、输出、ADC  |
| GPIO26     | GPIO / DAC / PWM | 可作输出、PWM、DAC |
| GPIO25     | GPIO / DAC / PWM | 可作输出、PWM、DAC |

推荐作为普通扩展口使用的 ESP32 引脚：

```text
GPIO25
GPIO26
GPIO32
GPIO33
```

***

## 9. ESP32 引脚占用总表

| GPIO       | 当前功能                  | 备注               |
| ---------- | --------------------- | ---------------- |
| GPIO1      | UART0 TX              | 经 GD32 转 USB 串口  |
| GPIO2      | 上键                    | 输入               |
| GPIO3      | UART0 RX              | 经 GD32 转 USB 串口  |
| GPIO4      | TFT DC                | 显示               |
| GPIO5      | TFT CS                | 显示               |
| GPIO12     | B 键                   | 启动相关敏感脚          |
| GPIO13     | 下键                    | 输入               |
| GPIO14     | 蜂鸣器                   | PWM              |
| GPIO15     | I2C SCL / SugarASR RX | 与 GPIO21 成组复用    |
| GPIO18     | SPI SCK               | TFT / SD 共用      |
| GPIO19     | SPI MISO / TFT 相关     | TFT / SD 相关      |
| GPIO21     | I2C SDA / SugarASR TX | 与 GPIO15 成组复用    |
| GPIO22     | SD CS                 | MicroSD          |
| GPIO23     | SPI MOSI              | TFT / SD 共用      |
| GPIO25     | 预留扩展                  | GPIO / DAC / PWM |
| GPIO26     | 预留扩展                  | GPIO / DAC / PWM |
| GPIO27     | 左键                    | 输入               |
| GPIO32     | 预留扩展                  | GPIO / ADC       |
| GPIO33     | 预留扩展                  | GPIO / ADC       |
| GPIO34     | A 键                   | 输入专用             |
| GPIO35     | 右键                    | 输入专用             |
| GPIO36     | 光照 ADC                | 输入专用             |
| GPIO39     | 热敏 ADC                | 输入专用             |

***

## 10. 开发用对象映射

| Python 对象 / 模块       | 底层硬件               |
| -------------------- | ------------------ |
| `screen`             | FrameBuffer + TFT  |
| `display`            | 160 × 128 TFT 显示封装 |
| `tft`                | 底层 SCREEN 对象       |
| `fb` / `fbuf`        | FrameBuffer        |
| `vspi`               | SPI2，40 MHz        |
| `i2c`                | I2C0，SCL=15，SDA=21 |
| `led1`               | GD32 0x40，寄存器 0xA0 |
| `led2`               | GD32 0x40，寄存器 0xA1 |
| `buzzer`             | GPIO14 PWM         |
| `sensor.adcLight`    | GPIO36 ADC         |
| `sensor.adcTemp`     | GPIO39 ADC         |
| `sensor.btns`        | 6 个 ESP32 GPIO 按键  |
| `motor.Motor`        | GD32 0x40 电机协议     |
| `sugar_asr.SugarASR` | UART1，TX=21，RX=15  |

***

## 11. 开发时可直接使用的底层信息

### 11.1 I2C

```text
I2C0:
  SCL = GPIO15
  SDA = GPIO21
  freq = 100000

设备：
  0x40 = GD32
  0x68 = MPU6050
```

### 11.2 LED

```text
LED1:
  addr = 0x40
  reg  = 0xA0
  value 0 = off
  value 1 = on

LED2:
  addr = 0x40
  reg  = 0xA1
  value 0 = off
  value 1 = on
```

### 11.3 电机

```text
Motor 1:
  起始寄存器 = 0x0E

Motor 2:
  起始寄存器 = 0x06

speed:
  0 ~ 255
  PWM = speed << 4

direction:
  1 = 第一方向通道 PWM，第二方向通道 0
  0 = 第一方向通道 0，第二方向通道 PWM
```

### 11.4 蜂鸣器

```text
GPIO14
PWM 输出
无源蜂鸣器
```

### 11.5 光照 / 温度

```text
光照：
  GPIO36
  ADC

热敏：
  GPIO39
  ADC
```

### 11.6 按键

```text
up    = GPIO2
down  = GPIO13
left  = GPIO27
right = GPIO35
a     = GPIO34
b     = GPIO12
```

### 11.7 显示

```text
分辨率：160 × 128
SPI：SPI2
SCK：GPIO18
MOSI：GPIO23
MISO：GPIO19
DC：GPIO4
CS：GPIO5
```
