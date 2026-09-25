# 节点 15A：Assets 与只读文件系统基础

## 元信息

- 父施工总纲：`goals/20260922-1701-assets-filesystem-chinese-font.md`
- 状态：已实施（CP1～CP3 编码与静态检查完成），待人工验证（CP4）
- 创建时间：2026-09-22 17:11（北京时间）
- 前置条件：节点 14 已完成
- 后续依赖：节点 15B 字体包与运行时必须在本 Goal 完成并取得必要验证后开始

## 目标与预期行为

建立节点 15 的文件系统基础：把现有 1.5 MB `assets` 分区构建为随固件烧录的 SPIFFS 镜像，启动时只读挂载到 `/assets`；提供安全的 `asset:/` 与显式 `sd:/` 逻辑路径、资源状态快照和有界只读文件接口。无 Assets 镜像、无 SD 卡或 SD 挂载失败都不得阻塞 Launcher。

本 Goal 不生成中文字体、不实现字体驱动、不翻译 UI。完成后只保证后续字体和图标有稳定、可验证的资源访问入口。

## 必要背景与文件入口

- 分区：`partitions.csv` 已定义 `assets, data, spiffs, 0x220000, 0x180000`。
- SD 生命周期：`main/services/xiaomiao_storage_service.{h,c}`，固定挂载点 `/sdcard`。
- 构建入口：根 `CMakeLists.txt`、`main/CMakeLists.txt`、`sdkconfig.defaults`、`sdkconfig.ci`。
- 父 Goal 已确认：生产字体只使用 `asset:/`；SD 资源只能显式使用 `sd:/`，不得覆盖内置资源。

## 已确定接口与实现决策

建议新增：

```text
main/services/xiaomiao_assets_service.h
main/services/xiaomiao_assets_service.c
assets/
  manifest.txt
  fonts/
  icons/
  fixtures/
  LICENSES/
```

公开接口至少包含：

```c
esp_err_t xiaomiao_assets_service_init(void);
void xiaomiao_assets_get_snapshot(xiaomiao_assets_snapshot_t *snapshot);

esp_err_t xiaomiao_asset_open(
    xiaomiao_asset_source_t source,
    const char *relative_path,
    xiaomiao_asset_file_t **out_file);
esp_err_t xiaomiao_asset_read(...);
esp_err_t xiaomiao_asset_seek(...);
esp_err_t xiaomiao_asset_get_size(...);
void xiaomiao_asset_close(...);
```

约束：

1. `/assets` 使用 `format_if_mount_failed=false`，初始化幂等，失败保存原始 `esp_err_t` 并继续启动。
2. 公开文件句柄不暴露 `FILE *`、SPIFFS、FATFS 或 Storage Service 内部对象。
3. `asset:/path` 只能解析到 `/assets/path`；`sd:/path` 只能解析到 `/sdcard/xiaomiao/path`。
4. 拒绝空路径、绝对路径、`..`、反斜杠、盘符、NUL 截断、超长路径和越界目录。
5. `sd:/` 打开前读取 Storage Service 最新快照；未挂载时确定失败，不自动重试挂载。
6. 首版只读，只提供 open／read／seek／size／close 和有界目录遍历。
7. 不新增 Task、队列或 timer；不持有共享 SPI2 或 SD 驱动资源。
8. SPIFFS 镜像必须加入正常 `idf.py flash` 参数；普通构建不联网、不动态生成字体。

## 允许修改范围

- `main/services/xiaomiao_assets_service.{h,c}`。
- `main/main.c` 中 Assets Service 的启动链接入和错误日志。
- `main/CMakeLists.txt`、根 CMake 配置、`sdkconfig.defaults`、`sdkconfig.ci`。
- 根 `assets/` 目录的空目录占位、manifest、测试 fixture 和许可证入口。
- `.gitignore` 中仅与已确定 Assets 源文件相关的精确例外。
- 本 Goal、父 Goal、ROADMAP 和必要架构文档。

## 禁止修改范围

- 不生成或提交完整中文字体，不实现 `lv_font_t` 回调。
- 不修改 Launcher、业务 App 和 Hardware Test 文案或布局。
- 不调用 `sdspi_host_*`、`esp_vfs_fat_*`，不操作 GPIO22，不改 Storage Service 生命周期语义。
- 不写入、格式化、删除或修复 SPIFFS／SD 数据。
- 不修改 NVS、phy_init、factory 与 assets 的偏移和容量。
- 不实现音乐播放、游戏、文件管理器、下载或 OTA。

## 检查点与验收

### CP1：SPIFFS 镜像和挂载

- 为 `assets` 分区生成 SPIFFS 镜像并加入 flash 参数。
- Assets Service 幂等挂载 `/assets`，发布 READY／DEGRADED／ERROR 快照、容量和原始错误。
- 挂载失败只记录一条确定性错误并继续 Launcher。

预期结果：带有效镜像时可读 manifest；镜像缺失或损坏时启动不阻塞且状态可诊断。

### CP2：逻辑路径和只读文件 API

