# Repository Guidelines

## 首次进入项目

依次阅读 `ROADMAP.md`、`README.md` 和 `docs/project-overview.md`。前者是当前进度源，`README.md` 保存硬件事实与协议，项目概览解释代码现状和开发入口。`docs/xiaomiao_firmware_v0.1_design.md` 是目标架构方案，不代表相关模块已经实现。

## Goal 文档与历史记录

每个施工任务使用 `goals/YYYYMMDD-HHMM-<goal-slug>.md` 保存目标、边界、实现决策、检查点、验收证据和未验证范围。Goal 完成后必须保留原文，只在文档中更新状态和交付结果；不要因为功能已完成而删除。保留这些记录可以让后续 Agent 复用已确认的技术决策、验证证据和已知限制，避免重复调研或误判项目状态。当前进度和唯一状态入口仍由 `ROADMAP.md` 维护；逐条施工与验证流水的日期索引在 `goals/ROADMAP-history.md`，只记录“发生了什么、结论、施工文档路径”，细则以各 Goal 为准，不要在 `ROADMAP.md` 里重建流水记录。

## 项目结构与模块组织

- `main/main.c` 是当前 ESP32-WROVER-B 固件的主业务源文件，包含 LVGL 9.5 硬件 Dashboard（注册为 `Hardware Test` App）及屏幕、按键、ADC、I2C、蜂鸣器和 MicroSD 驱动，并持有普通固件的启动链。`main/framework/` 是 App Framework 运行时及全局 Wi-Fi 状态图标，`main/apps/<app>/` 存放业务 App（当前有占位 `Games`、静态骨架 `PC Monitor`、菜单 `Tools` 与菜单 `Settings`）；`main/services/` 已实现 `Settings Service`（默认值、校验、NVS 读写与安全回退）和 `Wi-Fi Service`（STA、扫描、重连、SoftAP + 网页配网与凭据持久化），App 不直接访问 NVS 或 `esp_wifi_*`；`bsp/` 分层尚未建立。
- `main/idf_component.yml` 固定组件依赖，`sdkconfig.defaults` 与 `sdkconfig.ci` 保存可复现的 ESP-IDF 配置。
- `GD32_firmware/` 是独立的 GD32F350 Keil 工程；当前仓库源码实现 USB CDC、UART 桥和 ESP32 自动下载控制，尚未实现 README 所述的 I2C `0x40` LED/电机从机协议。请勿把厂商库改动混入 ESP32 功能提交。
- `docs/` 存放设计说明，`README.md` 记录已确认的引脚和协议，根目录 PDF 为硬件原理图。
- `build/`、`managed_components/`、本地 `sdkconfig` 均为生成或环境文件，不应提交。

## 构建、烧录与开发命令

先加载 ESP-IDF 环境（Windows CMD/PowerShell 运行官方 `export.bat` / `export.ps1`；MSYS/Git Bash 会因 `MSYSTEM` 变量被拒绝，需在干净 shell 中执行），并在仓库根目录执行：

```bash
idf.py set-target esp32   # 首次配置目标芯片
idf.py build              # 配置、解析依赖并编译固件
idf.py -p COM5 flash      # 将固件烧录到指定串口
idf.py -p COM5 monitor    # 查看启动日志和运行状态
```

可复现的全新基线构建（不依赖根目录 `build/` 与本地 `sdkconfig`，配置只来自 `sdkconfig.defaults` 和 `sdkconfig.ci`）：

```bash
idf.py -B .tmp/build-baseline/build -D SDKCONFIG=$(pwd)/.tmp/build-baseline/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" set-target esp32
idf.py -B .tmp/build-baseline/build -D SDKCONFIG=$(pwd)/.tmp/build-baseline/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" build
```

### Agent 与人工验证分工

- 为节省开发时间，ESP-IDF 编译、`set-target`、烧录、串口监视以及目标板按键和外设验证统一由人工执行。Agent 不得主动运行 `idf.py build`、`idf.py set-target`、`idf.py flash`、`idf.py monitor`、`esptool` 或直接占用设备串口。
- Agent 负责提供准确的人工验证命令和预期结果，执行源码与 diff 静态检查，并复核人工提供的命令输出、串口日志、截图和实机结果。
- 缺少人工验证证据时，Agent 必须将对应项目标记为“未验证”，不得根据历史缓存、构建产物或推测宣称通过。

环境要求与已验证事实：

- 本仓库以 ESP-IDF 6.1 为唯一目标版本；`idf.py --version` 应输出 `ESP-IDF v6.1`。
- 若安装使用非默认布局（venv 不在 `<IDF_TOOLS_PATH>/python_env/` 下），以进程级环境变量 `IDF_TOOLS_PATH` 和 `IDF_PYTHON_ENV_PATH` 指向实际安装位置，不修改系统环境。
- `PATH` 中的 `idf.py.exe`（idf-exe 包装器）`--version` 显示的是包装器自身版本；确认 IDF 版本应使用 `python "$env:IDF_PATH\tools\idf.py" --version`。
- 2026-09-19 已按上述命令完成全新配置构建与二次构建验证，结果见 `goals/20260919-1834-build-baseline.md`。

GD32 固件使用 Keil 打开 `GD32_firmware/Project/MDK-ARM/cdc_acm.uvprojx`。`go.py` 是原厂 MicroPython 环境的硬件探查脚本，不属于 ESP-IDF 构建流程；运行前按脚本提示断开电机。

## 编码风格与命名约定

C 代码使用 4 空格缩进，花括号沿用 `main/main.c` 的现有风格。宏和引脚常量使用 `UPPER_SNAKE_CASE`，函数与局部变量使用 `snake_case`，文件内状态优先声明为 `static`。保持硬件初始化、错误处理和资源释放路径清晰；新增依赖前先确认 ESP-IDF 或现有组件没有等价能力。仓库未配置统一格式化器，避免对无关代码做批量格式化。

## 测试与验证

当前没有自动化测试套件。每次改动至少需要人工执行 `idf.py build`；涉及硬件时，由人工在目标板验证启动、按键、显示刷新及受影响外设，并检查串口日志无新增错误。Agent 只执行源码、配置、diff 等静态检查并复核人工验证证据，不主动编译、烧录、监视串口或操作目标板。修改引脚、I2C 地址或协议时，同步核对并更新 `README.md`。无法完成人工验证时，在 Goal、`ROADMAP.md` 和 PR 中明确未验证范围。

影响项目状态的实现、修复或文档决策完成后，同步更新 `ROADMAP.md`；未验证事项不得记为完成。Launcher 分层改造应以现有 Dashboard 行为为回归基线，避免一次性搬迁全部代码。

## 提交与 Pull Request

提交信息使用中文和明确前缀：`feat:`、`fix:`、`refactor:`、`docs:` 或 `chore:`，例如 `fix: 修正 SD 卡挂载失败后的资源释放`。一次提交只处理一个主题，不提交构建产物。PR 应说明改动目的、影响的硬件与验证命令；UI 变化附截图，硬件问题附关键串口日志，并关联相关 Issue。

## 硬件与配置注意事项

本项目面向原厂硬件。GPIO12 会影响启动，GPIO34/35 仅支持输入；GPIO15/21 在 I2C 与 SugarASR UART 间复用。修改这些资源前必须核对 `README.md` 和原理图，避免引脚冲突或误驱动电机。
