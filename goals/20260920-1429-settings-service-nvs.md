# 节点 9：Settings Service 与 NVS

## 元信息

- 对应节点：节点 9“Settings Service 与 NVS”
- 状态：已完成（2026-09-20 20:16 收尾确认；普通构建与烧录、首启默认值与断电重启后 NVS 加载、6 步自测固件 `PASS`、Settings UI 与五 App 回归均已人工验证通过；两条无法构造的失败路径只按源码检查确认）
- 创建时间：2026-09-20 14:29（北京时间）
- 前置条件：节点 0～8 均已完成；节点 8 已建立 Settings 菜单、能力状态页和五入口分页回归基线
- 前置施工文档：`goals/20260920-1338-settings-ui.md`
- 后续目标：节点 10“Wi-Fi Service”消费 `wifi_auto_connect`；节点 12“Audio Service”消费 `sound_enabled`

## 目标与预期行为

建立首个 System Service：`Settings Service`。它负责提供确定的默认配置、内存配置快照、配置校验、NVS 读写和安全回退，使后续 Wi-Fi 与 Audio Service 不直接依赖 NVS，也不各自发明配置格式。

普通固件启动时先初始化 Settings Service，再继续现有硬件、LVGL、App Registry 和 Launcher 启动链。配置首次缺失时使用默认值并写入 NVS；配置 blob 的版本、长度或字段值非法时回退默认值并只修复 Settings 自己的 key；NVS 初始化、打开或提交失败时记录明确错误，当前启动继续使用内存默认值，不得阻塞 Launcher。

本节点只建立持久化能力和可观测状态，不提前实现设置的业务效果。Settings App 不提供 `Wi-Fi` 或 `Sound` 开关，避免用户把“偏好已保存”误认为 Wi-Fi 或声音能力已生效；现有四项菜单和两级返回保持不变。

## 背景与文件入口

- `docs/xiaomiao_firmware_v0.1_design.md` 第 6、7、12、15 节规定 Settings App → Settings Service → NVS 的边界、System Services 初始化阶段，以及节点 9 的默认值、读写、持久化和损坏回退要求。
- `goals/20260920-1338-settings-ui.md` 已明确：节点 8 不定义配置字段、默认值、NVS schema 或恢复策略，这些决策由本 Goal 固定。
- `main/apps/settings/xiaomiao_settings.c` 当前四个详情页均为只读状态页；`Display` 与 `System` 文案暂指向节点 9，实施时只允许替换为本节点能够真实证明的状态。
- `main/main.c::app_main()` 当前在普通构建中直接初始化传感历史、按键、LCD 和硬件，尚无 Service 初始化阶段。
- `main/CMakeLists.txt` 当前没有 Service 源文件集合，也未声明 `nvs_flash` 组件依赖。
- 根目录没有自定义分区表；当前配置使用 ESP-IDF 单 App 默认分区表。不得仅为本节点引入自定义分区表或改变应用分区布局。
- `README.md` 已确认屏幕背光引脚直连电源，不能调节背光亮度；本节点不得虚构亮度设置。

## 范围边界

### 本节点必须完成

- 新增 `main/services/xiaomiao_settings_service.h` 与 `main/services/xiaomiao_settings_service.c`。
- 在 `main/CMakeLists.txt` 中加入 Service 源文件集合和最小必要的 `nvs_flash` 依赖。
- 定义公开的运行时配置结构、默认值、完整配置读取与写入接口，以及持久化状态查询接口。
- 定义内部版本化 NVS blob、namespace、key、校验和安全回退策略。
- 在普通固件启动链中初始化 Settings Service；失败只降级持久化能力，不阻塞后续启动。
- 让 Settings 的 `System` 详情页只读显示真实持久化状态：已从 NVS 加载、首次写入默认值、损坏后恢复，或 NVS 不可用而使用内存默认值。
- 修正 Settings 的 `Display` 详情页，使其如实显示固定背光／硬件不支持亮度调节，不再承诺节点 9 提供显示控制。
- 提供能够验证默认值、非默认值写入、重启恢复、缺失 key 回退、非法 blob 回退和 NVS 不可用降级的测试入口或明确人工测试手段。
- 保持五个 App 的注册顺序、Launcher 分页、Settings 两级 B 语义和节点 0～8 既有行为。
- 实现及验证完成后同步更新 `ROADMAP.md`、`README.md`、`docs/project-overview.md` 和本 Goal 的交付／验证结果。

### 本节点禁止修改或实现

- 不实现 Wi-Fi 初始化、扫描、连接、凭据保存、自动重连或任何 `esp_wifi_*` 调用；这些属于节点 10。
- 不实现 Audio Service、蜂鸣器开关效果、音量或音效；这些属于节点 12。
- 不实现屏幕亮度控制、休眠、主题或新的 Display Service；当前硬件不能调节背光亮度，其他显示语义也尚未立项。
- 不增加“恢复出厂设置”或整机清理入口；Wi-Fi 等后续数据的所有权尚未建立。
- 不保存 Wi-Fi SSID、密码、token、密钥或其他敏感数据。
- 不修改分区表、Flash 布局、硬件引脚、GD32 工程、Dashboard 外设逻辑或 `main/framework/`。
- 不创建新的 FreeRTOS Task、队列、事件组或后台轮询；Settings Service 是同步、被调用式服务。
- 不调用 `nvs_flash_erase()`，不因 Settings 数据异常擦除整个默认 NVS 分区。
- 不为未来未知配置增加通用键值框架、动态注册、观察者系统或迁移框架。

## 已确定的配置模型

公开运行时配置只包含业务字段，不暴露 NVS header、保留位或内部布局：

```c
typedef struct {
    bool wifi_auto_connect;
    bool sound_enabled;
} xiaomiao_settings_t;
```

首版默认值固定为：

```text
wifi_auto_connect = true
sound_enabled     = true
```

字段语义：

- `wifi_auto_connect` 只是节点 10 的持久化偏好；节点 10 实现前不触发 Wi-Fi 行为，也不在 Settings UI 提供开关。
- `sound_enabled` 只是节点 12 的持久化偏好；节点 12 实现前不改变蜂鸣器或 Hardware Test 行为，也不在 Settings UI 提供开关。
- 首版不定义音量、亮度、休眠、主题、语言、启动 App、SSID、密码或恢复出厂标志。
- 后续新增字段必须提升 schema 版本并定义明确迁移或回退策略，不得按 `sizeof` 差异盲目读取旧 blob。

## NVS schema 与恢复策略

### 固定标识

```text
NVS partition：默认 `nvs`
namespace：`xiaomiao`
key：`settings`
schema version：1
```

内部 blob 使用固定宽度整数，至少包含：

```c
typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t reserved[2];
} settings_blob_v1_t;
```

实现不得直接把公开结构按原始内存布局写入 NVS。写入前把 `bool` 显式转换为 `0`／`1`，读取后验证：

- blob 长度与 v1 固定长度一致；
- `schema_version == 1`；
- `payload_size` 与 v1 payload 定义一致；
- 两个布尔字段只能是 `0` 或 `1`；
- `reserved` 写入时清零，读取时不承载业务语义。

恢复规则按下列顺序执行：

1. key 不存在：装载默认值，尝试把默认 blob 写入并提交；提交失败仍以默认值继续运行。
2. blob 长度、版本或字段非法：装载默认值，只覆盖 `xiaomiao/settings`，不得擦除 namespace 中其他 key 或整个 NVS 分区。
3. `nvs_flash_init()`、`nvs_open()`、读取或提交失败：装载内存默认值，状态标记为持久化不可用并继续启动。
4. 不自动处理 `ESP_ERR_NVS_NO_FREE_PAGES` 或 `ESP_ERR_NVS_NEW_VERSION_FOUND` 为整分区擦除；记录原始错误码，避免未来误删 Wi-Fi 或其他 Service 数据。
5. 写入新配置时先校验输入，再执行 `nvs_set_blob()` 和 `nvs_commit()`；只有提交成功后才替换内存快照。失败时保留原快照并返回原始错误。

NVS 自身已提供底层完整性检查；本节点通过固定长度、schema 和字段范围识别本 Service 的结构损坏，不重复增加无明确收益的应用层 CRC。

## 预期公开接口

头文件至少提供以下能力，最终命名可以因 ESP-IDF 约束做等价微调，但不得改变语义：