- 覆盖合法 `asset:/`、合法 `sd:/`、无 SD、短读、EOF、seek、重复 close。
- 覆盖 `../`、绝对路径、反斜杠、盘符、重复根前缀、过长路径和 NULL 参数。

预期结果：所有路径都被限制在各自根目录；失败不泄漏文件句柄或返回半初始化对象。

### CP3：自测与静态边界

新增 `XIAOMIAO_ASSETS_SERVICE_SELF_TEST`，与既有自测互斥，成功输出：

```text
ASSETS_SERVICE_SELF_TEST: PASS
```

Agent 静态检查：

- 只有 Assets Service 注册 SPIFFS。
- 只有 Storage Service 管理 SD 生命周期。
- 业务 App 中没有 `/assets`、`/sdcard`、SPIFFS、FATFS、SDSPI 直接调用。
- 检查所有分配、打开和目录遍历的失败释放路径。
- 检查生成镜像尺寸小于 1.5 MB，flash args 偏移为 `0x220000`。

### CP4：人工验证

项目负责人执行 ESP-IDF 6.1 构建、烧录和 Monitor：

1. 正常镜像启动为 READY，manifest 可读。
2. 无 SD 冷启动仍进入 Launcher，`asset:/` 可用，`sd:/` 确定失败。
3. 插卡挂载后 `sd:/` 测试文件可读；安全卸载后新请求失败。
4. Assets 初始化失败注入不阻塞五个 App 和 Hardware Test。
5. 自测输出 `PASS`，普通固件恢复后正常启动。

Agent 不主动运行 `idf.py`、烧录或串口监视。

具体人工命令（Windows，先加载 IDF 6.1 环境；本地 `sdkconfig` 若为旧配置请先删除或重新生成，使 `CONFIG_SPIFFS_OBJ_NAME_LEN=48` 生效）：

```bash
idf.py -DXIAOMIAO_ASSETS_SERVICE_SELF_TEST=ON build
idf.py -p COM5 flash && idf.py -p COM5 monitor     # 预期 ASSETS_SERVICE_SELF_TEST: PASS
idf.py -DXIAOMIAO_ASSETS_SERVICE_SELF_TEST=OFF build
idf.py -p COM5 flash && idf.py -p COM5 monitor     # 预期 Assets READY、正常进 Launcher
```

另需记录：构建产物中 `assets` SPIFFS 镜像大小（须 < 1.5 MB）、flash 参数含 `0x220000` 的 assets 行，以及上述 1～5 各场景的串口日志或逐项确认。

## 完成标准

- CP1～CP3 实施及静态检查通过。
- CP4 取得人工构建、烧录、无 SD 和有 SD 证据。
- `/assets`、`asset:/`、`sd:/` 接口契约稳定，后续 15B 不需要绕过 Service。
- 状态、交付文件、未验证范围和证据已回写本 Goal、父 Goal、ROADMAP 与历史索引。

## 实施记录

- 2026-09-22 17:11 -- 从节点 15 总纲拆分本独立 Goal。状态：待实施。
- 2026-09-22 17:45 -- 完成 CP1～CP3 编码与 Agent 静态检查：新增 `main/services/xiaomiao_assets_service.{h,c}`（幂等只读挂载 `/assets`、READY／DEGRADED／ERROR 快照、`asset:/`→`/assets`／`sd:/`→`/sdcard/xiaomiao` 路径解析与拒绝规则、open／read／seek／size／close 与有界目录遍历，`sd:/` 前读取 Storage Service 快照且从不自动挂载）、`xiaomiao_assets_service_selftest.{h,c}`（路径规则表、init 幂等与容量、文件生命周期短读／EOF／seek、命名空间与截断，输出 `ASSETS_SERVICE_SELF_TEST: PASS`）与 `assets/` 下 manifest 和四个目录占位；`main/CMakeLists.txt` 注册源文件、`spiffs` 依赖、`XIAOMIAO_ASSETS_SERVICE_SELF_TEST` 编译开关和 `spiffs_create_partition_image(assets ../assets FLASH_IN_PROJECT)`；`main/main.c` 在 Agent Service 之后、传感器历史之前接入 `xiaomiao_assets_service_init()`（失败仅告警，不阻塞启动链）；`.gitignore` 增加 `!assets/**/*.bin` 精确例外。静态复核：调用边界（全仓库只有 Assets Service 调 `esp_vfs_spiffs_register`，业务 App 无 `/assets`、`/sdcard`、SPIFFS／FATFS／SDSPI 直接调用）、括号配平、路径解析拒绝表、失败释放路径、分区偏移 `0x220000` 与 1.5 MB 容量一致性均通过。**未运行 `idf.py build`、烧录或串口监视；CP4 全部场景待人工验证。** 状态：已实施，待人工验证。
- 2026-09-23 -- 人工 CP4 首轮自测实机运行发现并修复两个缺陷（项目负责人提供串口日志，Agent 定位并修改，待重跑确认）：
  1. **快照 padding 假失败**：`xiaomiao_assets_get_snapshot()` 逐字段赋值但不清零，`bool` 后的 3 字节结构填充使两份内容相同的快照 `memcmp` 不等（自测 line 178 FAIL）。已在函数入口 `memset` 清零。
  2. **自测栈溢出导致 panic 重启循环**：`test_file_lifecycle()` 假定 `manifest.txt` ≤ 64 字节，实际清单 519 字节，`fread(file, first, size)` 越过栈缓冲 455 字节破坏局部 `file` 指针，后续 `xiaomiao_asset_read` 用野 `FILE*`（`0xc000667e`）进 `clearerr` 触发 `LoadProhibited`（EXCVADDR `0xc0006686`），重启后再进自测形成循环。已改为按缓冲区容量钳制读取窗口并重写短读断言，不再依赖清单长度。生产代码路径（Fonts／Icons／Tools）不受此缺陷影响，两处均为 Service 快照与自测自身问题。
  - 同轮正向证据（保留在日志中）：SPIFFS 实机挂载成功，`state=1(READY) total=1438481 used=783371`，与主机侧 `assets/` 负载 774,340 字节加文件系统开销一致；分区表四槽偏移无漂移；步骤 1 路径规则表全过。CP4 其余场景待修复版重跑。
