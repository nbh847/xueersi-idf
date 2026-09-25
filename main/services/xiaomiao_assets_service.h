/*
 * Assets Service (goal node 15A).
 *
 * Fifth System Service. It owns the read-only mount of the 1.5 MB
 * `assets` SPIFFS partition at /assets and the logical resource
 * namespaces `asset:/` and `sd:/`. Apps and the Framework never call
 * esp_vfs_spiffs_register(), never build /assets or /sdcard physical
 * paths, and never see FILE* or SPIFFS internals.
 *
 * The partition is never formatted, written, deleted or repaired at
 * runtime (format_if_mount_failed=false, no write API): resources only
 * change with a full firmware + image flash. A missing or damaged
 * image costs the resources, never the boot: init() records the raw
 * esp_err_t and the Launcher still starts.
 *
 * sd:/ requests are resolved against /sdcard/xiaomiao/ only after the
 * Storage Service snapshot reports the card mounted; this Service
 * never triggers, waits for or retries an SD mount. SD absence only
 * makes explicit sd:/ opens fail deterministically.
 *
 * No task, queue or timer is created. The mount state is fixed after
 * init(); file handles are independent, small and always released by
 * close().
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_ASSETS_PARTITION "assets"
#define XIAOMIAO_ASSETS_MOUNT_POINT "/assets"
/* Logical relative path budget; the physical prefix adds a small fixed
 * amount that is included in internal path buffers. */
#define XIAOMIAO_ASSETS_PATH_MAX 96
#define XIAOMIAO_ASSETS_NAME_MAX 64

/*
 * UNINITIALIZED: init() has not run. READY: /assets is mounted and
 * manifest.txt is readable. DEGRADED: mounted but the manifest is
 * missing or unreadable, i.e. the image is empty or damaged. ERROR:
 * the mount failed; last_error keeps the raw esp_err_t. The state is
 * published through a copied snapshot and never blocks the caller.
 */
typedef enum {
    XIAOMIAO_ASSETS_UNINITIALIZED = 0,
    XIAOMIAO_ASSETS_READY,
    XIAOMIAO_ASSETS_DEGRADED,
    XIAOMIAO_ASSETS_ERROR,
} xiaomiao_assets_state_t;

typedef struct {
    xiaomiao_assets_state_t state;
    bool mounted;
    uint32_t total_bytes;
    uint32_t used_bytes;
    esp_err_t last_error;
} xiaomiao_assets_snapshot_t;

/* Logical namespace selector. The caller passes the bare relative path;
 * the Service owns the physical prefixing. */
typedef enum {
    XIAOMIAO_ASSET_SOURCE_ASSET = 0, /* asset:/x -> /assets/x        */
    XIAOMIAO_ASSET_SOURCE_SD,        /* sd:/x    -> /sdcard/xiaomiao/x */
} xiaomiao_asset_source_t;

/* Opaque read-only file handle. Never exposed as FILE*. */
typedef struct xiaomiao_asset_file xiaomiao_asset_file_t;

typedef struct {
    char name[XIAOMIAO_ASSETS_NAME_MAX];
    bool is_dir;
    uint32_t size_bytes;
} xiaomiao_asset_entry_t;

/*
 * Mount /assets read-only and probe manifest.txt. Idempotent: only the
 * first call registers the filesystem, later calls return the first
 * result. A failure is logged once with the raw error, kept in the
 * snapshot and must never stop the caller's boot sequence.
 */
esp_err_t xiaomiao_assets_service_init(void);

/* Copy a consistent snapshot. NULL is a no-op. Readable before init. */
void xiaomiao_assets_get_snapshot(xiaomiao_assets_snapshot_t *snapshot);

/*
 * Validate a caller-supplied relative path against the namespace rules
 * (goal node 15A, constraints 3-5): non-empty, no absolute path, no
 * leading/trailing/double separator, no "." or ".." segment, no
 * backslash, colon or control character, length <= XIAOMIAO_ASSETS_PATH_MAX.
 * Returns ESP_OK or ESP_ERR_INVALID_ARG/ESP_ERR_INVALID_SIZE.
 */
esp_err_t xiaomiao_assets_validate_path(const char *relative_path);

/*
 * Open a file read-only from asset:/ or sd:/. Returns
 * ESP_ERR_INVALID_ARG for an invalid path, ESP_ERR_INVALID_STATE when
 * the namespace is unavailable (/assets not mounted, or the SD card
 * not mounted - without triggering any mount), ESP_ERR_NOT_FOUND when
 * the file does not exist, ESP_ERR_NO_MEM on allocation failure. On
 * success *out_file is a fully initialized handle; on failure it is
 * left untouched.
 */
esp_err_t xiaomiao_asset_open(xiaomiao_asset_source_t source,
                              const char *relative_path,
                              xiaomiao_asset_file_t **out_file);

/* Read up to buffer_len bytes from the current position. *out_read
 * reports the actual count; a short read at EOF still returns ESP_OK. */
esp_err_t xiaomiao_asset_read(xiaomiao_asset_file_t *file,
                              void *buffer,
                              size_t buffer_len,
                              size_t *out_read);

/* Absolute seek from the start of the file. Offsets at or past the end
 * are clamped to EOF: the call succeeds and the next read returns zero
 * bytes (SPIFFS itself rejects seeks past EOF; the Service hides that
 * namespace difference). */
esp_err_t xiaomiao_asset_seek(xiaomiao_asset_file_t *file, uint32_t offset);

/* Total size captured at open time. The image is read-only, so the
 * value never changes while the handle is alive. */
esp_err_t xiaomiao_asset_get_size(const xiaomiao_asset_file_t *file, uint32_t *out_size);

/* Close and free the handle. NULL is a no-op. After close() returns
 * the handle must not be used again. */
void xiaomiao_asset_close(xiaomiao_asset_file_t *file);

/*
 * Bounded, non-recursive directory listing. relative_path may be NULL
 * or "" for the namespace root, otherwise it must pass validation.
 * At most max_entries entries are filled; a full output buffer means
 * the listing was truncated. Returns the same errors as open() plus
 * ESP_ERR_NOT_FOUND for a missing directory. SPIFFS has no directory
 * objects, so an empty listing is treated as absence: empty directories
 * also return ESP_ERR_NOT_FOUND.
 */
esp_err_t xiaomiao_asset_list(xiaomiao_asset_source_t source,
                              const char *relative_path,
                              xiaomiao_asset_entry_t *entries,
                              int max_entries,
                              int *out_count);

#ifdef __cplusplus
}
#endif