```c
typedef enum {
    XIAOMIAO_SETTINGS_SOURCE_DEFAULTS = 0,
    XIAOMIAO_SETTINGS_SOURCE_NVS,
    XIAOMIAO_SETTINGS_SOURCE_RECOVERED,
    XIAOMIAO_SETTINGS_SOURCE_DEGRADED,
} xiaomiao_settings_source_t;

esp_err_t xiaomiao_settings_service_init(void);
esp_err_t xiaomiao_settings_get(xiaomiao_settings_t *out_settings);
esp_err_t xiaomiao_settings_set(const xiaomiao_settings_t *settings);
xiaomiao_settings_source_t xiaomiao_settings_source(void);
esp_err_t xiaomiao_settings_last_error(void);
```

接口约束：

- `init()` 幂等；首次调用完成 NVS 初始化和加载，重复调用不得重复写 Flash。
- 即使 `init()` 返回持久化错误，Service 也进入可读的降级状态，`get()` 返回内存默认值。
- `get()` 必须复制完整快照，不向调用者暴露内部可变指针。
- `set()` 接收完整配置、验证后原子更新；空指针或非法字段返回明确错误，不写 NVS、不改变缓存。
- `source()` 与 `last_error()` 只用于状态展示和诊断，不把 NVS handle 暴露给 App。
- Service 不依赖 LVGL、Launcher、Navigation 或任何具体 App。
- 若实现需要互斥，只能使用 Service 内部同步原语，不得在持锁期间调用 UI；不得新增后台任务。

## 启动与 UI 接入决策

1. 普通固件路径在硬件和 LVGL 初始化前调用 `xiaomiao_settings_service_init()`；失败记录一次包含原始 `esp_err_t` 的警告并继续启动。
2. 三个现有 Framework 自测构建不依赖 Settings Service，不得因 NVS 状态改变既有 Framework／Navigation／Launcher 自测结果。
3. Settings App 的 `Wi-Fi` 页继续显示节点 10 未实现，`Sound` 页继续显示节点 12 未实现；不显示可修改控件。
4. `Display` 页显示硬件事实，例如 `Brightness fixed` 与 `Backlight tied to VCC`，保持只读。
5. `System` 页显示 Settings Service 当前来源和必要错误摘要，不提供重置动作；每次进入详情页时读取一次状态，不创建 timer。
6. 状态文案必须区分“默认值正常使用”“损坏后恢复”和“NVS 不可用”；不得把降级状态显示成已持久化成功。
7. Settings App 只调用 Settings Service 公共接口，不直接 include `nvs.h`／`nvs_flash.h` 或调用 `nvs_*`。

## 执行检查点

### 检查点 1：Service 边界与默认配置

- 新增 Service 头文件和实现文件，公开接口与内部 NVS schema 分离。
- 默认配置精确为两个 `true`，输入校验拒绝非法值和空指针。
- 不创建 Task，不依赖 LVGL 或具体 App。

预期结果：静态检查可证明 Service 职责单一，调用者无法直接修改内部缓存或 NVS handle。

### 检查点 2：NVS 加载、写入与安全回退

- 覆盖首次缺失、有效 v1、非法长度、未知版本、非法布尔值和提交失败路径。
- 非默认值写入后能够重新打开 NVS 并读回；重启后能够恢复相同配置。
- 所有失败路径都有稳定内存配置和明确来源／错误状态。
- 不存在整分区擦除调用。

预期结果：缺失或损坏配置不会阻塞启动，也不会破坏其他 namespace／key。

### 检查点 3：普通启动链与 Settings 状态页

- 普通固件先初始化 Settings Service，再继续现有启动链。
- NVS 正常或异常时均能到达 `Launcher ready, 5 app(s) registered`。
- Display 页面准确陈述固定背光；System 页面准确显示配置来源，Wi-Fi／Sound 页面仍不承诺未实现能力。
- Settings 两级 B、按键锁存和 Navigation 内容根所有权不变。

预期结果：节点 9 的真实状态可见，未把偏好保存冒充成业务能力生效。

### 检查点 4：回归与文档交付

- 五个 App 注册顺序、两页布局、焦点保持、open／close 配对和 `screen children=2` 基线不变。
- 一次 Monitor 会话内的进入／返回日志必须严格成对：`<app> opened` 与 `<app> closed` 数量相等、无孤立 open、无重复 close，且 `screen children` 在所有 open／close 日志上恒为 2（与节点 5～8 的既有基线一致）。
- 本节点改动涉及的页面必须全部进入并返回一次：Settings 菜单的四项详情页，以及 Settings 的两级 B（菜单 ↔ 详情页 ↔ Launcher）。
- 未改动源码的 App 不设往返次数要求，其回归由“注册顺序不变 + 启动链注册日志正常 + `screen children` 基线”三类证据覆盖；如需补充目视确认，人工在既有固件上多按若干轮即可，不作为阻塞条件。
- Hardware Test 15 页仍可达；本节点不要求重新验证缺失硬件的真实 LED／电机／MPU6050 行为。
- 更新项目状态与当前真实能力描述，不把未执行的构建或实机测试写成通过。

