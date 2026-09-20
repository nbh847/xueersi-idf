#include <string.h>

#include "xiaomiao_app.h"

static bool s_initialized;
static const xiaomiao_app_t *s_current;

esp_err_t xiaomiao_app_manager_init_all(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < xiaomiao_app_registry_count(); ++i) {
        const xiaomiao_app_t *app = xiaomiao_app_registry_get_at(i);
        if (app->init != NULL) {
            app->init();
        }
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t xiaomiao_app_manager_open(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const xiaomiao_app_t *app = xiaomiao_app_registry_find(id);
    if (app == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_current != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (app->open != NULL) {
        app->open();
    }
    s_current = app;
    return ESP_OK;
}

esp_err_t xiaomiao_app_manager_close(void)
{
    if (s_current == NULL) {
        return ESP_OK;
    }

    if (s_current->close != NULL) {
        s_current->close();
    }
    s_current = NULL;
    return ESP_OK;
}

const xiaomiao_app_t *xiaomiao_app_manager_current(void)
{
    return s_current;
}