#include <string.h>

#include "xiaomiao_app.h"

static const xiaomiao_app_t *s_apps[XIAOMIAO_APP_REGISTRY_CAPACITY];
static size_t s_app_count;

esp_err_t xiaomiao_app_registry_reset(void)
{
    memset(s_apps, 0, sizeof(s_apps));
    s_app_count = 0;
    return ESP_OK;
}

esp_err_t xiaomiao_app_registry_register(const xiaomiao_app_t *app)
{
    if (app == NULL || app->id == NULL || app->id[0] == '\0' ||
        app->name == NULL || app->name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < s_app_count; ++i) {
        if (strcmp(s_apps[i]->id, app->id) == 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (s_app_count >= XIAOMIAO_APP_REGISTRY_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }

    s_apps[s_app_count] = app;
    s_app_count++;
    return ESP_OK;
}

size_t xiaomiao_app_registry_count(void)
{
    return s_app_count;
}

const xiaomiao_app_t *xiaomiao_app_registry_get_at(size_t index)
{
    if (index >= s_app_count) {
        return NULL;
    }
    return s_apps[index];
}

const xiaomiao_app_t *xiaomiao_app_registry_find(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < s_app_count; ++i) {
        if (strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return NULL;
}