预期结果：节点 0～8 无新增回归，项目文档与实现一致。

## 失败路径与处理要求

- `nvs_flash_init()` 失败：记录错误，装载内存默认值，继续 Launcher；不得调用 `ESP_ERROR_CHECK` 终止启动。
- namespace 打开失败：同样降级，不泄漏 handle，不阻塞启动。
- key 不存在：这是首次启动路径，不记录为设备故障；使用并尝试持久化默认值。
- blob 非法：记录恢复原因，使用并尝试持久化默认值；只覆盖本 key。
- 默认值回写失败：保持默认值可读，状态为降级而不是“已恢复”。
- `set()` 输入无效：返回 `ESP_ERR_INVALID_ARG`，不得修改缓存或 Flash。
- `set_blob()` 或 `commit()` 失败：保留旧缓存，返回原始错误；不得报告保存成功。
- Settings App 查询状态失败：显示 `Settings unavailable` 或等价真实文案，页面和 B 返回仍可用。
- Service 初始化失败后再次调用：保持幂等和可预测状态，不重复擦写或泄漏 handle。

## 验收标准

> 勾选依据见“验证结果”与 `goals/ROADMAP-history.md` 的 2026-09-20 条目（重构前位于 `ROADMAP.md` 的“最近验证”节）。第 7 条只部分验证：读取失败与降级已在自测第 6 步实测，`nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败无法在不引入自定义分区表的前提下构造，只按源码检查确认（该路径只记录原始错误码、装载内存默认值、不使用 `ESP_ERROR_CHECK`、不擦分区、不终止启动），按项目惯例标记为不阻塞收口。

- [x] 新增独立 Settings Service，App 不直接访问 NVS。（检索确认 `nvs_*` 调用只出现在 `main/services/`；`xiaomiao_settings.c` 内无 `nvs_`／`nvs_flash`）
- [x] 默认配置只有 `wifi_auto_connect=true` 与 `sound_enabled=true`。（首启日志 `source=defaults (wifi_auto_connect=1, sound_enabled=1)`）
- [x] NVS blob 使用固定 schema v1，公开结构不直接序列化。（`settings_blob_v1_t` + `_Static_assert` 固定 8 字节；公开 `xiaomiao_settings_t` 逐字段编码）
- [x] 首次缺失能写入默认值，重启后从 NVS 加载。（首启 `source=defaults`，断电重启 `rst:0x1 (POWERON_RESET)` 后 `source=nvs`）
- [x] 非默认值能够写入、提交并在重启后恢复。（自测第 1 步写入 `false/false`，第 2 步跨 `esp_restart()` 读到 `false/false`）
- [x] 长度、版本或字段损坏时安全回退，并且只修复 `xiaomiao/settings`。（自测第 3／4／5 步分别注入未知版本、越界布尔值、短于 v1 的 blob，均回退默认值并重写本 key；每步都断言 namespace 内其他 key 未被触碰）
- [x] 任一 NVS 初始化／打开／读取／写入失败均不阻塞 Launcher。（读取失败已实测：自测第 6 步 `ESP_ERR_NVS_INVALID_LENGTH` → `source=degraded` 且快照仍可读；初始化／打开／提交失败仅源码检查，见上方说明）
- [x] 源码中不存在 `nvs_flash_erase()`，不存在自定义分区表改动。（全仓库仅一句“未调用”的注释；分区表日志为默认单 App 表，`nvs` 分区 `01 02 0000a000 00006000`）
- [x] Settings UI 不出现尚未生效的 Wi-Fi／Sound 开关。（人工确认）
- [x] Display 页如实说明固定背光，System 页显示真实持久化状态。（人工确认；System 页在首启显示 `Defaults applied`、重启后显示 `Loaded from NVS`）
- [x] 普通启动仍注册 5 个 App，既有分页、焦点、生命周期和 B 语义无回归。（两次启动日志均 `Launcher ready, 5 app(s) registered`；五 App 回归人工确认）
- [x] 人工使用 ESP-IDF 6.1 完成普通构建、烧录、Monitor、重启持久化和实机 UI 回归。（构建 `App version: 7088147-dirty`；末轮 UI 与回归为人工口头确认，未提供补充串口日志）
- [x] Goal、`ROADMAP.md`、`README.md` 与 `docs/project-overview.md` 已同步真实状态。

## 人工验证命令与预期结果

Agent 只执行源码、配置和 diff 静态检查，不主动运行以下命令。人工在 ESP-IDF 6.1 PowerShell 环境中执行：

```powershell
python "$env:IDF_PATH\tools\idf.py" --version
python "$env:IDF_PATH\tools\idf.py" build
python "$env:IDF_PATH\tools\idf.py" -p COM5 flash
python "$env:IDF_PATH\tools\idf.py" -p COM5 monitor
```

预期：

- 版本输出为 `ESP-IDF v6.1`。
- 普通构建成功，不启用任何自测宏（`XIAOMIAO_FRAMEWORK_SELF_TEST`／`XIAOMIAO_NAVIGATION_SELF_TEST`／`XIAOMIAO_LAUNCHER_SELF_TEST`／`XIAOMIAO_SETTINGS_SERVICE_SELF_TEST`）。
- 首次启动先出现 `settings_svc` 的 `settings service ready, source=defaults (wifi_auto_connect=1, sound_enabled=1)`，随后启动链继续，最终仍出现 `Launcher ready, 5 app(s) registered`。
- Settings 的 `System` 页应显示 `Defaults applied` 与 `2 stored fields`；`Display` 页应显示 `Brightness fixed`／`Backlight tied to VCC`；`Wi-Fi`／`Sound` 页仍不提供开关；五 App 与 Hardware Test 回归满足检查点 4 的实质口径。
- 再次重启（不重新烧录）应变成 `settings service ready, source=nvs`，且 Settings 的 `System` 页显示 `Loaded from NVS`——这是“重启后从 NVS 加载”的现场证据。

### 自测构建（覆盖缺失 key、非默认值重启恢复与损坏回退）

沿用 `AGENTS.md` 的隔离构建形式（与节点 3 的 `build-launcher-selftest` 相同），只是多一个 `XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=ON` 并把另外三个自测选项显式关掉：

```bash
idf.py -B .tmp/build-settings-selftest/build -D SDKCONFIG=$(pwd)/.tmp/build-settings-selftest/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=ON -D XIAOMIAO_FRAMEWORK_SELF_TEST=OFF -D XIAOMIAO_NAVIGATION_SELF_TEST=OFF -D XIAOMIAO_LAUNCHER_SELF_TEST=OFF set-target esp32
idf.py -B .tmp/build-settings-selftest/build -D SDKCONFIG=$(pwd)/.tmp/build-settings-selftest/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=ON build
idf.py -B .tmp/build-settings-selftest/build -p COM5 flash monitor
```

PowerShell 下按 `.tmp/build-baseline.ps1` 的写法：先清掉 `Env:MSYSTEM`、设 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`、`& "$env:IDF_PATH\export.ps1"`，再用参数数组传 `'-D', 'SDKCONFIG=D:/WorkSpace/hardware/esp32-lab/xueersi-idf/.tmp/build-settings-selftest/sdkconfig'`（绝对路径，不能用 `$(pwd)`）。

