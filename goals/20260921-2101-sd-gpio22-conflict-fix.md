# Goal：MicroSD GPIO22「冲突」定位与最小修复

状态：GPIO22 冲突修复已通过人工验证；SD 卡初始化超时与正常卡挂载仍待确认。

## 目标

定位 Hardware Test MicroSD 页重复挂载时报 `gpio: conflict found for GPIO[22]` 的根因，并实施最小修复。

## 根因结论（基于 IDF 6.1 源码逐行核实）

`gpio: conflict found for GPIO[22]` 不是真实引脚冲突，而是 IDF 6.1 sdspi 驱动的 GPIO 占用位图泄漏：

1. 首次挂载：`sdspi_host_init_device()` 经 `gpio_config(OUTPUT)` 配置 CS，内部调 `esp_gpio_reserve(BIT64(22))` 记入全局占用位图（`esp_driver_gpio/src/gpio.c:390`）。
2. `sdmmc_card_init failed (0x107)` 后，`esp_vfs_fat_sdspi_sdcard_init()` 的失败清理调用 `sdspi_host_remove_device()`，其 `deinit_slot()`（`esp_driver_sdspi/src/sdspi_host.c:231-271`）只把 CS 配回输入，从不调 `esp_gpio_revoke()`。GPIO22 从此留在占用表。
3. 后续每次挂载：`gpio_config(OUTPUT)` 中 `esp_gpio_reserve` 发现位已置 → 打警告（`esp_driver_gpio/src/gpio.c:400-404`），仅警告不失败，配置照常执行。与用户日志顺序一致：`sdmmc_card_init failed` 后紧跟 GPIO22 冲突警告，说明警告来自本次失败清理，而不只是下一次尝试。
4. 排除真实冲突：README 引脚表 GPIO22 仅分配 SD CS；`main/main.c` 内仅 `PIN_NUM_SD_CS` 一处使用。

## 首版修复与人工复核

- 首版修复在 `sd_try_mount()` 挂载前调用 `gpio_reset_pin(PIN_NUM_SD_CS)`，用于清理上一次失败留下的占用。
- 2026-09-21 21:29 的人工日志证明该修复不足：本次 `sdmmc_card_init()` 失败后的内部清理仍会先调用 `gpio_config(INPUT)`，因此仍出现 GPIO22 冲突警告。首版结论作废，不能作为验收通过依据。

## 新实现决策

- 不再调用 `esp_vfs_fat_sdspi_mount()` 一体化助手；`main/main.c` 手动执行 `host.init()`、`sdspi_host_init_device()`、`sdmmc_card_init()` 和 `esp_vfs_fat_mount_initialized()`。
- 失败清理统一先调用 `gpio_reset_pin(PIN_NUM_SD_CS)`，再调用 `sdspi_host_remove_device()`，最后释放 `sdmmc_card_t`，从调用顺序上消除本次失败清理产生的 GPIO22 警告。
- 成功卸载前同样先复位 CS，再调用 `esp_vfs_fat_sdcard_unmount()`，覆盖卸载后重新挂载路径。
- `vfs_fat_internal.h` 只用于 IDF 6.1 已提供的 `esp_vfs_fat_mount_initialized()` 声明；不修改外部 ESP-IDF 源码，不新增 Service。
- 不在本任务处理 `sdmmc_card_init failed (0x107)`（`ESP_ERR_TIMEOUT`，卡不响应）：需人工插入已知良好卡，才能区分“未插卡预期行为”与共享 SPI 总线问题。

## 检查点与验收证据

- [x] 根因定位（IDF 源码静态核实，见上）。
- [x] 首版代码修改：`main/main.c` `sd_try_mount()` 增加 `gpio_reset_pin(PIN_NUM_SD_CS)`；人工验证证明不足，已被新实现取代。
- [x] 新代码修改（2026-09-21 21:48）：拆分 SDSPI 初始化与 FATFS 挂载，失败清理和成功卸载均在设备移除前复位 CS。
- [x] 新实现静态复核（2026-09-21 21:48）：API 调用顺序、卡初始化失败、FATFS 挂载失败、成功卸载和资源释放路径均已核对；未运行 ESP-IDF 构建。
- [x] 首版人工验证（2026-09-21 21:29，未通过）：用户提供的烧录后日志中至少 6 次出现 `sdmmc_card_init failed (0x107)`，且每次约 1 ms 后仍出现 `gpio: conflict found for GPIO[22]`；该版结论已作废。
- [x] 新实现人工验证（2026-09-21 21:56）：连续 9 次 `sdmmc_card_init failed (0x107)` 均未出现 `gpio: conflict found for GPIO[22]`；GPIO22 冲突修复验收通过。
- [ ] 插已知良好卡复测挂载成功与否（可选，用于区分 0x107 根因）。

## 既有人工验证结果

- `gpio_reset_pin(PIN_NUM_SD_CS)` 位于挂载调用之前，但本次失败日志仍在每次 `sdmmc_card_init` 超时之后紧跟 GPIO22 冲突警告。该时序说明首版修复最多清理了下一次尝试开始前的残留状态，未消除本次失败清理路径产生的警告；新实现已按该时序重新拆分清理路径，当前已通过实机复测。
- 日志只证明卡初始化超时仍存在；未提供已知良好 SD 卡的挂载成功证据，因此 `0x107` 的具体硬件或共享 SPI 根因仍未确认。

## 最新人工验证结果

- 2026-09-21 21:56 的烧录后日志显示，Hardware Test 中连续触发 9 次 SD 卡初始化，均只输出 `sdmmc_card_init failed (0x107)`，没有任何 `gpio: conflict found for GPIO[22]`。随后 Hardware Test 正常关闭、重新打开并返回 Launcher，`screen children=2` 保持正常。
- 本次未插入或未确认已知良好 SD 卡，尚不能判断 `0x107` 是无卡预期行为还是实际 SPI／卡硬件问题。

## 未验证范围

- 新实现已完成“重复无卡重试不产生 GPIO22 冲突”的实机验证；尚未取得已知良好 SD 卡挂载成功证据。
