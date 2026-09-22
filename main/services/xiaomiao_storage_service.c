/*
 * Storage Service implementation (goal node 14).
 *
 * The mount/unmount flows are a direct migration of the manually split
 * SDSPI + FATFS sequence proven on hardware by the GPIO22 fix
 * (goals/20260921-2101, verified 2026-09-21 21:56): init the host,
 * create the SDSPI device, init the card, then mount FATFS - and on
 * every failure or unmount path reset the CS GPIO BEFORE the driver
 * releases it, because the IDF 6.1 sdspi deinit path configures CS
 * back to input without revoking its GPIO ownership.
 *
 * Threading rules (goal node 14, decision 5): the Service lock guards
 * the state fields and serializes mount/unmount. Card I/O, FATFS and
 * sdspi calls run inside the lock because they are one serialized
 * lifecycle, not per-call contention; get_snapshot() only copies the
 * committed fields. No worker task, queue or timer exists.
 *
 * Failure discipline (goal node 14, decision 9): the card is never
 * formatted, written or repaired; format_if_mount_failed stays false
 * and errors keep their original esp_err_t for the UI and the log.
 */

#include "xiaomiao_storage_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include "vfs_fat_internal.h"

static const char TAG[] = "storage_svc";

/*
 * Values carried over from the working board code this Service
 * replaces: the SDSPI device waits up to 20 ms for MISO, FATFS keeps
 * at most 3 open files, and the SPI clock is validated against the
 * SD spec range (400 kHz minimum, 20 MHz SDSPI maximum).
 */
#define STORAGE_MISO_WAIT_MS 20
#define STORAGE_MAX_FILES 3
#define STORAGE_MIN_FREQ_KHZ 400
#define STORAGE_MAX_FREQ_KHZ 20000

static SemaphoreHandle_t s_lock;

static bool s_init_done;
static esp_err_t s_init_result = ESP_OK;

/* Set once by init() before any mount can run, read-only afterwards. */
static xiaomiao_storage_config_t s_config;

/* Protected by s_lock. */
static xiaomiao_storage_state_t s_state = XIAOMIAO_STORAGE_UNINITIALIZED;
static sdmmc_card_t *s_card;
static char s_card_name[XIAOMIAO_STORAGE_CARD_NAME_MAX];
static uint64_t s_capacity_bytes;
static esp_err_t s_last_error = ESP_ERR_NOT_FOUND;

/*
 * IDF 6.1 sdspi deinit_slot() configures the CS pin as input but does
 * not revoke its GPIO ownership. Reset the pin before every host
 * cleanup or retry (goal node 14, decision 8; GPIO22 fix goal).
 */
static void storage_revoke_cs_ownership(void)
{
    (void)gpio_reset_pin((gpio_num_t)s_config.cs_gpio);
}

/*
 * Release one failed attempt: reset CS first, then remove the SDSPI
 * device (its deinit path touches CS GPIO again), then free the card.
 * Only the resources acquired so far are released; sd_handle is -1
 * when the device was never created.
 */
static void storage_release_attempt(sdspi_dev_handle_t sd_handle, sdmmc_card_t *card)
{
    storage_revoke_cs_ownership();
    if (sd_handle >= 0) {
        (void)sdspi_host_remove_device(sd_handle);
    }
    free(card);
}