该固件不需要 LCD 或按键，烧录后自动完成 6 步并自行重启，人工只需观察串口：

```text
Settings service self test, step 1/6
step 1/6 done, restarting
...
Settings service self test, step 6/6
SETTINGS_SERVICE_SELF_TEST: PASS
```

预期：任意一步不符合断言时输出 `SETTINGS_SERVICE_SELF_TEST: FAIL <说明> (<条件>)` 并挂起，不会陷入重启循环；末步 PASS 后 namespace 已被清回干净状态，可重复烧录重跑；全程串口**不出现** `nvs_flash_erase` 相关行为（不会擦除整个分区）。跑完后需重新烧录普通固件以继续上面的普通构建验证。

如实现无法提供安全、可重复且不依赖修改源码的非默认值写入与损坏注入手段，则“非默认值重启恢复”和“损坏回退”保持未验证，节点 9 不得标记完成。（已提供并已静态核对，仍待人工执行。）

## 静态检查要求

实现后至少检查：

```powershell
rg -n "nvs_|nvs_flash" main
rg -n "nvs_flash_erase|PARTITION_TABLE_CUSTOM|partitions\.csv" main sdkconfig.defaults sdkconfig.ci CMakeLists.txt
rg -n "esp_wifi|ledc_|gpio_|i2c_|spi_|sdmmc|sdspi|xTaskCreate|xQueue|xEventGroup" main/services
rg -n "wifi_auto_connect|sound_enabled|schema_version|payload_size" main/services
```

