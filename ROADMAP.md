# 项目路线图

本文件记录项目当前状态。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。每个 Goal 的施工文档保留在 `goals/`，用于沉淀范围、决策、验收证据和限制；完成后不删除，当前状态以本文件为准。

## 当前状态

- ESP32 固件基于 ESP-IDF 6.1 与 LVGL 9.5，当前入口为 `main/main.c` 的单文件 Hardware Dashboard。
- Dashboard 已覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统和 About。
- ESP32 端已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。
- GD32 工程当前只确认实现 USB CDC、USART1 桥和 ESP32 IO0/EN 控制；I2C `0x40` LED/电机从机协议未在仓库源码中实现。
- Launcher、App Framework、Service 和 BSP 分层仍处于设计阶段。App Framework 核心运行时（App 描述、Registry、Manager）已于节点 1 实现，位于 `main/framework/`；Launcher、Navigation 和 App 切换尚未实现。

## 当前开发节点

- 节点 0“构建基线恢复”已完成（2026-09-19，全新配置构建、烧录和基础交互验证通过；未覆盖全部外设回归）。施工文档为 `goals/20260919-1834-build-baseline.md`。
- 节点 1“App Framework”已完成（2026-09-20，框架代码、自测代码、普通构建、自测构建、自测固件 PASS 标记和普通固件 Dashboard 回归均通过）。施工文档为 `goals/20260919-2037-app-runtime.md`。
- 下一个开发节点为节点 2“Navigation”，尚未开始。
- GD32 实机固件与 README 中 `0x40` 协议的对应关系仍在待确认状态，不阻塞不依赖 LED、电机的 v0.1 框架开发。

## 有序开发节点

节点定义、交付和验收标准见 `docs/xiaomiao_firmware_v0.1_design.md` 第 15 节；此处只维护唯一进度状态。

- [x] 节点 0：构建基线恢复。
- [x] 节点 1：App Framework。
- [ ] 节点 2：Navigation。
- [ ] 节点 3：Launcher。
- [ ] 节点 4：Hardware Test App。
- [ ] 节点 5：Games 占位 App。
- [ ] 节点 6：PC Monitor UI。
- [ ] 节点 7：Tools App。
- [ ] 节点 8：Settings UI。
- [ ] 节点 9：Settings Service 与 NVS。
- [ ] 节点 10：Wi-Fi Service。
- [ ] 节点 11：PC Monitor 通信。
- [ ] 节点 12：Audio Service。
- [ ] 节点 13：首个正式游戏。
- [ ] 节点 14：Storage Service。
- [ ] 节点 15：Assets 与文件系统。
- [ ] 节点 16：GD32 `0x40` 协议补全。

## 下一步

1. 实施节点 2“Navigation”，建立统一导航和返回机制；以节点 1 的 App Framework 为基础，不提前开发 Launcher。
2. 节点 2 验收后再实施节点 3“Launcher”。
3. 在节点 4“Hardware Test App”中逐项回归 LED、电机、MicroSD、MPU6050 等外设。

## 待确认与已知风险

- `docs/xiaomiao_firmware_v0.1_design.md` 记录过 `sdmmc_card_init failed` 和 GPIO22 冲突，但本次仅做源码与构建环境检查，尚未实机复现。
- README 记录的 GD32 LED/电机协议已被 ESP32 代码使用，但当前 GD32 工程缺少对应实现，双 MCU 联调结果待确认。
- 本机 ESP-IDF 安装为非默认布局：Python venv 3.14.7 位于 `<IDF_TOOLS_PATH>/tools/python/v6.1/venv`（不在 `<IDF_TOOLS_PATH>/python_env/` 下），需进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 后再运行官方 `export.ps1`；constraints 文件曾错位于工具目录的 `tools/` 子目录，2026-09-19 经授权复制到工具目录根修复（原文件保留）。MSYS/Git Bash 中执行 export 会因 `MSYSTEM` 变量被拒绝。PATH 中的 `idf.py.exe`（idf-exe 1.0.3 包装器）`--version` 显示包装器自身版本，确认 IDF 版本需用 `python "$env:IDF_PATH\tools\idf.py" --version`。
- 节点 0 已完成基础实机验证；LED、电机、MicroSD、MPU6050 等完整外设回归仍待节点 4。

## 最近完成

- 2026-09-20 07:33：完成节点 1“App Framework”：在 `main/framework/` 实现 `xiaomiao_app_t` 描述、静态 Registry（容量 16）、生命周期 Manager（init_all/open/close/current）和开发自测；通过 CMake option `XIAOMIAO_FRAMEWORK_SELF_TEST` 接入；普通构建和自测构建均通过 ESP-IDF 6.1 编译，自测固件串口输出 `APP_FRAMEWORK_SELF_TEST: PASS`，普通固件 15 页 Dashboard 翻页及 A/B 键回归通过。
- 2026-09-19 20:50：将 `dependencies.lock` 独立同步到项目的 ESP-IDF 6.1 基线（IDF 6.1.0、锁文件格式 3.0.0）；LVGL 仍为 9.5.0，`manifest_hash` 未变化。
- 2026-09-19 20:37：创建节点 1“App Framework”施工文档，明确框架接口、静态 Registry、生命周期 Manager、自测方式、范围边界和验收标准；尚未启动执行。
- 2026-09-19 20:35：完成节点 0 补充实机验证，固件可正常烧录和启动，15 个 Dashboard 页面均可翻页，A/B 键操作正常；未将未测试外设标记为已验证。
- 2026-09-19 20:23：修正节点 0 Goal 与路线图中的机器绝对路径表述，改用环境变量占位符；保留 `.tmp/` 中的本机诊断证据。
- 2026-09-19 19:52：完成节点 0“构建基线恢复”：定位环境错配根因（`IDF_TOOLS_PATH` 未设置、venv 非默认布局、`MSYSTEM` 泄漏、constraints 文件错位），以进程级环境变量修复并经授权复制 constraints 文件到工具目录根；在 `.tmp/build-baseline/` 完成全新配置构建与二次构建验证；构建入口与环境要求写入 `AGENTS.md` 与 `docs/project-overview.md`。
- 2026-09-19 18:34：创建节点 0 的 Goal 施工文档，明确目标、边界、检查点、失败路径、验收标准和交付内容。
- 2026-09-19 18:27：将后续功能拆分为节点 0～16，明确版本归属、依赖顺序、交付内容和验收结果；进度统一由本文件维护。
- 2026-09-19 18:20：将仓库内所有 `.vscode/` 目录列为本地配置并取消 Git 跟踪，本地文件继续保留；`.devcontainer/` 仍作为可复用开发环境配置保留。
- 2026-09-19 18:18：补充 Git 忽略规则，排除任务临时目录、本地备份及 AI/IDE 助手状态。
- 2026-09-19 18:12：建立 `AGENTS.md`、项目概览和路线图，明确当前实现、目标架构、双 MCU 边界与验证入口。