esp_err_t xiaomiao_storage_mount(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Idempotent: a mounted slot never gets a second device. */
    if (s_state == XIAOMIAO_STORAGE_MOUNTED) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    /* Clear ownership left by a previous failed attempt before
     * configuring the next SDSPI device (GPIO22 fix goal). */
    storage_revoke_cs_ownership();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = s_config.host_id;
    host.max_freq_khz = s_config.max_freq_khz;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = s_config.host_id;
    slot_config.gpio_cs = (gpio_num_t)s_config.cs_gpio;
    slot_config.wait_for_miso = STORAGE_MISO_WAIT_MS;

    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = STORAGE_MAX_FILES;

    sdmmc_card_t *card = malloc(sizeof(*card));
    sdspi_dev_handle_t sd_handle = -1;
    err = card ? ESP_OK : ESP_ERR_NO_MEM;

    if (err == ESP_OK) {
        err = (*host.init)();
    }
    if (err == ESP_OK) {
        err = sdspi_host_init_device(&slot_config, &sd_handle);
    }
    if (err == ESP_OK) {
        sdmmc_host_t card_host = host;
        card_host.slot = sd_handle;
        err = sdmmc_card_init(&card_host, card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sdmmc_card_init failed (0x%x)", err);
        }
    }
    if (err == ESP_OK) {
        err = esp_vfs_fat_mount_initialized(card, XIAOMIAO_STORAGE_MOUNT_POINT, &mount_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_vfs_fat_mount_initialized failed (0x%x)", err);
        }
    }

    if (err != ESP_OK) {
        if (card) {
            storage_release_attempt(sd_handle, card);
        }
        s_state = XIAOMIAO_STORAGE_ERROR;
        s_card = NULL;
        s_capacity_bytes = 0;
        s_card_name[0] = '\0';
        s_last_error = err;
        xSemaphoreGive(s_lock);
        return err;
    }

    /*
     * Commit the MOUNTED snapshot in one piece: name, capacity and
     * ESP_OK only after FATFS accepted the card (goal node 14,
     * "public interface contract").
     */
    s_card = card;
    s_state = XIAOMIAO_STORAGE_MOUNTED;
    s_last_error = ESP_OK;
    memset(s_card_name, 0, sizeof(s_card_name));
    memcpy(s_card_name, card->cid.name, sizeof(card->cid.name));
    s_card_name[sizeof(s_card_name) - 1] = '\0';
    s_capacity_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t xiaomiao_storage_unmount(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Nothing to unmount: deterministic error, snapshot untouched. */
    if (s_state != XIAOMIAO_STORAGE_MOUNTED || s_card == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * Reset CS before the driver releases it: esp_vfs_fat_sdcard_unmount
     * runs the sdspi deinit path, which would otherwise leave the GPIO
     * ownership behind and warn on the next mount (GPIO22 fix goal).
     */
    storage_revoke_cs_ownership();
    err = esp_vfs_fat_sdcard_unmount(XIAOMIAO_STORAGE_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        /* Still mounted: keep the card information, never lie. */
        s_last_error = err;
        xSemaphoreGive(s_lock);
        return err;
    }

    s_card = NULL;
    s_state = XIAOMIAO_STORAGE_UNMOUNTED;
    s_capacity_bytes = 0;
    s_card_name[0] = '\0';
    s_last_error = ESP_ERR_NOT_FOUND;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void xiaomiao_storage_get_snapshot(xiaomiao_storage_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->state = XIAOMIAO_STORAGE_UNINITIALIZED;
    snapshot->last_error = ESP_ERR_NOT_FOUND;

    if (s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    snapshot->state = s_state;
    snapshot->mounted = (s_state == XIAOMIAO_STORAGE_MOUNTED);
    memcpy(snapshot->card_name, s_card_name, sizeof(snapshot->card_name));
    snapshot->capacity_bytes = s_capacity_bytes;
    snapshot->last_error = s_last_error;

    xSemaphoreGive(s_lock);
}

esp_err_t xiaomiao_storage_service_init(const xiaomiao_storage_config_t *config)
{
    /* Idempotent: only the first validated call brings the Service up. */
    if (s_init_done) {
        return s_init_result;
    }

    if (config == NULL ||
        config->host_id < 0 ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->cs_gpio) ||
        config->max_freq_khz < STORAGE_MIN_FREQ_KHZ ||
        config->max_freq_khz > STORAGE_MAX_FREQ_KHZ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_config = *config;
    s_init_done = true;

    /* First mount attempt runs synchronously; a failure keeps the
     * Service initialized and retryable (goal node 14, decision 4). */
    s_init_result = xiaomiao_storage_mount();
    if (s_init_result != ESP_OK) {
        ESP_LOGW(TAG, "first mount failed (0x%x), slot stays retryable",
                 (unsigned)s_init_result);
    }

    return s_init_result;
}