- 2026-09-23 -- 人工 CP4 第二轮自测（ELF `31c4e07bd`）：上一轮两处修复实机确认生效（无崩溃、重启不再循环，步骤 2 快照 `memcmp` 通过，步骤 3 钳制窗口读取通过），同时暴露两个此前被崩溃掩盖的 SPIFFS 平台行为差异，已修复（待第三轮重跑确认）：
  1. **`seek` 越过 EOF（自测 line 113 FAIL）**：SPIFFS 核心 `SPIFFS_lseek` 对 `offset > 文件长度` 返回 `SPIFFS_ERR_END_OF_OBJECT`（`spiffs_hydrogen.c` 第 628 行），而 FATFS 允许越过 EOF，原设计假设“seek 越过 EOF 后读 0 字节”只在 SD 侧成立。已在 `xiaomiao_asset_seek()` 内把超出 `file->size` 的目标钳制到 EOF（SPIFFS 允许 seek 到恰等于长度的位置，之后读取返回 0），两个命名空间对外契约统一为：seek 到 EOF 或更晚一律成功，随后的读返回 0 字节。头文件契约注释同步更新。
  2. **不存在目录的 `list` 返回 `ESP_OK`（自测 line 168 FAIL）**：SPIFFS 为扁平命名空间，`SPIFFS_opendir` 对从未写入的路径也成功并返回空目录流；`SPIFFS_stat` 只能按精确对象名解析、无法识别虚拟目录，因此不能用 `stat` 预判目录存在。改为以枚举结果判空：`xiaomiao_asset_list()` 在成功打开目录但零条目时返回 `ESP_ERR_NOT_FOUND`。代价：真正的空目录（当前镜像内不存在）与缺失目录不可区分，统一按 `ESP_ERR_NOT_FOUND` 处理，已写入头文件契约与本记录。SD 侧 FATFS 行为待有卡场景人工复核。
  - 本轮无生产代码新问题：两处均属 Service 对外契约细化，Font／Icons 调用方只做 `open/read/seek(界内)/get_size`，不受影响。
- 2026-09-23 -- CP4 第三轮 Assets 自测实机通过（ELF `be39aef26`，IDF v6.1，App 版本 `cf87593-dirty`）：步骤 1～4 无任何 FAIL，`ASSETS_SERVICE_SELF_TEST: PASS` 后正常挂起，不复启。确认生效的两个契约修复：seek 越 EOF 钳制（步骤 3）与空目录流返回 `ESP_ERR_NOT_FOUND`（步骤 4）。同轮日志补充 CP3 证据：SPIFFS 挂载 759→853 ms（约 94 ms）、`state=1(READY) total=1438481 used=783371` 复现一致；构建侧 `spiffsgen.py ... --obj-name-len=48` 输出确认 `CONFIG_SPIFFS_OBJ_NAME_LEN=48` 已进镜像参数、flash 命令含 `0x220000 assets.bin`；自测构建九项 `unused-function` 警告确认为 `#elif` 分支跳过启动链的预期产物（该构建 `xiaomiao.bin` 0x2f0a0≈192 KB，与常规 1,497 KB 区分）。至此 15A 自测项（CP4 第 1 场景）通过；剩余 CP4 场景（常规固件无 SD 启动、Tools Assets 页、有 SD 与卸载语义）随 15D CP4～CP5 人工验收继续。

## 交付结果

- CP1～CP3 交付源码与构建接入，见 2026-09-22 17:45 实施记录所列文件。
- `asset:/`、`sd:/` 与只读文件接口契约按本 Goal 约束冻结，15B 字体运行时直接复用 `xiaomiao_asset_open/read/seek/get_size`，无需绕过 Service。
- 未验证范围（保持“未验证”，不得记为通过）：SPIFFS 镜像实际生成与大小、烧录后 READY 状态与 manifest 实读、无 SD／有 SD 冷启动行为、`sd:/` 插拔语义、自测在目标板的 `PASS` 输出、Assets 初始化失败注入不阻塞五个 App——以上全部属 CP4，待项目负责人按本 Goal「CP4：人工验证」1～5 项执行并回传证据。