预期：NVS 调用只位于 Service 和明确的测试入口；没有整分区擦除、自定义分区、硬件访问、网络调用或后台任务；两个字段及 schema 校验可追踪。

## 验证结果（2026-09-20，人工执行、Agent 复核）

### 1. 普通构建与烧录（19:34）

- 构建产物标识 `App version: 7088147-dirty`、`Compile time: Sep 20 2026 19:34:22`，确认板上固件含本节点改动。
- 分区表为默认单 App 表：`nvs WiFi data 01 02 0000a000 00006000`，未引入自定义分区表。
- **首次启动**：`xiaomiao_dash: Xiaomiao LVGL 9.5 launcher boot` → `settings_svc: settings service ready, source=defaults (wifi_auto_connect=1, sound_enabled=1)` → `Start Xiaomiao launcher` → `launcher created (5 apps)` → `Launcher ready, 5 app(s) registered`。证明 Settings Service 在硬件与 LVGL 之前完成初始化，且 key 缺失时装载并成功写入默认值。
- **人为断电重启**（`rst:0x1 (POWERON_RESET)`，真实冷启动）后：`settings_svc: settings service ready, source=nvs (wifi_auto_connect=1, sound_enabled=1)`，且仍到达 `Launcher ready, 5 app(s) registered`。证明配置从 NVS 成功加载，且 Service 接入未破坏启动链。
- 两次启动均无 self-test 标记、无旧 dashboard 启动行、无 panic／看门狗／重启循环、无 `nvs_flash_erase` 相关行为。

### 2. 自测固件 6 步（`-D XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=ON`，`.tmp/build-settings-selftest/`）

- 编译 exit=0，出现 10 条 `-Wunused-function` 警告（`lvgl_task`／`lcd_init`／`buttons_init` 等），属预期：该自测分支不初始化 LCD 与 LVGL，与既有 Framework 自测构建同源。
- 末步日志：`settings_selftest: step 6/6` → `settings_svc: reading settings failed: ESP_ERR_NVS_INVALID_LENGTH (0x110c), using memory defaults` → `settings_svc: settings service degraded, source=degraded` → `SETTINGS_SERVICE_SELF_TEST: PASS`。
- **通过的有效性依据**：`xiaomiao/selftest_stage` 标记只在当前整步的全部断言通过后才推进（失败打印 FAIL 并挂起，不推进也不重启），因此进入第 6 步即证明第 1～5 步全部通过。据此已覆盖：缺失 key 装载并持久化默认值（第 1 步）、非默认值写入后跨真实重启恢复（第 1→2 步）、未知 schema 版本回退（第 3 步）、越界布尔值回退（第 4 步）、短于 v1 的 blob 回退（第 5 步）、长于 v1 的 blob 降级与“不覆盖已存条目”（第 6 步），以及每步附带的“只重写 `xiaomiao/settings`、其他 key 未被触碰”断言；另覆盖 `init()` 幂等、`get()`／`set()` 的 NULL 与未初始化参数校验、布尔值归一化为 0／1、保留位清零。
- 人工只提供了末步日志，未提供第 1～5 步的完整串口输出；上一条结构性推论是判定依据，已如实记录而非补造日志。

### 3. Settings UI 与五 App 回归

- 人工将普通固件烧回目标板后确认 Settings 四项详情页文案与布局、Wi-Fi／Sound 页不出现开关、五 App 进入／返回与既有行为“都没问题”。
- **本次只有人工口头确认，未提供补充串口日志**，因此不补造 `open`／`closed` 时间戳、`screen children` 数值或焦点索引。

### 未取得证据的部分

- `nvs_set_blob()`／`nvs_commit()` 提交失败与 `nvs_flash_init()`／`nvs_open()` 直接失败两条路径无法在不引入自定义分区表、不破坏其他 Service 未来数据的前提下构造，只按源码检查确认，不阻塞收口。

