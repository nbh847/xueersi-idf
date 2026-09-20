#include "xiaomiao_navigation.h"

/*
 * App content root owned by Navigation; NULL means the root page. The
 * current App state itself is kept by the App Manager, never duplicated
 * here (goal decision 2).
 */
static lv_obj_t *s_app_root;

esp_err_t xiaomiao_navigation_open(const char *app_id)
{
    if (app_id == NULL || app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_app_root != NULL) {
        /* Invariant: content root non-NULL implies a current App, so
         * switching requires xiaomiao_navigation_back() first. */
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t *root = lv_obj_create(lv_screen_active());
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Publish the root before the Manager runs the App open callback:
     * the callback reads it via xiaomiao_navigation_app_root(). On
     * failure below, restore NULL and delete the temporary root so no
     * object is left behind. */
    s_app_root = root;

    esp_err_t err = xiaomiao_app_manager_open(app_id);
    if (err != ESP_OK) {
        s_app_root = NULL;
        lv_obj_delete(root);
        return err;
    }

    return ESP_OK;
}

esp_err_t xiaomiao_navigation_back(void)
{
    if (xiaomiao_app_manager_current() == NULL) {
        return ESP_OK;
    }

    esp_err_t err = xiaomiao_app_manager_close();
    if (err != ESP_OK) {
        return err;
    }

    /* Deleting the root releases every child the App created under it. */
    lv_obj_delete(s_app_root);
    s_app_root = NULL;
    return ESP_OK;
}

const xiaomiao_app_t *xiaomiao_navigation_current(void)
{
    return xiaomiao_app_manager_current();
}

lv_obj_t *xiaomiao_navigation_app_root(void)
{
    return s_app_root;
}