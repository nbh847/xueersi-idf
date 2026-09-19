# Repository Guidelines

## 首次进入项目

依次阅读 `ROADMAP.md`、`README.md` 和 `docs/project-overview.md`。前者是当前进度源，`README.md` 保存硬件事实与协议，项目概览解释代码现状和开发入口。`docs/xiaomiao_firmware_v0.1_design.md` 是目标架构方案，不代表相关模块已经实现。

## 项目结构与模块组织

- `main/main.c` 是当前 ESP32-WROVER-B 固件的唯一业务源文件，包含 LVGL 9.5 硬件 Dashboard 及屏幕、按键、ADC、I2C、蜂鸣器和 MicroSD 驱动。当前尚无 `apps/`、`services/` 或 `bsp/` 分层。
- `main/idf_component.yml` 固定组件依赖，`sdkconfig.defaults` 与 `sdkconfig.ci` 保存可复现的 ESP-IDF 配置。
- `GD32_firmware/` 是独立的 GD32F350 Keil 工程；当前仓库源码实现 USB CDC、UART 桥和 ESP32 自动下载控制，尚未实现 README 所述的 I2C `0x40` LED/电机从机协议。请勿把厂商库改动混入 ESP32 功能提交。
- `docs/` 存放设计说明，`README.md` 记录已确认的引脚和协议，根目录 PDF 为硬件原理图。
- `build/`、`managed_components/`、本地 `sdkconfig` 均为生成或环境文件，不应提交。

## 构建、烧录与开发命令

先加载 ESP-IDF 环境，并在仓库根目录执行：

```bash
idf.py set-target esp32   # 首次配置目标芯片
idf.py build              # 配置、解析依赖并编译固件
idf.py -p COM5 flash      # 将固件烧录到指定串口
idf.py -p COM5 monitor    # 查看启动日志和运行状态
```

GD32 固件使用 Keil 打开 `GD32_firmware/Project/MDK-ARM/cdc_acm.uvprojx`。`go.py` 是原厂 MicroPython 环境的硬件探查脚本，不属于 ESP-IDF 构建流程；运行前按脚本提示断开电机。

## 编码风格与命名约定

C 代码使用 4 空格缩进，花括号沿用 `main/main.c` 的现有风格。宏和引脚常量使用 `UPPER_SNAKE_CASE`，函数与局部变量使用 `snake_case`，文件内状态优先声明为 `static`。保持硬件初始化、错误处理和资源释放路径清晰；新增依赖前先确认 ESP-IDF 或现有组件没有等价能力。仓库未配置统一格式化器，避免对无关代码做批量格式化。

## 测试与验证

当前没有自动化测试套件。每次改动至少运行 `idf.py build`；涉及硬件时，在目标板验证启动、按键、显示刷新及受影响外设，并检查串口日志无新增错误。修改引脚、I2C 地址或协议时，同步核对并更新 `README.md`。无法完成实机验证时，在 PR 中明确未验证范围。

影响项目状态的实现、修复或文档决策完成后，同步更新 `ROADMAP.md`；未验证事项不得记为完成。Launcher 分层改造应以现有 Dashboard 行为为回归基线，避免一次性搬迁全部代码。

## 提交与 Pull Request

提交信息使用中文和明确前缀：`feat:`、`fix:`、`refactor:`、`docs:` 或 `chore:`，例如 `fix: 修正 SD 卡挂载失败后的资源释放`。一次提交只处理一个主题，不提交构建产物。PR 应说明改动目的、影响的硬件与验证命令；UI 变化附截图，硬件问题附关键串口日志，并关联相关 Issue。

## 硬件与配置注意事项

本项目面向原厂硬件。GPIO12 会影响启动，GPIO34/35 仅支持输入；GPIO15/21 在 I2C 与 SugarASR UART 间复用。修改这些资源前必须核对 `README.md` 和原理图，避免引脚冲突或误驱动电机。