## 当前交付与未验证范围

- 已完成：节点 9 范围、配置字段、默认值、NVS schema、恢复边界、公开接口语义、UI 边界、检查点和人工验收标准已确定。
- 已实现（2026-09-20）：Service 源码、CMake 接入、启动链接入、Settings 状态页改造、6 步自测入口与项目说明更新。详见下方“实现记录”。
- 尚未验证：只有两条无法构造的失败路径（`nvs_set_blob()`／`nvs_commit()` 提交失败、`nvs_flash_init()`／`nvs_open()` 直接失败），按源码检查确认且不阻塞收口。
- 证据边界：自测只提供了末步串口日志，第 1～5 步按“标记只在整步通过后推进”的结构性推论判定；Settings 四页与五 App 回归为人工口头确认，均无补充串口日志。
- 已人工验证（2026-09-20）：ESP-IDF 6.1 普通构建与烧录通过（`App version: 7088147-dirty`），首次启动 `source=defaults`、断电重启（`POWERON_RESET`）后 `source=nvs`，两次均输出 `Launcher ready, 5 app(s) registered`；6 步自测固件输出 `SETTINGS_SERVICE_SELF_TEST: PASS`；Settings UI 与五 App 回归通过。详见“验证结果”。
- 节点状态：已完成，功能验收无待办；上述未验证项与证据边界已在“验证结果”中如实记录。

## 实现记录（2026-09-20）

### 新增文件

- `main/services/xiaomiao_settings_service.h`：公开 `xiaomiao_settings_t`（仅 `wifi_auto_connect`、`sound_enabled` 两个业务字段）、`xiaomiao_settings_source_t` 四态枚举与 `init/get/set/source/last_error` 五个接口。NVS 头、schema 版本与保留位不进入公开结构。
- `main/services/xiaomiao_settings_service.c`：namespace `xiaomiao`、key `settings`、schema version 1；内部 `settings_blob_v1_t` 用固定宽度整数加 2 字节清零保留位（`_Static_assert(sizeof(...) == 8)` 固定布局），公开结构不按内存布局序列化。`init()` 一次性幂等（`s_initialized` 先置位，重复调用返回首次结果且不重写 Flash）；加载顺序为 key 缺失→写默认值、读取失败→降级、blob 非法→只重写本 key、有效→装载；`set()` 先校验、`nvs_commit()` 成功后才替换内存快照；`s_nvs_usable` 为假时不打开 namespace，直接返回首次 `init()` 的原始错误。
- `main/services/xiaomiao_settings_service_selftest.{h,c}`：`XIAOMIAO_SETTINGS_SERVICE_SELF_TEST` 自测构建入口。6 步覆盖缺失 key、有效 v1 重启恢复、未知版本、越界布尔值、短于 v1 的 blob、长于 v1 的 blob，步骤间用 `esp_restart()` 自动重启、以 `xiaomiao/selftest_stage` 键推进；自测独立声明一份 v1 布局镜像，布局漂移即失败；检查失败打印 `SETTINGS_SERVICE_SELF_TEST: FAIL` 并挂起（不 abort，避免自重启固件进入失败重启循环），末步打印 `SETTINGS_SERVICE_SELF_TEST: PASS` 并把 namespace 恢复干净。全程只用 `nvs_erase_key()` 处理自己拥有的两个 key，不调用 `nvs_flash_erase()`。

### 修改文件

- `main/CMakeLists.txt`：新增 `SERVICE_SRCS`（含自测条件追加）、`XIAOMIAO_SETTINGS_SERVICE_SELF_TEST` 选项与编译定义，`PRIV_REQUIRES` 增加 `nvs_flash`。
- `main/main.c`：普通构建在硬件与 LVGL 初始化前调用 `xiaomiao_settings_service_init()`，失败只记一条警告并继续启动；新增第四个互斥 `#elif` 形态把 Settings Service 自测接成独立构建。
- `main/apps/settings/xiaomiao_settings.c`：`settings_build_status()` 改为通用的 `settings_build_detail()`（页面主题／真实状态／一行上下文，状态色由调用方给出）；新增 `settings_build_system()`，每次进入通过 `get()`／`source()`／`last_error()` 读取一次真实状态，显示 `Loaded from NVS`／`Defaults applied`／`Defaults restored`／`Not persisted` 与 `NVS error 0x…`；`Display` 页改为陈述固定背光事实。App 仍不 include `nvs.h`／`nvs_flash.h`。
- `docs/project-overview.md`、`README.md`、`AGENTS.md`、`ROADMAP.md`：同步目录职责、启动链、Service 说明与“尚未验证”状态。

