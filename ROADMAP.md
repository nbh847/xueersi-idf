# 项目路线图

本文件记录项目当前状态。硬件事实与协议查阅 `README.md`，代码结构与开发入口查阅 `docs/project-overview.md`，目标架构查阅 `docs/xiaomiao_firmware_v0.1_design.md`。

## 当前状态

- ESP32 固件基于 ESP-IDF 6.1 与 LVGL 9.5，当前入口为 `main/main.c` 的单文件 Hardware Dashboard。
- Dashboard 已覆盖 15 个页面：光照、热敏、运动、LED1、LED2、蜂鸣器、电机 1、电机 2、MicroSD、GPIO25、GPIO26、ADC32、ADC33、系统和 About。
- ESP32 端已接入 ST7735、六键输入、ADC、LEDC、SPI MicroSD、GD32 `0x40` 与 MPU6050 `0x68`。
- GD32 工程当前只确认实现 USB CDC、USART1 桥和 ESP32 IO0/EN 控制；I2C `0x40` LED/电机从机协议未在仓库源码中实现。
- Launcher、App Framework、Service 和 BSP 分层仍处于设计阶段。

## 进行中

- 以现有 15 页 Dashboard 为回归基线，规划 v0.1 Launcher 与 App Framework 的小步落地顺序。
- 核对 GD32 实机固件与 README 中 `0x40` 协议的真实对应关系。

## 下一步

- 为硬件访问提取最小 BSP/Service 接口，先保持 UI 与外部行为不变。
- 建立 App Registry 与 Launcher 骨架，再把 Dashboard 封装为 Hardware Test App。
- 增加至少可在 CI 执行的 ESP-IDF 构建检查；对可分离逻辑补充单元测试。
- 实机验证 MicroSD 初始化及 GPIO22 冲突，并据结果修正文档或驱动。

## 待确认与已知风险

- `docs/xiaomiao_firmware_v0.1_design.md` 记录过 `sdmmc_card_init failed` 和 GPIO22 冲突，但本次仅做源码与构建环境检查，尚未实机复现。
- README 记录的 GD32 LED/电机协议已被 ESP32 代码使用，但当前 GD32 工程缺少对应实现，双 MCU 联调结果待确认。
- 当前 Windows 工作站的 `D:\esp\v6.1\esp-idf\export.ps1` 指向不存在的用户级 Python 环境；缓存构建实际使用 `D:\Espressif\tools\python\v6.1\venv`。后续需要修复本机 ESP-IDF 环境后再验证全新配置构建。

## 最近完成

- 2026-09-19 18:20：将仓库内所有 `.vscode/` 目录列为本地配置并取消 Git 跟踪，本地文件继续保留；`.devcontainer/` 仍作为可复用开发环境配置保留。
- 2026-09-19 18:18：补充 Git 忽略规则，排除任务临时目录、本地备份及 AI/IDE 助手状态。
- 2026-09-19 18:12：建立 `AGENTS.md`、项目概览和路线图，明确当前实现、目标架构、双 MCU 边界与验证入口。

## 最近验证

- 2026-09-19 18:20：使用 `git ls-files` 和 `git check-ignore` 核对根目录及 GD32 子目录中的 `.vscode/` 文件，确认取消跟踪后由统一规则排除。
- 2026-09-19 18:18：使用 `git check-ignore` 核对新增规则，确认本地工具状态与临时路径被排除。
- 2026-09-19 18:15：直接调用构建缓存记录的 Ninja 完成增量构建和分区尺寸检查；`xiaomiao.bin` 为 `0xa2ac0` 字节，应用分区剩余 36%。该结果不等同于已验证全新配置构建。
- 2026-09-19 18:12：完成仓库结构、ESP32 入口与依赖、GD32 工程、默认配置和现有设计文档的静态核对；未执行实机验证。