## 最近验证

- 2026-09-20 08:08（节点 1 主 Agent 复核）：逐项核对 Goal 验收条款、实际源码、CMake 接入和 Git 改动范围，未发现阻塞缺陷；`git diff --check` 无错误。普通构建、自测构建、烧录、自测 PASS 标记和 Dashboard 实机回归采用已记录证据及用户确认的成功结果，本次未重复构建或烧录。
- 2026-09-20 07:33（节点 1 验收）：普通构建 exit=0，烧录后串口出现 `Xiaomiao LVGL 9.5 dashboard boot`，15 页翻页及 A/B 键正常。自测构建（`.tmp/build-selftest/`，`XIAOMIAO_FRAMEWORK_SELF_TEST=ON`）exit=0，`xiaomiao.bin` 0xa47f0 字节，应用分区 36% free；烧录后串口出现 `APP_FRAMEWORK_SELF_TEST: PASS`。未修改 GD32、硬件协议、依赖版本或 `dependencies.lock`。
- 2026-09-19 20:50：使用项目实际 Python 环境直接调用 `tools/idf.py --version`，输出 `ESP-IDF v6.1.0`；当前 `dependencies.lock` 与节点 0 隔离构建保存的锁快照哈希一致。本次未重新执行完整固件构建，以避免将工作区其他未提交源码改动混入依赖锁定验证；节点 0 已记录同一锁快照的完整构建证据。
- 2026-09-19 20:37：核对节点 1 Goal 与 v0.1 设计文档、当前 `main/main.c` 单文件结构和现有 CMake 入口，确认本 Goal 未扩大到 Navigation、Launcher、硬件迁移或 GD32 固件。
- 2026-09-19 20:35（节点 0 实机验证）：目标设备烧录成功并正常运行；15 个 Dashboard 页面翻页、A/B 键操作通过。未覆盖：LED、电机、MicroSD、MPU6050 等逐项外设测试。
- 2026-09-19 20:25（主 Agent 独立复核）：在新的临时构建目录重新加载 ESP-IDF v6.1，直接调用 `tools/idf.py` 完成全新配置和 1833 个目标的完整构建；产物与分区尺寸均通过，同一目录第二次构建 exit=0，`dependencies.lock` 哈希保持不变。当时未包含实机验证，后续结果见 20:35 记录。
- 2026-09-19 20:23（节点 0 文档复核）：确认 Goal 验收条款与可提交文档一致；本机绝对路径仅保留在被忽略的 `.tmp/` 证据中。
- 2026-09-19 19:52（节点 0 验收）：环境为 ESP-IDF v6.1（`python "$env:IDF_PATH\tools\idf.py" --version`，exit=0）。在 `.tmp/build-baseline/` 以 `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci"` 完成 `set-target esp32`（exit=0）与完整 `build`（exit=0，1833 个 ninja 目标），产物 `bootloader.bin` 30496 字节、`partition-table.bin` 3072 字节、`xiaomiao.elf` 9420368 字节、`xiaomiao.bin` 0xa2ac0 字节；分区检查通过，应用分区 0x100000 剩余 0x5d540（36%）。同一命令二次构建 exit=0。`dependencies.lock` 全程哈希 `e9da2f2a` 不变，`git diff --check` 通过，工作区改动未超出本 Goal 授权文件。未验证：烧录与目标板运行。
- 2026-09-19 18:34：核对节点 0 Goal 文档与 `AGENTS.md`、项目概览及路线图，确认未扩大到业务代码、GD32 固件或系统级配置修改。
- 2026-09-19 18:27：交叉核对 `ROADMAP.md`、项目概览和 v0.1 设计目标，确认节点 1～8 覆盖 v0.1 验收范围，节点 9～15 对应 v0.2～v0.5，节点 16 保持为独立硬件支线。
- 2026-09-19 18:20：使用 `git ls-files` 和 `git check-ignore` 核对根目录及 GD32 子目录中的 `.vscode/` 文件，确认取消跟踪后由统一规则排除。
- 2026-09-19 18:18：使用 `git check-ignore` 核对新增规则，确认本地工具状态与临时路径被排除。
- 2026-09-19 18:15：直接调用构建缓存记录的 Ninja 完成增量构建和分区尺寸检查；`xiaomiao.bin` 为 `0xa2ac0` 字节，应用分区剩余 36%。该结果不等同于已验证全新配置构建。
- 2026-09-19 18:12：完成仓库结构、ESP32 入口与依赖、GD32 工程、默认配置和现有设计文档的静态核对；未执行实机验证。
