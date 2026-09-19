# 项目概览与开发导航

## 项目定位

本仓库包含学而思小喵掌机的两套固件与硬件资料：ESP32-WROVER-B 负责 UI、传感器和主要外设；GD32F350G8 负责 USB 串口桥及 ESP32 自动下载控制。目标硬件、引脚和底层协议以根目录 `README.md` 与 `xueersi-xiaomiao-schematic.pdf` 为准。

## 当前实现

ESP32 工程基于 ESP-IDF 6.1 和 LVGL 9.5。当前全部业务代码位于 `main/main.c`，启动链为：

```text
app_main
  -> 按键、LCD、ADC、I2C、LEDC、SD 初始化
  -> LVGL 显示与 keypad 输入注册
  -> lvgl FreeRTOS 任务
  -> 15 页 Hardware Dashboard
```

Dashboard 页面依次覆盖光照、热敏、MPU6050、两路 LED、蜂鸣器、两路电机、MicroSD、GPIO25/26 PWM、GPIO32/33 ADC、系统状态和 About。左右键切页，上下键调节当前值，A 执行动作，B 停止或取消。硬件状态集中在 `board_state_t`，UI 引用集中在 `ui_state_t`。

显示屏为 160 × 128 ST7735 兼容面板，使用 SPI2、RGB565 交换字节格式与 3 个全屏 DMA 缓冲；LVGL tick 为 1 ms，UI 目标刷新周期为 16 ms。TFT 与 MicroSD 共用 SPI2，通过独立 CS 分时访问。

## 两颗 MCU 的边界

- ESP32 端已按 I2C 地址 `0x40` 实现 GD32 探测、LED 寄存器和双电机 PWM 命令，也按 `0x68` 接入 MPU6050。
- `GD32_firmware/Project/src/app.c` 当前实现 USB CDC 与 USART1 双向环形缓冲，并用 DTR/RTS 控制 ESP32 IO0/EN。
- 仓库内 GD32 源码尚未实现 I2C 从机、LED 和电机协议；README 中的 `0x40` 协议是 ESP32 端沿用的硬件协议资料，联调前必须确认 GD32 实际固件版本。

## 构建与配置

ESP32 目标由根目录 `CMakeLists.txt` 定义，组件依赖见 `main/idf_component.yml` 和 `main/CMakeLists.txt`。关键默认值位于 `sdkconfig.defaults`：240 MHz CPU、80 MHz QIO Flash、4 MB Flash、80 MHz Quad PSRAM、FreeRTOS 1000 Hz 和 LVGL RGB565。常用流程：

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

环境加载使用官方 `export.bat` / `export.ps1`（需在 CMD/PowerShell 中执行，MSYS shell 会因 `MSYSTEM` 被拒绝）。当安装布局非默认（Python venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下）时，用进程级 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 指向实际位置，不改系统环境；确认版本用 `python "$env:IDF_PATH\tools\idf.py" --version`（PATH 中的 `idf.py.exe` 包装器会显示自身版本）。不依赖现有 `build/` 缓存的可复现基线构建命令见 `AGENTS.md`；2026-09-19 已在隔离构建目录完成全新配置构建与二次构建验证，记录见 `ROADMAP.md`。

GD32 使用 Keil 工程 `GD32_firmware/Project/MDK-ARM/cdc_acm.uvprojx`，目标器件为 GD32F350G8，依赖 GigaDevice DFP 3.4.0。`go.py` 仅用于原厂 MicroPython 固件环境下的硬件探查，不参与 ESP-IDF 构建。

## 目标架构与演进约束

`xiaomiao_firmware_v0.1_design.md` 规划将 Dashboard 封装为 Hardware Test App，并逐步引入 BSP、Service、App Framework 和 Launcher。该目录结构目前尚不存在。实施时以小步迁移为原则：先建立可验证边界，再移动功能；保留 15 页硬件测试；SD 缺失不得阻塞启动；业务 App 不直接操作 GPIO、SPI 或 I2C。

## 验证入口与已知缺口

仓库目前没有自动化测试。最低验证为 `idf.py build`；硬件改动还需烧录实机，检查启动日志、六个按键、屏幕刷新及受影响外设。当前待解决事项以 `ROADMAP.md` 为准，主要包括 Launcher 架构尚未实现、GD32 `0x40` 协议源码缺失，以及 MicroSD/GPIO22 冲突需在实机复核。
