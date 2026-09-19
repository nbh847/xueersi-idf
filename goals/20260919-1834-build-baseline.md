# Goal：恢复可复现的 ESP-IDF 构建基线

## 元信息

- 对应节点：节点 0“构建基线恢复”
- 状态：已完成（2026-09-19；构建、烧录和基础交互验证通过，未覆盖全部外设回归）
- 依赖：无功能依赖；执行前必须保护当前未提交的 `dependencies.lock` 修改
- 后续目标：节点 1“App Framework”

## 目标与预期行为

在不依赖现有 `build/` 缓存的情况下，为本仓库建立可复现的 ESP-IDF 6.1 构建入口。新的终端会话应能加载正确工具链，在独立临时构建目录中完成 ESP32 配置、依赖解析和固件编译，并输出可烧录的 `xiaomiao.bin`。

本 Goal 只恢复构建基线，不开发 Launcher、App Framework 或任何硬件功能。

## 已知事实

- 项目目标为 ESP32-WROVER-B，ESP-IDF 版本为 6.1，LVGL 依赖固定为 9.5.0。
- 当前缓存构建记录的 IDF 路径是 `D:/esp/v6.1/esp-idf`，Python 是 `D:/Espressif/tools/python/v6.1/venv/Scripts/python.exe`。
- 当前直接执行 `<IDF_ROOT>/export.ps1` 时，会因未设置工具目录而回退查找不存在的用户级 `python_env`。
- 直接调用缓存记录的 Ninja 已通过增量构建，但这不能证明全新配置构建有效。
- 工作区存在一项来源未确认的 `dependencies.lock` 修改，不得覆盖、还原或混入本 Goal。

## 范围边界

允许修改：

- ESP-IDF 构建入口及必要的项目级配置。
- `.devcontainer/`、`sdkconfig.defaults`、`sdkconfig.ci`、根目录和 `main/` 的 CMake 文件，但仅限全新构建暴露出的明确问题。
- `AGENTS.md`、`README.md`、`ROADMAP.md`、`docs/project-overview.md` 及本 Goal 文档。
- 必要时新增最小构建辅助脚本；新增前必须确认 ESP-IDF 官方入口无法直接满足要求。

禁止修改：

- `main/main.c` 的业务逻辑和硬件行为。
- `GD32_firmware/`、硬件协议、引脚定义和原理图。
- LVGL 或其他依赖版本，除非有明确构建证据并另行确认。
- 当前未提交的 `dependencies.lock` 修改。
- 现有 `build/` 内容；全新验证统一使用 `.tmp/build-baseline/`。
- 未经明确授权，不安装全局依赖、不修改系统环境变量或 PowerShell 执行策略。

## 已确定的实施决策

1. ESP-IDF 6.1 是本 Goal 的唯一目标版本，不使用 `latest` 结果替代验收。
2. 优先修复进程级环境发现，例如核对 `IDF_TOOLS_PATH` 和 ESP-IDF 安装元数据；不得把本机绝对路径写入可提交配置。
3. 全新构建使用 `.tmp/build-baseline/build/`，生成的独立 `sdkconfig` 也放在同一任务临时目录。
4. 配置以 `sdkconfig.defaults` 和 `sdkconfig.ci` 为输入，不依赖根目录未提交的本地 `sdkconfig`。
5. 不通过注释错误、降低检查要求或继续依赖旧缓存来获得“通过”。
6. 若必须修改系统级 ESP-IDF 安装，停止执行并向用户说明具体修改、风险和恢复方式，取得授权后继续。

## 执行检查点

### 检查点 1：保护工作区并定位环境错配（已通过）

- 执行前 `git status --short`：`M ROADMAP.md`、`M dependencies.lock`、`M docs/xiaomiao_firmware_v0.1_design.md`、`?? goals/`；`dependencies.lock` 哈希 `e9da2f2a`。
- 根因链：`IDF_TOOLS_PATH` 未设置 → `activate.py` 回退 `~/.espressif`；实际 venv 使用 Python 3.14.7，位于 `<IDF_TOOLS_PATH>/tools/python/v6.1/venv`（非 `python_env/` 模板布局），venv 目录名模板随运行时 Python 版本变化（`idf_tools.py:103`）；MSYS `MSYSTEM` 泄漏进 cmd/PowerShell 触发拒绝；`espidf.constraints.v6.1.txt` 错位于 `<IDF_TOOLS_PATH>/tools/` 子目录，而 `idf_tools.py:2679` 要求位于工具目录根。
- 修复：进程级设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH` 指向实际安装位置；在 CMD/PowerShell 中清除 `MSYSTEM`；经授权将 constraints 文件复制到工具目录根（原文件保留，可删除恢复）。

验证方式：在新的子 PowerShell 进程中加载环境，`idf.py --version` 返回 ESP-IDF 6.1。
验证结果：`python "$env:IDF_PATH\tools\idf.py" --version` 输出 `ESP-IDF v6.1`，exit=0。PATH 中 `idf.py.exe`（idf-exe 1.0.3 包装器）`--version` 显示包装器自身版本，不作为验收依据。

### 检查点 2：验证全新配置构建（已通过）

- 构建目录 `.tmp/build-baseline/build/`，独立 `sdkconfig` 位于 `.tmp/build-baseline/sdkconfig`，输入为 `sdkconfig.defaults;sdkconfig.ci`。
- `set-target esp32` exit=0；完整 `build` exit=0（1833 个 ninja 目标，全新编译）。
- 产物：`bootloader.bin` 30496 字节、`partition-table.bin` 3072 字节、`xiaomiao.elf` 9420368 字节、`xiaomiao.bin` 0xa2ac0（666304）字节。
- 分区尺寸检查通过：应用分区 0x100000，剩余 0x5d540（36%）。

验证方式：保存实际命令、ESP-IDF 版本、退出码、固件大小和剩余应用分区空间。
验证结果：命令与日志见 `.tmp/build-baseline/set-target.log`、`.tmp/build-baseline/build.log`（任务临时目录，不入库）。

### 检查点 3：验证重复执行与工作区完整性（已通过）

- 同一临时构建目录二次构建 exit=0。
- `dependencies.lock` 哈希执行前后均为 `e9da2f2a`，原有修改未被覆盖或混入。
- `git diff --check` exit=0；工作区改动未超出本 Goal 授权文件（`AGENTS.md`、`docs/project-overview.md`、`ROADMAP.md`、本文件）。

### 检查点 4：交付构建入口与文档（已通过）

- 可移植构建入口与环境要求已写入 `AGENTS.md`（不含本机绝对路径）与 `docs/project-overview.md`。
- 验证时间、命令、结果与限制已记录于 `ROADMAP.md`；本文件状态与各检查点结果已更新。
- 限制：固件未烧录、未在目标板运行；本机 ESP-IDF 安装为非默认布局，新环境需按 `AGENTS.md` 说明设置进程级 `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH`。

## 失败路径与处理要求

- Python 环境不存在：先核对项目实际安装位置和 `IDF_TOOLS_PATH`，不得直接安装全局 Python 包。
- 依赖需要联网下载：先确认 `dependencies.lock` 和本地组件缓存；确需联网时按权限门禁申请，不伪造离线成功。
- `dependencies.lock` 发生变化：立即对比执行前内容；无法证明属于本 Goal 时停止，不覆盖用户修改。
- 全新配置失败但缓存构建成功：Goal 保持未完成，报告首个根因错误，不以 Ninja 增量构建代替验收。
- 固件或 bootloader 超出分区：记录精确尺寸并定位原因，不扩大分区或降低功能作为临时绕过。
- 无法实机烧录：本 Goal 可以完成构建基线，但必须明确“未验证烧录与运行”；不得声称硬件验证通过。

## 验收标准

- [x] 新终端会话中可确认使用 ESP-IDF 6.1。
- [x] 不读取现有 `build/` 缓存即可完成目标 `esp32` 的配置和构建。
- [x] 新生成的 bootloader、partition table、`xiaomiao.elf` 和 `xiaomiao.bin` 均存在。
- [x] bootloader 与应用分区尺寸检查通过，并记录精确结果。
- [x] 同一命令第二次执行成功。
- [x] `dependencies.lock` 的原有修改未被覆盖或混入交付。
- [x] 未修改 `main/main.c`、GD32 固件或硬件行为。
- [x] 构建文档与实际验证命令一致，`ROADMAP.md` 已同步。
- [x] `git diff --check` 通过；未引入构建产物或本机绝对路径到可提交项目配置，机器诊断证据仅保存在被忽略的 `.tmp/` 中。

主 Agent 于 2026-09-19 20:25 在独立临时构建目录重复完成全新配置、完整构建和二次构建，确认 ESP-IDF v6.1、1833 个构建目标、分区检查及 `dependencies.lock` 未变化。

### 补充实机验证（已完成）

2026-09-19 20:35，用户在目标设备上完成烧录和基础操作验证：固件可正常烧录和启动，15 个 Dashboard 页面均可翻页，A/B 键操作正常。LED、电机、MicroSD、MPU6050 等未在本次补充验证中逐项覆盖，保留给后续 Hardware Test App 回归。

## 交付内容

- 可复现的 ESP-IDF 6.1 构建入口，优先使用官方命令和项目现有配置。
- 必要且最小的项目配置修复。
- 构建命令、环境要求、结果与限制的文档更新。
- 本 Goal 的检查点记录、验证证据和未解决问题。
- 更新后的 `ROADMAP.md`；只有全部必要验收通过后，才能将节点 0 标记完成并开始节点 1。

## 委派约束

本 Goal 当前不计划委派。若后续明确授权使用子 Agent，必须先在本文件追加子任务编号、修改范围、禁止范围、依赖、验收命令和预期结果；主 Agent 必须复核实际 diff 与验证证据。
