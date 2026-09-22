# 节点 14：Storage Service 与 MicroSD 完整验证

## 元信息

- 对应节点：节点 14“Storage Service”
- 状态：已完成（2026-09-22 15:38；CP1～CP4 实施与静态复核通过，CP5 人工构建、烧录和九个实机场景全部通过）
- 创建时间：2026-09-22 09:38（北京时间）
- 前置条件：节点 0～11 已完成；节点 12、13 已确认推迟，不阻塞本节点；MicroSD GPIO22 冲突修复已于 2026-09-21 通过实机验证
- 前置施工文档：`goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 人工验证条件：项目负责人已于 2026-09-22 确认 SD 卡到货；其卡片、文件系统和共享 SPI 链路是否正常，以本 Goal 的实机验收结果为准
- 后续目标：节点 15“Assets 与文件系统”只能消费本节点公开的挂载状态和固定挂载点，不重新实现 SD 驱动生命周期

## 目标与预期行为

建立独立的 `Storage Service`，把当前位于 `main/main.c` 的 MicroSD 探测、SDSPI 设备创建、FATFS 挂载、卸载、错误状态和资源清理迁移到 `main/services/`。普通启动在共享 SPI2 总线建立后尝试挂载一次；有可用 FAT 文件系统时发布卡名、容量和挂载状态，没有卡、卡不响应、文件系统不支持或底层初始化失败时只记录确定性错误并继续进入 Launcher。

Hardware Test 的 MicroSD 页面改为只调用 Storage Service 公开接口：读取快照显示状态，A 在未挂载时重试挂载，B 在已挂载时安全卸载。页面、其他 App 和未来节点 15 不得直接持有 `sdmmc_card_t *`、调用 `sdspi_host_*`／`esp_vfs_fat_*`，也不得自行操作 GPIO22。

本节点必须保留已经验证通过的 GPIO22 修复时序：失败清理和成功卸载均在移除 SDSPI 设备前复位 CS 所有权。重复无卡重试不得重新出现 `gpio: conflict found for GPIO[22]`。

## 当前基线与证据

- TFT 与 MicroSD 共用 SPI2：SCK GPIO18、MOSI GPIO23、MISO GPIO19；TFT CS 为 GPIO5，SD CS 为 GPIO22。
- `lcd_init()` 调用 `spi_bus_initialize(SPI2_HOST, ...)` 并创建 TFT panel IO；Storage Service 不能重复初始化或释放共享 SPI2 总线。
- `main/main.c` 当前固定 SD 时钟上限为 10 MHz，挂载点为 `/sdcard`，`format_if_mount_failed=false`，`max_files=3`。
- 当前 `sd_try_mount()` 已手动执行 `host.init()`、`sdspi_host_init_device()`、`sdmmc_card_init()` 和 `esp_vfs_fat_mount_initialized()`；失败时复位 GPIO22、移除 SDSPI 设备并释放 `sdmmc_card_t`。
- 当前 `sd_unmount()` 在 `esp_vfs_fat_sdcard_unmount()` 前复位 GPIO22，成功后清除卡指针、卡名和容量。
- 2026-09-21 21:56 的人工日志证明：连续 9 次 `sdmmc_card_init failed (0x107)` 均未再出现 GPIO22 冲突警告，Hardware Test 关闭、重开和返回 Launcher 正常。
- 上述证据只覆盖无响应卡路径。正常卡挂载、卸载、重新挂载和断电重启尚无证据，本节点负责闭环。

## 已确认实现决策

1. **Service 拥有完整 SD 生命周期。** `sdmmc_card_t *`、SDSPI 设备句柄、挂载状态、卡名、容量和最后错误只保存在 Storage Service 私有实现中。
2. **`main` 保留板级接线配置。** GPIO22、SPI2 和 10 MHz 等硬件值由 `app_main()` 通过初始化配置传入；Service 不在实现文件中重新硬编码另一套引脚定义。当前没有 BSP 层，本节点不顺带建立整个 BSP。
3. **固定挂载点 `/sdcard`。** 作为节点 15 的稳定接口，不开放任意路径、多个卡槽或运行时修改挂载点。
4. **启动时尝试挂载一次。** Storage Service 在 `lcd_init()` 已建立共享 SPI2 总线之后初始化并尝试挂载；失败只记警告，启动继续。不得在 Settings、Wi-Fi 或 Agent Service 之前触碰尚未初始化的 SPI2。
5. **同步、串行操作。** 首版 `mount()`／`unmount()` 为同步接口，由 Service 内部互斥保护；不新增 FreeRTOS Task、事件总线或异步命令队列。不得从 ISR 调用。
6. **幂等语义。** 已挂载时再次 `mount()` 返回 `ESP_OK` 且不重复创建设备；未挂载时 `unmount()` 返回确定性错误且不改坏快照；初始化重复调用不重复创建资源。
7. **快照而非内部指针。** App 通过复制式快照读取状态、卡名、容量和最后错误，不能取得可变内部对象或裸卡句柄。
8. **保留 GPIO22 修复。** 每次挂载前清理遗留所有权；所有已创建设备的失败路径和正常卸载路径，必须先执行 `gpio_reset_pin(cs_gpio)`，再移除 SDSPI 设备。
9. **不自动格式化。** `format_if_mount_failed` 固定为 `false`。挂载失败不得格式化、擦除、修复或写入 SD 卡，也不得覆盖卡内已有文件。
10. **不拥有共享 SPI 总线。** Service 只添加和移除自己的 SDSPI 设备，绝不调用 `spi_bus_initialize()`、`spi_bus_free()`，也不删除 TFT panel IO。
11. **节点 14 不提供通用文件 API。** 文件打开、读取、写入、目录遍历、资源索引、缓存和 Assets 加载属于节点 15；本节点只保证 `/sdcard` 的生命周期和状态契约。
12. **不宣称热拔插安全。** 卡已挂载时禁止直接拔卡；人工测试必须先在 MicroSD 页按 B 成功卸载，再移除卡。未挂载状态下插卡后可按 A 重试。

## 公开接口契约

建议新增：

```text
main/services/xiaomiao_storage_service.h
main/services/xiaomiao_storage_service.c
```

公开头文件只暴露业务状态、初始化配置和操作函数，不暴露 `sdmmc_card_t` 或 SDSPI 设备句柄。建议接口：

```c
#define XIAOMIAO_STORAGE_MOUNT_POINT "/sdcard"
#define XIAOMIAO_STORAGE_CARD_NAME_MAX 24

