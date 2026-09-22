/*
 * Storage Service (goal node 14).
 *
 * Fourth System Service. It owns the whole MicroSD lifecycle on the
 * shared SPI2 bus: the SDSPI device, the FATFS mount at the fixed
 * mount point, the unmount path and the deterministic error state.
 * The board wiring (SPI host, CS GPIO, clock) is passed in by app_main
 * as an init configuration - this Service never hardcodes a second
 * set of pin definitions.
 *
 * Layering follows the existing Services: Apps and the Framework only
 * call this interface and read state through a copied snapshot. They
 * never include sdspi/sdmmc/FATFS headers, never see the sdmmc_card_t
 * handle and never touch the CS GPIO themselves. The Service does not
 * own the SPI bus: it never calls spi_bus_initialize() or
 * spi_bus_free(), those belong to the LCD bring-up that runs before
 * this Service is initialized (goal node 14, decisions 2, 4 and 10).
 *
 * Synchronization (goal node 14, decision 5): mount() and unmount()
 * are synchronous and serialized by the Service lock. There is no
 * worker task, no queue and no retry timer; callers must not invoke
 * the API from an ISR.
 *
 * GPIO22 fix carried over from goals/20260921-2101: IDF 6.1 sdspi
 * deinit paths configure the CS pin back to input without revoking
 * its GPIO ownership, which is what used to print
 * `gpio: conflict found for GPIO[22]` on every retry. Every cleanup
 * and every unmount therefore resets the CS pin BEFORE the SDSPI
 * device is removed, and every mount attempt clears stale ownership
 * before creating the device again (goal node 14, decision 8).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed mount point consumed by node 15; not configurable at runtime. */
#define XIAOMIAO_STORAGE_MOUNT_POINT "/sdcard"
#define XIAOMIAO_STORAGE_CARD_NAME_MAX 24

/*
 * Lifecycle of the card slot. UNINITIALIZED means init() has not been
 * called yet. ERROR means the last mount attempt failed while the
 * Service stays retryable; UNMOUNTED is reached only through a
 * successful unmount (goal node 14, "public interface contract").
 */
typedef enum {
    XIAOMIAO_STORAGE_UNINITIALIZED = 0,
    XIAOMIAO_STORAGE_UNMOUNTED,
    XIAOMIAO_STORAGE_MOUNTED,
    XIAOMIAO_STORAGE_ERROR,
} xiaomiao_storage_state_t;

/*
 * Board wiring owned by main: the shared SPI host that already carries
 * the TFT, the SD CS GPIO and the clock limit. Values are validated in
 * init(); the Service stores a copy.
 */
typedef struct {
    int host_id;
    int cs_gpio;
    uint32_t max_freq_khz;
} xiaomiao_storage_config_t;

/*
 * One consistent view of the slot. card_name is NUL-terminated and
 * empty when no card is mounted; capacity is the 64-bit byte count so
 * large cards cannot overflow - the UI formats MiB itself. last_error
 * carries the raw esp_err_t of the last failed operation for
 * diagnostics; it is never replaced by a generic error code.
 */
typedef struct {
    xiaomiao_storage_state_t state;
    bool mounted;
    char card_name[XIAOMIAO_STORAGE_CARD_NAME_MAX];
    uint64_t capacity_bytes;
    esp_err_t last_error;
} xiaomiao_storage_snapshot_t;

/*
 * Validate the configuration, create the Service state and try the
 * first mount. Call it after lcd_init() has set up the shared SPI2
 * bus (goal node 14, decision 4).
 *
 * Idempotent: only the first call does anything, later calls return
 * the first result. Returns ESP_ERR_INVALID_ARG on a NULL config or
 * an invalid host/CS/frequency without creating any resource. The
 * return value is the result of the first mount attempt: a missing or
 * unresponsive card returns the raw error and leaves the snapshot in
 * ERROR, but the Service stays initialized and mount() can be retried
 * (goal node 14, "public interface contract").
 */
esp_err_t xiaomiao_storage_service_init(const xiaomiao_storage_config_t *config);

/*
 * Mount the card at XIAOMIAO_STORAGE_MOUNT_POINT. Synchronous and
 * serialized; never call from an ISR.
 *
 * Idempotent: while mounted this returns ESP_OK without creating a
 * second device. On success the MOUNTED snapshot (card name, capacity,
 * ESP_OK) commits only after the FATFS mount succeeded; any failure
 * releases the resources acquired so far, commits ERROR with the raw
 * esp_err_t and returns that error. The card is never formatted,
 * written or repaired (format_if_mount_failed=false, goal node 14,
 * decision 9).
 */
esp_err_t xiaomiao_storage_mount(void);

/*
 * Unmount the card and release the SDSPI device. Synchronous and
 * serialized; never call from an ISR.
 *
 * Returns ESP_ERR_NOT_FOUND when nothing is mounted, leaving the
 * snapshot untouched. On success the snapshot returns to UNMOUNTED.
 * On failure the card information is kept (the slot is still mounted)
 * and the raw esp_err_t is recorded and returned - the Service never
 * claims an unmount that did not happen (goal node 14, "public
 * interface contract"). The CS pin is reset before the driver releases
 * it, matching the verified GPIO22 fix sequence.
 */
esp_err_t xiaomiao_storage_unmount(void);

/*
 * Copy the current snapshot into *snapshot. Safe on a NULL pointer
 * (no-op) and readable before initialization (UNINITIALIZED). The
 * copy is taken under the Service lock, so state, name, capacity and
 * error always belong to the same moment (goal node 14, decision 7).
 */
void xiaomiao_storage_get_snapshot(xiaomiao_storage_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif