/*
 * Assets Service implementation (goal node 15A).
 *
 * Only this file calls esp_vfs_spiffs_register()/esp_spiffs_info().
 * SD access goes exclusively through the Storage Service public
 * snapshot; this file never includes sdspi/FATFS headers and never
 * mounts or unmounts the card.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"

#include "services/xiaomiao_assets_service.h"
#include "services/xiaomiao_storage_service.h"

static const char *TAG = "assets";

#define ASSETS_MAX_OPEN_FILES 6
#define ASSETS_MANIFEST_PATH  XIAOMIAO_ASSETS_MOUNT_POINT "/manifest.txt"
/* Physical prefix budget: "/sdcard/xiaomiao/" is the longest root and
 * every relative path is already capped by validate_path(). */
#define ASSETS_FULL_PATH_MAX  (XIAOMIAO_ASSETS_PATH_MAX + 32)

typedef struct {
    bool inited;
    bool mounted;
    xiaomiao_assets_state_t state;
    uint32_t total_bytes;
    uint32_t used_bytes;
    esp_err_t last_error;
} assets_state_t;

static assets_state_t s_assets;

struct xiaomiao_asset_file {
    FILE *fh;
    uint32_t size;
};

esp_err_t xiaomiao_assets_service_init(void)
{
    if (s_assets.inited) {
        return s_assets.last_error;
    }
    s_assets.inited = true;
    s_assets.state = XIAOMIAO_ASSETS_ERROR;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = XIAOMIAO_ASSETS_MOUNT_POINT,
        .partition_label = XIAOMIAO_ASSETS_PARTITION,
        .max_files = ASSETS_MAX_OPEN_FILES,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        s_assets.last_error = err;
        ESP_LOGE(TAG, "mount %s failed: %s (0x%x), continuing without built-in assets",
                 XIAOMIAO_ASSETS_MOUNT_POINT, esp_err_to_name(err), (unsigned)err);
        return err;
    }

    s_assets.mounted = true;
    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(XIAOMIAO_ASSETS_PARTITION, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spiffs info failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        total = 0;
        used = 0;
    }
    s_assets.total_bytes = (uint32_t)total;
    s_assets.used_bytes = (uint32_t)used;

    FILE *manifest = fopen(ASSETS_MANIFEST_PATH, "rb");
    if (manifest != NULL) {
        fclose(manifest);
        s_assets.state = XIAOMIAO_ASSETS_READY;
    } else {
        s_assets.state = XIAOMIAO_ASSETS_DEGRADED;
        ESP_LOGW(TAG, "%s not readable: assets image is empty or damaged", ASSETS_MANIFEST_PATH);
    }
    s_assets.last_error = ESP_OK;
    ESP_LOGI(TAG, "assets ready: state=%d total=%u used=%u",
             (int)s_assets.state, (unsigned)s_assets.total_bytes, (unsigned)s_assets.used_bytes);
    return ESP_OK;
}

void xiaomiao_assets_get_snapshot(xiaomiao_assets_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    /* Zero first: the bool/enum layout carries padding that callers may
     * compare with memcmp (self test step 2), and uninitialized bytes
     * there made two equal snapshots compare unequal on target
     * (2026-09-23 device run). */
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->state = s_assets.inited ? s_assets.state : XIAOMIAO_ASSETS_UNINITIALIZED;
    snapshot->mounted = s_assets.mounted;
    snapshot->total_bytes = s_assets.total_bytes;
    snapshot->used_bytes = s_assets.used_bytes;
    snapshot->last_error = s_assets.last_error;
}