typedef enum {
    XIAOMIAO_STORAGE_UNINITIALIZED = 0,
    XIAOMIAO_STORAGE_UNMOUNTED,
    XIAOMIAO_STORAGE_MOUNTED,
    XIAOMIAO_STORAGE_ERROR,
} xiaomiao_storage_state_t;

typedef struct {
    int host_id;
    int cs_gpio;
    uint32_t max_freq_khz;
} xiaomiao_storage_config_t;

typedef struct {
    xiaomiao_storage_state_t state;
    bool mounted;
    char card_name[XIAOMIAO_STORAGE_CARD_NAME_MAX];
    uint64_t capacity_bytes;
    esp_err_t last_error;
} xiaomiao_storage_snapshot_t;

esp_err_t xiaomiao_storage_service_init(const xiaomiao_storage_config_t *config);
esp_err_t xiaomiao_storage_mount(void);
esp_err_t xiaomiao_storage_unmount(void);
void xiaomiao_storage_get_snapshot(xiaomiao_storage_snapshot_t *snapshot);
```

实现时允许在不改变语义的前提下调整命名或字段类型，但必须在本 Goal 的“口径差异与实现记录”中说明。以下语义固定：

- `init(NULL)` 和非法 host、CS、频率返回 `ESP_ERR_INVALID_ARG`，不得创建半初始化资源。
- `init()` 建立 Service 状态并尝试首次挂载；首次挂载失败时返回原始 `esp_err_t`，快照进入 `ERROR`，但 Service 仍保持可重试。
- `mount()` 成功后才一次性提交 `MOUNTED`、卡名、容量和 `ESP_OK`；任一步失败均清理临时资源并提交 `ERROR` 与原始错误。
- `unmount()` 只有底层卸载成功后才清除已挂载快照；失败时保留当前卡信息并记录错误，不能谎报已卸载。
- `get_snapshot()` 对空指针安全返回；复制过程必须得到一份自洽快照。
- 卡名必须保证 NUL 结尾；容量使用 64 位字节数保存，由 UI 再格式化为 MiB，避免大容量卡截断。

## 启动链与 UI 接入

普通固件启动顺序调整为：

```text
Settings Service
  -> Wi-Fi Service
  -> Agent Service
  -> 按键初始化
  -> lcd_init()：初始化共享 SPI2 与 TFT
  -> Storage Service init + 首次挂载尝试（失败只警告）
  -> ADC／I2C／LEDC 等硬件初始化
  -> LVGL、全局图标、Launcher