### 静态检查结果（Agent 执行，未编译）

- `git diff --check` 通过（仅既有 LF→CRLF 提示）；新增文件无行尾空白、无 Tab、纯 ASCII，仓库既有非 ASCII 说明文本未新增。
- 花括号配平：Service `.c` 36/36、`.h` 3/3、自测 `.c` 34/34、自测 `.h` 1/1、Settings `.c` 63/63、`main/main.c` 354/354、`main/CMakeLists.txt` 8/8。
- `nvs_*` 调用只出现在 `main/services/` 的 Service 与自测入口；`main/apps/settings/xiaomiao_settings.c` 与 `main/main.c` 内无 `nvs_`／`nvs_flash` 调用；`nvs_flash_erase` 在全仓库仅出现在自测文件的一句注释中（说明未调用）。
- `main/services/` 内无 `esp_wifi`／`ledc_`／`gpio_`／`i2c_`／`spi_`／`sdmmc`／`sdspi`／`xTaskCreate`／`xQueue`／`xSemaphore`／`xEventGroup`／`esp_timer_`／`lv_` 调用；未新增自定义分区表或 `partitions.csv`。
- 对照本仓库锁定的 ESP-IDF 6.1 源码核对所用行为：`nvs_get_blob()` 在存储条目长于缓冲区时返回 `ESP_ERR_NVS_INVALID_LENGTH` 并把实际长度写回 `*length`，短于缓冲区时返回 `ESP_OK` 并回写真实长度（`nvs_api.cpp:588-616`）；`Storage::writeItem()` 覆盖同 key 的旧条目（含长度不同与旧类型不同，`nvs_storage.cpp:392-551`）；`Page::findItem()` 对同 key 不同类型返回 `ESP_ERR_NVS_TYPE_MISMATCH`（`nvs_page.cpp:1128-1134`）。ESP-IDF 6.1 默认 C 标准为 `gnu23`（`tools/cmake/build.cmake:207`），`_Static_assert` 可用。

### 本节点已识别但未构造的路径

- “NVS 不可用降级”（`nvs_flash_init()` 或 `nvs_open()` 直接失败）无法在不引入自定义分区表、不破坏其他 Service 未来数据的前提下构造，因此**只能靠源码检查确认**：该路径只记录原始错误码、装载内存默认值并把来源置为 `DEGRADED`，不使用 `ESP_ERROR_CHECK`、不擦分区、不阻塞启动。可构造的降级路径（读取失败）已由自测第 6 步覆盖。
- 存储条目长于 v1 时落在恢复规则 3（降级）而非规则 2（修复），原因是 NVS 层在数据到达 Service 之前就以 `ESP_ERR_NVS_INVALID_LENGTH` 拒绝过短的缓冲区。该行为更保守：既不覆盖也不破坏未知格式的数据，与“不按 `sizeof` 差异盲目读取旧 blob”的既定要求一致，已由自测第 6 步断言“未覆盖已存条目”。

## 修订记录

- 2026-09-20（立项同日，修订检查点 4）：原检查点 4 要求“Games、PC Monitor、Tools、Settings、Hardware Test 各完成至少一次进入／返回”与“Settings 至少完成 5 次进入／返回，四个详情页均进入并返回一次”。节点 5、6、7、8 的同类计数与覆盖条款连续四次因实测样本不足走人工豁免（最近一次即节点 8 的“五个 App 各完成至少一次”，Games 与 PC Monitor 未进入），说明该类口径在单次 Monitor 会话内难以稳定达成，而它要证明的实质（open／close 严格成对、`screen children` 恒定、本节点改动涉及的页面可用、无崩溃与输入失效）与计数本身无关。
- 修订后：计数与穷举覆盖改为实质口径——进入／返回严格成对 + `screen children` 恒定 + 本节点改动涉及的页面全部进入；未改动源码的 App 只要求注册顺序、启动链注册日志与基线三类证据。修订不降低任何实质要求，`screen children=2` 这一对象生命周期证据反而被写成了显式门槛。
- 同批次同文件内已同步的两处表述：检查点 4 与“人工验证命令与预期结果”末条。仅改文档，未改源码。