esp_err_t xiaomiao_assets_validate_path(const char *relative_path)
{
    if (relative_path == NULL || relative_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strnlen(relative_path, XIAOMIAO_ASSETS_PATH_MAX + 2);
    if (len > XIAOMIAO_ASSETS_PATH_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (relative_path[0] == '/') {
        return ESP_ERR_INVALID_ARG;
    }
    if (relative_path[len - 1] == '/') {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pos = 0;
    while (pos < len) {
        size_t start = pos;
        while (pos < len && relative_path[pos] != '/') {
            char c = relative_path[pos];
            /* Backslash, colon and control characters never appear in a
             * legal relative path: they also block drive letters,
             * repeated root prefixes (asset:/... fed back in) and path
             * tricks on other platforms. */
            if (c == '\\' || c == ':' || (unsigned char)c < 0x20 || c == 0x7f) {
                return ESP_ERR_INVALID_ARG;
            }
            pos++;
        }
        size_t seg_len = pos - start;
        if (seg_len == 0) {
            return ESP_ERR_INVALID_ARG; /* empty or doubled separator */
        }
        if (seg_len == 1 && relative_path[start] == '.') {
            return ESP_ERR_INVALID_ARG;
        }
        if (seg_len == 2 && relative_path[start] == '.' && relative_path[start + 1] == '.') {
            return ESP_ERR_INVALID_ARG;
        }
        if (pos < len) {
            pos++; /* skip the separator */
        }
    }
    return ESP_OK;
}

static esp_err_t assets_gate_source(xiaomiao_asset_source_t source)
{
    if (source == XIAOMIAO_ASSET_SOURCE_ASSET) {
        return s_assets.mounted ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (source != XIAOMIAO_ASSET_SOURCE_SD) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Deterministic failure without ever starting a mount (decision 5). */
    xiaomiao_storage_snapshot_t sd;
    xiaomiao_storage_get_snapshot(&sd);
    return sd.mounted ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t assets_build_path(xiaomiao_asset_source_t source,
                                   const char *relative_path,
                                   bool relative_required,
                                   char *out,
                                   size_t out_len)
{
    const char *root = (source == XIAOMIAO_ASSET_SOURCE_ASSET)
                           ? XIAOMIAO_ASSETS_MOUNT_POINT
                           : XIAOMIAO_STORAGE_MOUNT_POINT "/xiaomiao";
    int n;
    if (relative_path == NULL || relative_path[0] == '\0') {
        if (relative_required) {
            return ESP_ERR_INVALID_ARG;
        }
        n = snprintf(out, out_len, "%s", root);
    } else {
        n = snprintf(out, out_len, "%s/%s", root, relative_path);
    }
    if (n < 0 || (size_t)n >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t xiaomiao_asset_open(xiaomiao_asset_source_t source,
                              const char *relative_path,
                              xiaomiao_asset_file_t **out_file)
{
    if (out_file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = xiaomiao_assets_validate_path(relative_path);
    if (err != ESP_OK) {
        return err;
    }
    err = assets_gate_source(source);
    if (err != ESP_OK) {
        return err;
    }
    char full[ASSETS_FULL_PATH_MAX];
    err = assets_build_path(source, relative_path, true, full, sizeof(full));
    if (err != ESP_OK) {
        return err;
    }

    xiaomiao_asset_file_t *file = calloc(1, sizeof(*file));
    if (file == NULL) {
        return ESP_ERR_NO_MEM;
    }
    file->fh = fopen(full, "rb");
    if (file->fh == NULL) {
        free(file);
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(file->fh, 0, SEEK_END) != 0) {
        fclose(file->fh);
        free(file);
        return ESP_FAIL;
    }
    long size = ftell(file->fh);
    if (size < 0) {
        fclose(file->fh);
        free(file);
        return ESP_FAIL;
    }
    file->size = (uint32_t)size;
    rewind(file->fh);
    *out_file = file;
    return ESP_OK;
}

esp_err_t xiaomiao_asset_read(xiaomiao_asset_file_t *file,
                              void *buffer,
                              size_t buffer_len,
                              size_t *out_read)
{
    if (file == NULL || file->fh == NULL || buffer == NULL || out_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    clearerr(file->fh);
    size_t got = fread(buffer, 1, buffer_len, file->fh);
    *out_read = got;
    if (got == 0 && buffer_len > 0 && ferror(file->fh)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t xiaomiao_asset_seek(xiaomiao_asset_file_t *file, uint32_t offset)
{
    if (file == NULL || file->fh == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* SPIFFS refuses lseek beyond the last byte while FATFS allows it.
     * Clamping gives both namespaces one contract: seeking at or past
     * EOF succeeds and the next read returns zero bytes. Device finding
     * on 2026-09-23 (assets self test run 2). */
    if (offset > file->size) {
        offset = file->size;
    }
    if (fseek(file->fh, (long)offset, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t xiaomiao_asset_get_size(const xiaomiao_asset_file_t *file, uint32_t *out_size)
{
    if (file == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_size = file->size;
    return ESP_OK;
}

void xiaomiao_asset_close(xiaomiao_asset_file_t *file)
{
    if (file == NULL) {
        return;
    }
    if (file->fh != NULL) {
        fclose(file->fh);
    }
    free(file);
}

esp_err_t xiaomiao_asset_list(xiaomiao_asset_source_t source,
                              const char *relative_path,
                              xiaomiao_asset_entry_t *entries,
                              int max_entries,
                              int *out_count)
{
    if (entries == NULL || max_entries <= 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (relative_path != NULL && relative_path[0] != '\0') {
        esp_err_t err = xiaomiao_assets_validate_path(relative_path);
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = assets_gate_source(source);
    if (err != ESP_OK) {
        return err;
    }
    char dir_path[ASSETS_FULL_PATH_MAX];
    err = assets_build_path(source, relative_path, false, dir_path, sizeof(dir_path));
    if (err != ESP_OK) {
        return err;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    int count = 0;
    struct dirent *de;
    while (count < max_entries && (de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '\0') {
            continue;
        }
        char child[ASSETS_FULL_PATH_MAX];
        int n = snprintf(child, sizeof(child), "%s/%s", dir_path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) {
            continue;
        }
        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }
        size_t name_len = strnlen(de->d_name, XIAOMIAO_ASSETS_NAME_MAX);
        if (name_len >= XIAOMIAO_ASSETS_NAME_MAX) {
            ESP_LOGW(TAG, "entry name too long, skipped: %s", child);
            continue;
        }
        strncpy(entries[count].name, de->d_name, XIAOMIAO_ASSETS_NAME_MAX - 1);
        entries[count].name[XIAOMIAO_ASSETS_NAME_MAX - 1] = '\0';
        entries[count].is_dir = S_ISDIR(st.st_mode);
        entries[count].size_bytes = S_ISDIR(st.st_mode) ? 0 : (uint32_t)st.st_size;
        count++;
    }
    closedir(dir);
    *out_count = count;
    if (count == 0) {
        /* SPIFFS opendir() also succeeds for paths that were never
         * written (flat namespace, no directory object to stat): an
         * empty listing is the only reliable absence signal. Cost: a
         * genuinely empty directory reports ESP_ERR_NOT_FOUND too. */
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