```

Hardware Test 的 MicroSD 页面：

- 刷新时读取一次 Storage Service 快照，不再读取 `board_state_t` 的 SD 字段。
- `MOUNTED` 时显示卡名和容量，提示 `B unmount`。
- 未挂载或错误时显示 `NO CARD` 与最后错误摘要，提示 `A rescan`。
- A 只调用 `xiaomiao_storage_mount()`；B 只调用 `xiaomiao_storage_unmount()`。
- 页面不得 include SDSPI、SDMMC、FATFS 或 GPIO 驱动头文件。
- 迁移完成后从 `board_state_t` 移除 `sd_mounted`、`sd_name`、`sd_mb`、`last_sd_err`，从 `main.c` 移除私有卡指针和原 SD 生命周期函数。

## 范围边界

### 本节点必须完成

- 新增 Storage Service 公开头文件与私有实现，并加入 `main/CMakeLists.txt` 的 `SERVICE_SRCS`。
- 迁移现有挂载、卸载、错误快照与 GPIO22 清理逻辑，保持 10 MHz、`/sdcard`、`max_files=3` 和禁止自动格式化。
- 将普通启动和 Hardware Test MicroSD 页面接入 Service。
- 验证共享 SPI2 上 TFT 显示与 SD 操作互不破坏，挂载失败不阻塞 Launcher。
- 覆盖无卡冷启动、插卡后重试、带卡冷启动、卸载、移除、重新插入、重新挂载和断电重启。
- 重复失败路径不得出现 GPIO22 冲突警告；成功路径不得泄漏 SDSPI 设备或重复挂载。
- 完成源码、配置、diff、资源释放和分层边界静态检查。
- 人工验证完成后同步本 Goal、`ROADMAP.md`、`README.md`、`docs/project-overview.md`、`AGENTS.md` 和历史索引。

### 本节点禁止实现或修改

- 不格式化、擦除、修复或写入 SD 卡，不删除或覆盖卡内现有文件。
- 不实现文件浏览器、目录遍历、文件读写封装、资源索引、缓存、图标、字体、音乐、游戏资源或日志落盘。
- 不实现 SD 热插拔中断、卡检测 GPIO、轮询 Task 或自动重挂载；硬件资料未证明存在独立 Card Detect 引脚。
- 不建立完整 BSP 层，不迁移 TFT、按键、ADC、I2C、LEDC 或其他 Dashboard 驱动。
- 不修改 GPIO18／19／22／23 接线、TFT 时钟、分区表、NVS schema、Wi-Fi、Agent、Audio、Games 或 GD32 工程。
- 不切换到 SDMMC 四线模式，不引入第三方文件系统，不增加项目依赖。
- 不用 workaround 隐藏 `0x107`、挂载错误或 GPIO 冲突警告；错误必须保留原始 `esp_err_t` 供 UI 和日志诊断。

## 检查点、验收方式与预期结果

### CP1：Service 契约与构建接入

实施内容：

- 新增 `xiaomiao_storage_service.{h,c}` 和公开状态模型。
- 加入 `SERVICE_SRCS`，依赖继续复用项目已有 `esp_driver_sdspi`、`fatfs`、`sdmmc`、GPIO 和 SPI 组件。
- 初始化参数校验、幂等和快照复制路径完整。

静态验收：

- Service 不 include LVGL、Launcher、Navigation 或具体 App 头文件。
- 公开头文件不暴露 `sdmmc_card_t *` 或 SDSPI 设备句柄。
- `main.c` 仍是板级 SPI2／GPIO22 配置来源。
- 不新增第三方依赖、Task、队列、timer 或动态框架。

### CP2：挂载、失败清理与卸载迁移

实施内容：

- 迁移手动 SDSPI + FATFS 挂载流程。
- 覆盖分配失败、host 初始化失败、设备创建失败、卡初始化失败、FATFS 挂载失败和卸载失败。
- 保留 GPIO22 在设备移除前复位的既有修复。

静态验收：

- 每条失败路径只释放已取得资源，不重复释放、不泄漏卡对象。
- 任何失败都不提交半完成的 `MOUNTED` 快照。
- Service 不初始化或释放共享 SPI2 总线。
- 源码中不存在 `format_if_mount_failed=true`、格式化 API 或卡数据写入。

### CP3：启动链和 Hardware Test 接入

实施内容：

- 在 `lcd_init()` 之后初始化 Storage Service，失败只记录警告。
- MicroSD 页面和 A／B 动作改用 Service。
- 删除 `main.c` 中已迁移的 SD 状态和生命周期实现。

静态验收：

- `main.c` 不再直接调用 `sdspi_host_init_device()`、`sdmmc_card_init()`、`esp_vfs_fat_mount_initialized()` 或 `esp_vfs_fat_sdcard_unmount()`。
- Hardware Test 页面不持有 SD 私有句柄。
- 其他 14 个 Hardware Test 页面与五 App 注册顺序不变。

### CP4：Agent 静态复核

Agent 执行以下只读检查，不运行项目禁止的 ESP-IDF 构建、烧录、串口监视或硬件操作：

- 精确检索 SDSPI／FATFS／GPIO22 调用边界。
- 检查初始化顺序、状态提交时机、互斥范围和资源释放顺序。
- 检查 diff 只包含节点 14 相关代码与文档。
- 检查新增 C 文件的括号、行尾空白、制表符和项目命名风格。
- 检查 README、项目概览、AGENTS、ROADMAP、Goal 与历史索引口径一致。

### CP5：人工构建与完整实机验收

人工在 Windows PowerShell 加载 ESP-IDF 6.1 环境后执行：

```powershell
python "$env:IDF_PATH\tools\idf.py" --version
python "$env:IDF_PATH\tools\idf.py" build
python "$env:IDF_PATH\tools\idf.py" -p COM5 flash
python "$env:IDF_PATH\tools\idf.py" -p COM5 monitor
```

预期版本：`ESP-IDF v6.1`。验收场景按顺序执行：

1. **无卡冷启动**：不插卡启动，Storage Service 挂载失败但 Launcher 正常出现；五个 App 可进入，串口无致命错误。
2. **重复无卡重试**：进入 Hardware Test 的 MicroSD 页连续按 A 10 次；每次保持可操作，日志不得出现 `gpio: conflict found for GPIO[22]`，随后可退出并重新进入 Hardware Test。
3. **未挂载状态插卡**：保持设备通电，在未挂载状态插入本次到货的 SD 卡，按 A；预期显示 `MOUNTED`、非空卡名和合理容量，串口无 GPIO22 冲突。
4. **安全卸载**：按 B；预期返回未挂载状态，Launcher、屏幕刷新和其他 App 正常。
5. **卸载后移除并重插**：只在卸载成功后拔卡，再插回并按 A；预期再次成功挂载，卡名和容量与首次一致。
6. **重复挂载语义**：已挂载状态下页面不应提供或触发第二次设备创建；通过页面切换、退出和重新进入确认快照仍正确。
7. **带卡冷启动**：保持卡已插入并断电重启；预期启动阶段自动挂载成功，仍正常进入 Launcher，MicroSD 页首次进入即显示 `MOUNTED`。
8. **共享 SPI 回归**：挂载状态下连续切换 Hardware Test 全部 15 页并往返 Launcher 至少 5 轮；屏幕无新增花屏、卡死、刷新错误或 SPI 冲突。
9. **既有功能烟测**：Games、PC Monitor、Tools、Settings 和 Hardware Test 均可进入、操作和返回；Wi-Fi 图标与 PC Monitor 既有行为无回归。

验收证据至少保留：

- `idf.py --version` 与构建成功结论。
- 无卡冷启动到 `Launcher ready` 的关键日志。
- 10 次无卡重试中无 GPIO22 冲突警告的日志范围。
- 首次正常挂载、卸载、重新挂载和带卡冷启动的关键日志。
- 页面显示的卡名与容量，及共享 SPI／五 App 人工回归结论。

若本次到货卡仍返回 `0x107`，不得直接判定代码缺陷或宣称节点完成；先核对卡格式、触点、插入方向并用电脑确认卡可识别，再决定是否需要另一张已知良好卡。不得通过自动格式化绕过问题。

## 完成标准

以下条件全部满足后，节点 14 才能标记完成：

- CP1～CP4 的实现和静态检查全部通过。
- 人工 `idf.py build`、烧录和 CP5 场景全部通过。
- 至少取得一次正常挂载、一次安全卸载、一次重新挂载和一次带卡冷启动成功证据。
- 10 次无卡重试无 GPIO22 冲突警告，失败后 Launcher 与 Hardware Test 生命周期正常。
- 没有格式化、写入、删除或覆盖 SD 卡数据。
- Goal 中记录实际实现差异、验证证据和未验证范围。
- `ROADMAP.md` 将节点 14 移入完成状态，并同步当前能力文档和历史索引。

仅有源码静态检查、历史无卡日志或一次偶然挂载成功，都不足以完成节点 14。

## 当前未验证范围

- 2026-09-22 15:38 起无遗留项：九个 CP5 场景全部通过（见实施记录），本节仅保留边界说明——节点 15 的文件读写与 Assets 加载不属于本节点，不能用本节点完成状态替代其验收。

## 实施记录

- 2026-09-22 09:38：创建施工文档。确认 SD 卡已到货，节点 14 可以覆盖完整硬件流程；当前状态为已立项、尚未实施。
- 2026-09-22（实施）：CP1～CP3 编码完成。新增 `main/services/xiaomiao_storage_service.{h,c}` 并加入 `main/CMakeLists.txt` 的 `SERVICE_SRCS`；`main/main.c` 删除 `sd_revoke_cs_ownership()`、`sd_release_mount_resources()`、`sd_try_mount()`、`sd_unmount()`、`s_sd_card` 与 `board_state_t` 的 `sd_mounted`／`sd_name`／`sd_mb`／`last_sd_err` 字段，MicroSD 页刷新与 A／B 动作改为调用 Service 快照与 `xiaomiao_storage_mount()`／`xiaomiao_storage_unmount()`，启动链在 `lcd_init()` 之后以 `LCD_HOST`＋`PIN_NUM_SD_CS`＋`SD_SPI_MAX_FREQ_KHZ` 初始化 Service（失败只记警告）。`PIN_NUM_SD_CS` 与 `SD_SPI_MAX_FREQ_KHZ` 保留在 `main.c` 作为板级配置来源（决策 2）。因迁移成为孤儿的 `#include <stdlib.h>` 一并移除。
- 2026-09-22（实施）：CP4 静态复核通过。括号配平、行尾空白、制表符检查无异常；`main.c` 无 SDSPI／SDMMC／FATFS 残留（仅剩被启动链消费的板级宏），`main/apps/` 无任何 SD 头引用；Service 头文件不暴露 `sdmmc_card_t`／SDSPI 句柄，不 include LVGL／framework／App 头；失败路径只释放已取得资源且不提交半完成快照；diff 范围 = 本节点代码 + 立项时的文档同步（README／ROADMAP／overview／design／历史索引，为 2026-09-22 09:32/09:38 遗留改动，内容与本节点一致）。实现与 Goal 建议接口完全一致，无口径差异；一处实现细化：`init()` 参数校验失败不锁定初始化状态（可重新传入合法配置），校验通过后才置 `s_init_done` 并执行首次挂载；`cs_gpio` 校验使用 `GPIO_IS_VALID_OUTPUT_GPIO()`。未运行 ESP-IDF 构建（AGENTS.md 分工），构建与实机验证待 CP5。
- 2026-09-22 15:38：CP5 人工构建与完整实机验收通过。项目负责人执行 `idf.py --version`（ESP-IDF v6.1）、`build`、`flash`、`monitor` 后按序完成九个场景，全部通过。逐场景证据：
  1. **无卡冷启动**：挂载失败只留警告，Launcher 正常出现，五 App 均可进入（运行期日志 158879～530005 ms 五 App 全部开关正常）。启动段日志未单独保留，以"挂载失败后设备完全可用"为等价证据（与节点 8/9 末轮回归相同的人工证据类型）。
  2. **重复无卡重试**：运行期日志 549676～553607 ms 连续 10 次 `storage_svc: sdmmc_card_init failed (0x107)`，全程无 `gpio: conflict found for GPIO[22]`；569205 ms 一次 0x107 位于首次插卡成功（639038 ms）之前，按无卡预期失败记录。
  3. **未挂载状态插卡**：639038 与 671473 ms 两条 `sdspi_transaction: cmd=5, R1 response: command not supported` 后无任何失败日志（经核对 IDF v6.1 `sdmmc_init.c`，CMD5 为 `sdmmc_card_init()` 标准的 SDIO 卡探测步骤，microSD 卡拒绝 CMD5 属预期正常行为，同时证明卡在物理上应答了 SPI 命令）；屏幕显示 `MOUNTED` + `SD 29818MB`，卡名来自 CID 寄存器，29818MB ≈ 29.1GB 与 32GB 卡真实用户容量吻合。
  4. **安全卸载**：按 B 返回未挂载态，屏幕与提示正常（Service 卸载成功按设计静默，无日志输出）。
  5. **卸载后移除并重插**：卸载成功后才拔卡，重插后按 A 再次 `MOUNTED`，卡名与容量一致（会话日志 304876 ms 处 `cmd=5` 后无失败日志，人工确认）。
  6. **重复挂载语义**：页面切换、退出重进期间快照保持正确，无重复设备创建（幂等路径人工确认）。
  7. **带卡冷启动**：`rst:0x1 (POWERON_RESET)` 后启动段分区表正常、无 `Storage service unavailable` 警告、无任何 `storage_svc` 错误（对比无卡启动必有 0x107 警告），开机自动挂载成功；9975 ms 打开 Hardware Test 首次进入 MicroSD 页即 `MOUNTED`，46156 ms 关闭时 `launcher focus=4 page=1` 正常。
  8. **共享 SPI 回归**：挂载状态下 15 页往返 + Launcher 往返 5 轮无花屏、卡死、刷新错误（人工确认）。
  9. **既有功能烟测**：49768～56374 ms 五 App（Games、PC Monitor、Tools、Settings、Hardware Test）依次开关 `screen children=2` 全部正常，Wi-Fi 自动重连 3805 ms 取得 IPv4，PC Monitor 既有行为无回归。日志中 `esp-tls`／`HTTP_CLIENT` 超时为 PC Agent 未运行的既有背景，与 SD 无关；`ClearCommError` 为断电重启引起的串口断开，非固件故障。

