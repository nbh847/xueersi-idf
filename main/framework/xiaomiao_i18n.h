/*
 * UI text layer (goal node 15C).
 *
 * Every user-visible string in the Launcher, the business Apps and the
 * Hardware Test dashboard is addressed by a stable ID instead of a
 * literal, so the whole UI switches between the fixed zh-CN table and
 * the existing English table in one place.
 *
 * Language selection (goal node 15, decision 8): Chinese is active only
 * while the Font Service reports a validated Chinese glyph pack. The
 * choice is evaluated once and cached, because a label that was already
 * drawn must never change language under the user.
 *
 * Technical abbreviations (CPU, RAM, GPU, GPIO, SSID, IP, ADC, PWM,
 * I2C, SD, LED, Wi-Fi, MB, MiB, Hz, RAW, ON/OFF register values) stay
 * English inside the mixed strings; only the surrounding words are
 * translated. Serial logs and self test output stay English everywhere.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* App names shown by the Launcher grid. */
    XM_TEXT_APP_GAMES = 0,
    XM_TEXT_APP_PC_MONITOR,
    XM_TEXT_APP_TOOLS,
    XM_TEXT_APP_SETTINGS,
    XM_TEXT_APP_HARDWARE_TEST,

    /* Launcher chrome. The "Xiaomiao" brand title is not translatable. */
    XM_TEXT_LAUNCHER_HINT,
    XM_TEXT_LAUNCHER_EMPTY,

    /* Shared menu entries and footer key hints. */
    XM_TEXT_MENU_WIFI,
    XM_TEXT_HINT_B_BACK,
    XM_TEXT_HINT_A_OPEN_B_BACK,
    XM_TEXT_HINT_A_SELECT_B_BACK,
    XM_TEXT_HINT_A_YES_B_NO,
    XM_TEXT_HINT_B_CANCEL,

    /* Games placeholder page. */
    XM_TEXT_GAMES_COMING_SOON,

    /* PC Monitor pages. */
    XM_TEXT_PM_TITLE_CPU_RAM,
    XM_TEXT_PM_TITLE_GPU_TEMP,
    XM_TEXT_PM_LABEL_TEMP,
    XM_TEXT_PM_LABEL_GPU_TEMP,
    XM_TEXT_PM_LABEL_CPU_TEMP,
    XM_TEXT_PM_HINT,

    /* Service state words used by the Wi-Fi, Tools and Settings pages. */
    XM_TEXT_STATE_CONNECTED,
    XM_TEXT_STATE_CONNECTING,
    XM_TEXT_STATE_RECONNECTING,
    XM_TEXT_STATE_SCANNING,
    XM_TEXT_STATE_SETUP,
    XM_TEXT_STATE_DISABLED,
    XM_TEXT_STATE_ON,
    XM_TEXT_STATE_OFF,
    XM_TEXT_STATE_NOT_CONFIGURED,
    XM_TEXT_STATE_AUTH_FAILED,
    XM_TEXT_STATE_ERROR,
    XM_TEXT_STATE_NOT_CONNECTED,
    XM_TEXT_STATE_UNAVAILABLE,
    XM_TEXT_STATE_READY,
    XM_TEXT_STATE_DEGRADED,
    XM_TEXT_STATE_UNINITIALIZED,
    XM_TEXT_STATE_UNKNOWN,
    XM_TEXT_STATE_NONE,
    XM_TEXT_STATE_MOUNTED,
    XM_TEXT_STATE_UNMOUNTED,

    /* Wi-Fi signal quality names. */
    XM_TEXT_LEVEL_STRONG,
    XM_TEXT_LEVEL_GOOD,
    XM_TEXT_LEVEL_FAIR,
    XM_TEXT_LEVEL_WEAK,

    /* Row labels of the two-column detail pages. */
    XM_TEXT_LABEL_STATUS,
    XM_TEXT_LABEL_SSID,
    XM_TEXT_LABEL_SIGNAL,
    XM_TEXT_LABEL_IP,
    XM_TEXT_LABEL_CHIP,
    XM_TEXT_LABEL_CPU,
    XM_TEXT_LABEL_FLASH,
    XM_TEXT_LABEL_PSRAM,
    XM_TEXT_LABEL_IDF,
    XM_TEXT_LABEL_FW,
    XM_TEXT_LABEL_AUTO_CONNECT,
    XM_TEXT_LABEL_CONFIGURE,
    XM_TEXT_LABEL_FORGET,
    XM_TEXT_LABEL_FORGET_CONFIRM,
    XM_TEXT_LABEL_PASSWORD,
    XM_TEXT_LABEL_OPEN,

    /* Tools detail pages. */
    XM_TEXT_TOOLS_SYSTEM_INFO,
    XM_TEXT_TOOLS_ABOUT,
    XM_TEXT_TOOLS_ASSETS,
    XM_TEXT_LABEL_PROJECT,
    XM_TEXT_LABEL_FIRMWARE,
    XM_TEXT_LABEL_AUTHOR,
    XM_TEXT_LABEL_REPO,

    /* Tools -> Assets diagnostics rows. */
    XM_TEXT_ASSETS_PARTITION,
    XM_TEXT_ASSETS_TOTAL,
    XM_TEXT_ASSETS_USED,
    XM_TEXT_ASSETS_FONT,
    XM_TEXT_ASSETS_GLYPHS,
    XM_TEXT_ASSETS_SD,

    /* Settings pages and their status rows. */
    XM_TEXT_SETTINGS_DISPLAY,
    XM_TEXT_SETTINGS_SOUND,
    XM_TEXT_SETTINGS_SYSTEM,
    XM_TEXT_SETTINGS_SERVICE,
    XM_TEXT_SETTINGS_AUDIO_SERVICE,
    XM_TEXT_SETTINGS_BRIGHTNESS_FIXED,
    XM_TEXT_SETTINGS_BACKLIGHT_VCC,
    XM_TEXT_SETTINGS_NO_DISPLAY,
    XM_TEXT_SETTINGS_NODE12,
    XM_TEXT_SETTINGS_FROM_NVS,
    XM_TEXT_SETTINGS_RECOVERED,
    XM_TEXT_SETTINGS_NOT_PERSISTED,
    XM_TEXT_SETTINGS_DEFAULTS,
    XM_TEXT_SETTINGS_SETTINGS_UNAVAILABLE,
    XM_TEXT_SETTINGS_NVS_ERROR,
    XM_TEXT_SETTINGS_STORED_FIELDS,

    /* Settings -> Wi-Fi provisioning page. */
    XM_TEXT_SETTINGS_PROV_TITLE,
    XM_TEXT_SETTINGS_PROV_WAITING,
    XM_TEXT_SETTINGS_PROV_CONNECTING,
    XM_TEXT_SETTINGS_PROV_CONNECTED_NOT_SAVED,
    XM_TEXT_SETTINGS_PROV_WRONG_PASSWORD,
    XM_TEXT_SETTINGS_PROV_NOT_FOUND,
    XM_TEXT_SETTINGS_PROV_TIMEOUT,
    XM_TEXT_SETTINGS_PROV_FAILED,
    XM_TEXT_SETTINGS_PROV_WAIT_PHONE,

    /* Settings one-line result messages. */
    XM_TEXT_SETTINGS_MSG_SAVE_FAILED,
    XM_TEXT_SETTINGS_MSG_APPLY_FAILED,
    XM_TEXT_SETTINGS_MSG_SETUP_FAILED,
    XM_TEXT_SETTINGS_MSG_FORGOTTEN,
    XM_TEXT_SETTINGS_MSG_FORGET_FAILED,
    XM_TEXT_SETTINGS_MSG_PRESS_CONFIRM,
    XM_TEXT_SETTINGS_MSG_CANCELLED,
    XM_TEXT_SETTINGS_MSG_SETUP_COMPLETE,
    XM_TEXT_SETTINGS_MSG_SETUP_CLOSED,
    XM_TEXT_SETTINGS_MSG_SETUP_CANCELLED,
    XM_TEXT_SETTINGS_MSG_SETUP_STOP_FAILED,

    /* Hardware Test dashboard page titles. */
    XM_TEXT_HT_PAGE_LIGHT,
    XM_TEXT_HT_PAGE_THERM,
    XM_TEXT_HT_PAGE_MOTION,
    XM_TEXT_HT_PAGE_LED1,
    XM_TEXT_HT_PAGE_LED2,
    XM_TEXT_HT_PAGE_BUZZER,
    XM_TEXT_HT_PAGE_MOTOR1,
    XM_TEXT_HT_PAGE_MOTOR2,
    XM_TEXT_HT_PAGE_SD,
    XM_TEXT_HT_PAGE_GPIO25,
    XM_TEXT_HT_PAGE_GPIO26,
    XM_TEXT_HT_PAGE_GPIO32,
    XM_TEXT_HT_PAGE_GPIO33,
    XM_TEXT_HT_PAGE_SYSTEM,
    XM_TEXT_HT_PAGE_ABOUT,

    /* Hardware Test key hints. */
    XM_TEXT_HT_HINT_PAGE,
    XM_TEXT_HT_HINT_SAMPLE,
    XM_TEXT_HT_HINT_RESCAN,
    XM_TEXT_HT_HINT_TOGGLE_B_OFF,
    XM_TEXT_HT_HINT_BUZZER,
    XM_TEXT_HT_HINT_MOTOR_RUN,
    XM_TEXT_HT_HINT_MOTOR_IDLE,
    XM_TEXT_HT_HINT_DUTY,
    XM_TEXT_HT_HINT_SD_UNMOUNT,
    XM_TEXT_HT_HINT_HOLD_TO_EXIT,

    /* Hardware Test value and state words. */
    XM_TEXT_HT_ABSENT,
    XM_TEXT_HT_MOUNTED,
    XM_TEXT_HT_NO_CARD,
    XM_TEXT_HT_OK,
    XM_TEXT_HT_FAIL,
    XM_TEXT_HT_ADC_FAIL,
    XM_TEXT_HT_PWM_INIT_FAIL,

    /* MPU6050 gesture names (render-time mapping of the stored token). */
    XM_TEXT_HT_GESTURE_ABSENT,
    XM_TEXT_HT_GESTURE_READY,
    XM_TEXT_HT_GESTURE_TILT_UP,
    XM_TEXT_HT_GESTURE_TILT_DN,
    XM_TEXT_HT_GESTURE_TILT_R,
    XM_TEXT_HT_GESTURE_TILT_L,
    XM_TEXT_HT_GESTURE_LEVEL,

    /* Short esp_err_t names shown on the sensor sub-lines. */
    XM_TEXT_ERR_TIMEOUT,
    XM_TEXT_ERR_NOT_FOUND,
    XM_TEXT_ERR_STATE,
    XM_TEXT_ERR_ARG,
    XM_TEXT_ERR_FAIL,
    XM_TEXT_ERR_GENERIC,

    /* Hardware Test action results (the transient hint line). */
    XM_TEXT_HT_ACT_MOTOR1_STOPPED,
    XM_TEXT_HT_ACT_MOTOR2_STOPPED,
    XM_TEXT_HT_ACT_MOTOR1_OUTPUT,
    XM_TEXT_HT_ACT_MOTOR2_OUTPUT,
    XM_TEXT_HT_ACT_MOTOR1_DIR,
    XM_TEXT_HT_ACT_MOTOR2_DIR,
    XM_TEXT_HT_ACT_MOTOR_CMD_FAIL,
    XM_TEXT_HT_ACT_PWM_ZERO,
    XM_TEXT_HT_ACT_PWM_CMD_FAIL,
    XM_TEXT_HT_ACT_PWM_INIT_FAIL,
    XM_TEXT_HT_ACT_GPIO25_OFF,
    XM_TEXT_HT_ACT_GPIO26_OFF,
    XM_TEXT_HT_ACT_GPIO25_PWM,
    XM_TEXT_HT_ACT_GPIO26_PWM,
    XM_TEXT_HT_ACT_DUTY_ZERO,
    XM_TEXT_HT_ACT_DUTY_SET,
    XM_TEXT_HT_ACT_SAMPLED,
    XM_TEXT_HT_ACT_ADC_READ_FAIL,
    XM_TEXT_HT_ACT_MPU_READY,
    XM_TEXT_HT_ACT_MPU_ABSENT,
    XM_TEXT_HT_ACT_LED1_TOGGLED,
    XM_TEXT_HT_ACT_LED2_TOGGLED,
    XM_TEXT_HT_ACT_LED1_OFF,
    XM_TEXT_HT_ACT_LED2_OFF,
    XM_TEXT_HT_ACT_LED_CMD_FAIL,
    XM_TEXT_HT_ACT_BEEP,
    XM_TEXT_HT_ACT_BUZZER_INIT_FAIL,
    XM_TEXT_HT_ACT_BUZZER_STOP,
    XM_TEXT_HT_ACT_SD_MOUNTED,
    XM_TEXT_HT_ACT_SD_UNMOUNTED,
    XM_TEXT_HT_ACT_SD_UNMOUNT_FAIL,
    XM_TEXT_HT_ACT_NO_SD_CARD,
    XM_TEXT_HT_ACT_RESCANNED,
    XM_TEXT_HT_ACT_I2C_INIT_FAIL,
    XM_TEXT_HT_ACT_PITCH_SET,
    XM_TEXT_HT_ACT_POWER_SET,
    XM_TEXT_HT_ACT_CANCELED,

    /* Hardware Test About page section headers. */
    XM_TEXT_HT_ABOUT_MODEL,
    XM_TEXT_HT_ABOUT_AUTHOR,
    XM_TEXT_HT_ABOUT_CHIP_REV,
    XM_TEXT_HT_ABOUT_SYSTEM,
    XM_TEXT_HT_ABOUT_TARGET,
    XM_TEXT_HT_ABOUT_BUILD,
    XM_TEXT_HT_ABOUT_CLOCKS,
    XM_TEXT_HT_ABOUT_STORAGE,
    XM_TEXT_HT_ABOUT_DISPLAY,
    XM_TEXT_HT_ABOUT_BOARD_IO,
    XM_TEXT_HT_ABOUT_KEYS,

    XM_TEXT_COUNT
} xiaomiao_text_id_t;

/*
 * Text for one ID in the active language. Never returns NULL and never
 * a partial phrase: an unknown ID yields the "???" marker and an empty
 * zh-CN entry falls back to the English one.
 */
const char *xiaomiao_text(xiaomiao_text_id_t id);

/* True once the language has been latched to zh-CN. */
bool xiaomiao_i18n_is_chinese(void);

#ifdef __cplusplus
}
#endif
