/*
 * Built-in Launcher icon loader (goal node 15, 15D closure item).
 * See xiaomiao_icons.h for the format contract and failure semantics.
 */

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "framework/xiaomiao_icons.h"
#include "services/xiaomiao_assets_service.h"

static const char TAG[] = "xiaomiao_icons";

#define ICON_DATA_BYTES (XIAOMIAO_ICON_SIZE_PX * XIAOMIAO_ICON_SIZE_PX * 2u)
#define ICON_CACHE_SLOTS 8

typedef struct {
    char app_id[16];
    bool missing; /* negative cache: never re-read a known-absent icon */
    lv_image_dsc_t dsc;
    uint8_t *pixels; /* PSRAM, ICON_DATA_BYTES */
} icon_slot_t;

static icon_slot_t s_slots[ICON_CACHE_SLOTS];
static size_t s_used;

static icon_slot_t *slot_find_or_alloc(const char *app_id)
{
    for (size_t i = 0; i < s_used; i++) {
        if (strcmp(s_slots[i].app_id, app_id) == 0) {
            return &s_slots[i];
        }
    }
    if (s_used >= ICON_CACHE_SLOTS || strlen(app_id) >= sizeof(s_slots[0].app_id)) {
        return NULL;
    }
    icon_slot_t *slot = &s_slots[s_used++];
    memset(slot, 0, sizeof(*slot));
    strlcpy(slot->app_id, app_id, sizeof(slot->app_id));
    return slot;
}

static bool icon_read(const char *app_id, uint8_t **out_pixels)
{
    char rel[40];
    if (snprintf(rel, sizeof(rel), "icons/%s.bin", app_id) >= (int)sizeof(rel)) {
        return false;
    }
    xiaomiao_asset_file_t *file = NULL;
    if (xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, rel, &file) != ESP_OK) {
        return false;
    }
    uint32_t size = 0;
    bool ok = (xiaomiao_asset_get_size(file, &size) == ESP_OK) &&
              (size == ICON_DATA_BYTES);
    if (ok) {
        uint8_t *buf = heap_caps_malloc(ICON_DATA_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf == NULL) {
            ok = false;
        } else {
            size_t got = 0;
            ok = (xiaomiao_asset_read(file, buf, ICON_DATA_BYTES, &got) == ESP_OK) &&
                 (got == ICON_DATA_BYTES);
            if (ok) {
                *out_pixels = buf;
            } else {
                free(buf);
            }
        }
    }
    xiaomiao_asset_close(file);
    if (!ok) {
        ESP_LOGW(TAG, "icon '%s' rejected (size/format/read)", app_id);
    }
    return ok;
}

bool xiaomiao_icons_get(const char *app_id, lv_image_dsc_t *out_dsc)
{
    if (app_id == NULL || app_id[0] == '\0' || out_dsc == NULL) {
        return false;
    }
    icon_slot_t *slot = slot_find_or_alloc(app_id);
    if (slot == NULL) {
        return false;
    }
    if (slot->missing) {
        return false;
    }
    if (slot->pixels == NULL) {
        if (!icon_read(app_id, &slot->pixels)) {
            slot->missing = true;
            return false;
        }
        memset(&slot->dsc, 0, sizeof(slot->dsc));
        slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        slot->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        slot->dsc.header.w = XIAOMIAO_ICON_SIZE_PX;
        slot->dsc.header.h = XIAOMIAO_ICON_SIZE_PX;
        slot->dsc.header.stride = XIAOMIAO_ICON_SIZE_PX * 2;
        slot->dsc.data_size = ICON_DATA_BYTES;
        slot->dsc.data = slot->pixels;
    }
    *out_dsc = slot->dsc;
    return true;
}