## 交付结果

节点 14 于 2026-09-22 完成验收。

**实际改动文件**：
- 新增 `main/services/xiaomiao_storage_service.h`、`main/services/xiaomiao_storage_service.c`
- `main/CMakeLists.txt`：`SERVICE_SRCS` 加入新源文件
- `main/main.c`：删除 4 个 SD 生命周期函数、`s_sd_card` 与 `board_state_t` 的 4 个 SD 字段，MicroSD 页与 A/B 动作改调 Service，启动链 `lcd_init()` 后接入（孤儿 `stdlib.h` 一并移除）
- 文档同步：`README.md`、`ROADMAP.md`、`docs/project-overview.md`、`AGENTS.md`、`goals/ROADMAP-history.md`

**接口最终形态**：与「公开接口契约」一节完全一致（`xiaomiao_storage_state_t`、`xiaomiao_storage_config_t`、`xiaomiao_storage_snapshot_t`、`xiaomiao_storage_service_init()`、`xiaomiao_storage_mount()`、`xiaomiao_storage_unmount()`、`xiaomiao_storage_get_snapshot()`），无命名或字段类型调整。

**静态检查**：CP4 通过（调用边界、括号配平、行尾空白、diff 范围、分层边界，详见实施记录）。

**人工验证证据**：见实施记录 2026-09-22 15:38 条目。关键证据：10 次无卡重试无 GPIO22 冲突警告（日志 549676～553607 ms）；插卡挂载屏幕 `MOUNTED` + `SD 29818MB`；带卡冷启动启动段无 Storage 警告与 SD 错误（`rst:0x1` 后 9975 ms 进 Hardware Test 即 `MOUNTED`）；15 页往返 + 五 App 烟测通过。安全卸载与幂等重挂为人工确认证据（卸载成功静默属 Service 设计行为，成功路径无日志）。

**口径差异**：
- 场景 1（无卡冷启动）的 0 秒起启动段日志未单独保留，以「挂载失败后五 App 与 Launcher 完全可用」为等价证据，类型为人工确认。
- 卸载与幂等重挂成功时 Service 按设计不打日志，证据为人工确认 + 卸载后 UI 状态正确。
- `cmd=5, R1 response: command not supported` 为 IDF 标准 SDIO 探测的正常预期输出，不记为异常。

**剩余限制**：
- 首版不提供通用文件 API（文件读写、目录遍历、资源索引属节点 15）。
- 不支持热拔插：已挂载时拔卡语义未定义，必须先按 B 卸载（goal 决策 12）；硬件无独立 Card Detect 引脚，不做插入检测。
- Node 15 只能消费 `/sdcard` 挂载点与 Service 快照，不得重新实现 SD 驱动生命周期。
