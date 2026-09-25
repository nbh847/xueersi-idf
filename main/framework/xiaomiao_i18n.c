/*
 * UI text tables (goal node 15C).
 *
 * Two fixed tables indexed by xiaomiao_text_id_t: the English one keeps
 * the exact literals the UI used before localisation, the zh-CN one
 * holds the Simplified Chinese strings. Only these two string literals
 * may carry non-ASCII bytes in the whole UI layer, so the font coverage
 * scan has a single file to read.
 *
 * Language selection is latched on the first call, which happens while
 * the first UI object is built. The boot chain initializes the Font
 * Service before the LVGL task registers and creates any App, so the
 * latch always sees the final font state. A caller that asks before the
 * Font Service has run latches English, which is the safe half of the
 * degradation rule: every English string has Montserrat glyphs.
 */

#include "xiaomiao_i18n.h"

#include <stddef.h>

#include "services/xiaomiao_font_service.h"

/* Shown when an out-of-range ID reaches the UI layer. */
static const char TEXT_UNKNOWN[] = "???";

static const char *const s_text_en[XM_TEXT_COUNT] = {
    [XM_TEXT_APP_GAMES] = "Games",
    [XM_TEXT_APP_PC_MONITOR] = "PC Monitor",
    [XM_TEXT_APP_TOOLS] = "Tools",
    [XM_TEXT_APP_SETTINGS] = "Settings",
    [XM_TEXT_APP_HARDWARE_TEST] = "Hardware Test",

    [XM_TEXT_LAUNCHER_HINT] = "A open",
    [XM_TEXT_LAUNCHER_EMPTY] = "no apps registered",

    [XM_TEXT_MENU_WIFI] = "Wi-Fi",
    [XM_TEXT_HINT_B_BACK] = "B Back",
    [XM_TEXT_HINT_A_OPEN_B_BACK] = "A Open  B Back",
    [XM_TEXT_HINT_A_SELECT_B_BACK] = "A Select  B Back",
    [XM_TEXT_HINT_A_YES_B_NO] = "A Yes  B No",
    [XM_TEXT_HINT_B_CANCEL] = "B Cancel",

    [XM_TEXT_GAMES_COMING_SOON] = "Coming soon",

    [XM_TEXT_PM_TITLE_CPU_RAM] = "CPU / RAM",
    [XM_TEXT_PM_TITLE_GPU_TEMP] = "GPU / Temp",
    [XM_TEXT_PM_LABEL_TEMP] = "TEMP",
    [XM_TEXT_PM_LABEL_GPU_TEMP] = "GPU T",
    [XM_TEXT_PM_LABEL_CPU_TEMP] = "CPU T",
    [XM_TEXT_PM_HINT] = "B Back   < > Switch",

    [XM_TEXT_STATE_CONNECTED] = "Connected",
    [XM_TEXT_STATE_CONNECTING] = "Connecting",
    [XM_TEXT_STATE_RECONNECTING] = "Reconnecting",
    [XM_TEXT_STATE_SCANNING] = "Scanning",
    [XM_TEXT_STATE_SETUP] = "Setup",
    [XM_TEXT_STATE_DISABLED] = "Off",
    [XM_TEXT_STATE_ON] = "On",
    [XM_TEXT_STATE_OFF] = "Off",
    [XM_TEXT_STATE_NOT_CONFIGURED] = "Not configured",
    [XM_TEXT_STATE_AUTH_FAILED] = "Auth failed",
    [XM_TEXT_STATE_ERROR] = "Error",
    [XM_TEXT_STATE_NOT_CONNECTED] = "Not connected",
    [XM_TEXT_STATE_UNAVAILABLE] = "Unavailable",
    [XM_TEXT_STATE_READY] = "Ready",
    [XM_TEXT_STATE_DEGRADED] = "Degraded",
    [XM_TEXT_STATE_UNINITIALIZED] = "Not initialized",
    [XM_TEXT_STATE_UNKNOWN] = "Unknown",
    [XM_TEXT_STATE_NONE] = "None",
    [XM_TEXT_STATE_MOUNTED] = "Mounted",
    [XM_TEXT_STATE_UNMOUNTED] = "Unmounted",

    [XM_TEXT_LEVEL_STRONG] = "Strong",
    [XM_TEXT_LEVEL_GOOD] = "Good",
    [XM_TEXT_LEVEL_FAIR] = "Fair",
    [XM_TEXT_LEVEL_WEAK] = "Weak",

    [XM_TEXT_LABEL_STATUS] = "Status",
    [XM_TEXT_LABEL_SSID] = "SSID",
    [XM_TEXT_LABEL_SIGNAL] = "Signal",
    [XM_TEXT_LABEL_IP] = "IP",
    [XM_TEXT_LABEL_CHIP] = "Chip",
    [XM_TEXT_LABEL_CPU] = "CPU",
    [XM_TEXT_LABEL_FLASH] = "Flash",
    [XM_TEXT_LABEL_PSRAM] = "PSRAM",
    [XM_TEXT_LABEL_IDF] = "IDF",
    [XM_TEXT_LABEL_FW] = "FW",
    [XM_TEXT_LABEL_AUTO_CONNECT] = "Auto connect",
    [XM_TEXT_LABEL_CONFIGURE] = "Configure",
    [XM_TEXT_LABEL_FORGET] = "Forget network",
    [XM_TEXT_LABEL_FORGET_CONFIRM] = "Forget network?",
    [XM_TEXT_LABEL_PASSWORD] = "Pass",
    [XM_TEXT_LABEL_OPEN] = "Open",

    [XM_TEXT_TOOLS_SYSTEM_INFO] = "System Info",
    [XM_TEXT_TOOLS_ABOUT] = "About",
    [XM_TEXT_TOOLS_ASSETS] = "Assets",
    [XM_TEXT_LABEL_PROJECT] = "Project",
    [XM_TEXT_LABEL_FIRMWARE] = "Firmware",
    [XM_TEXT_LABEL_AUTHOR] = "Author",
    [XM_TEXT_LABEL_REPO] = "Repo",

    [XM_TEXT_ASSETS_PARTITION] = "Assets",
    [XM_TEXT_ASSETS_TOTAL] = "Total",
    [XM_TEXT_ASSETS_USED] = "Used",
    [XM_TEXT_ASSETS_FONT] = "Font",
    [XM_TEXT_ASSETS_GLYPHS] = "Glyphs",
    [XM_TEXT_ASSETS_SD] = "SD card",

    [XM_TEXT_SETTINGS_DISPLAY] = "Display",
    [XM_TEXT_SETTINGS_SOUND] = "Sound",
    [XM_TEXT_SETTINGS_SYSTEM] = "System",
    [XM_TEXT_SETTINGS_SERVICE] = "Settings Service",
    [XM_TEXT_SETTINGS_AUDIO_SERVICE] = "Audio Service",
    [XM_TEXT_SETTINGS_BRIGHTNESS_FIXED] = "Brightness fixed",
    [XM_TEXT_SETTINGS_BACKLIGHT_VCC] = "Backlight tied to VCC",
    [XM_TEXT_SETTINGS_NO_DISPLAY] = "No display settings",
    [XM_TEXT_SETTINGS_NODE12] = "Implemented in node 12",
    [XM_TEXT_SETTINGS_FROM_NVS] = "Loaded from NVS",
    [XM_TEXT_SETTINGS_RECOVERED] = "Defaults restored",
    [XM_TEXT_SETTINGS_NOT_PERSISTED] = "Not persisted",
    [XM_TEXT_SETTINGS_DEFAULTS] = "Defaults applied",
    [XM_TEXT_SETTINGS_SETTINGS_UNAVAILABLE] = "Settings unavailable",
    [XM_TEXT_SETTINGS_NVS_ERROR] = "NVS error",
    [XM_TEXT_SETTINGS_STORED_FIELDS] = "2 stored fields",

    [XM_TEXT_SETTINGS_PROV_TITLE] = "Wi-Fi Setup",
    [XM_TEXT_SETTINGS_PROV_WAITING] = "Waiting...",
    [XM_TEXT_SETTINGS_PROV_CONNECTING] = "Connecting...",
    [XM_TEXT_SETTINGS_PROV_CONNECTED_NOT_SAVED] = "Connected, not saved",
    [XM_TEXT_SETTINGS_PROV_WRONG_PASSWORD] = "Wrong password",
    [XM_TEXT_SETTINGS_PROV_NOT_FOUND] = "Network not found",
    [XM_TEXT_SETTINGS_PROV_TIMEOUT] = "Timed out",
    [XM_TEXT_SETTINGS_PROV_FAILED] = "Connection failed",
    [XM_TEXT_SETTINGS_PROV_WAIT_PHONE] = "Waiting for phone",

    [XM_TEXT_SETTINGS_MSG_SAVE_FAILED] = "Save failed",
    [XM_TEXT_SETTINGS_MSG_APPLY_FAILED] = "Apply failed",
    [XM_TEXT_SETTINGS_MSG_SETUP_FAILED] = "Setup failed",
    [XM_TEXT_SETTINGS_MSG_FORGOTTEN] = "Network forgotten",
    [XM_TEXT_SETTINGS_MSG_FORGET_FAILED] = "Forget failed",
    [XM_TEXT_SETTINGS_MSG_PRESS_CONFIRM] = "Press A to confirm",
    [XM_TEXT_SETTINGS_MSG_CANCELLED] = "Cancelled",
    [XM_TEXT_SETTINGS_MSG_SETUP_COMPLETE] = "Setup complete",
    [XM_TEXT_SETTINGS_MSG_SETUP_CLOSED] = "Setup closed",
    [XM_TEXT_SETTINGS_MSG_SETUP_CANCELLED] = "Setup cancelled",
    [XM_TEXT_SETTINGS_MSG_SETUP_STOP_FAILED] = "Setup stop failed",

    [XM_TEXT_HT_PAGE_LIGHT] = "LIGHT",
    [XM_TEXT_HT_PAGE_THERM] = "THERM",
    [XM_TEXT_HT_PAGE_MOTION] = "MOTION",
    [XM_TEXT_HT_PAGE_LED1] = "LED 1",
    [XM_TEXT_HT_PAGE_LED2] = "LED 2",
    [XM_TEXT_HT_PAGE_BUZZER] = "BUZZER",
    [XM_TEXT_HT_PAGE_MOTOR1] = "MOTOR 1",
    [XM_TEXT_HT_PAGE_MOTOR2] = "MOTOR 2",
    [XM_TEXT_HT_PAGE_SD] = "SD CARD",
    [XM_TEXT_HT_PAGE_GPIO25] = "GPIO25",
    [XM_TEXT_HT_PAGE_GPIO26] = "GPIO26",
    [XM_TEXT_HT_PAGE_GPIO32] = "GPIO32",
    [XM_TEXT_HT_PAGE_GPIO33] = "GPIO33",
    [XM_TEXT_HT_PAGE_SYSTEM] = "SYSTEM",
    [XM_TEXT_HT_PAGE_ABOUT] = "ABOUT",

    [XM_TEXT_HT_HINT_PAGE] = "L/R page",
    [XM_TEXT_HT_HINT_SAMPLE] = "A sample   L/R",
    [XM_TEXT_HT_HINT_RESCAN] = "A rescan   L/R",
    [XM_TEXT_HT_HINT_TOGGLE_B_OFF] = "A toggle   B off",
    [XM_TEXT_HT_HINT_BUZZER] = "U/D Hz  A beep  B stop",
    [XM_TEXT_HT_HINT_MOTOR_RUN] = "U/D PWM  A off  B stop",
    [XM_TEXT_HT_HINT_MOTOR_IDLE] = "U/D PWM  A out  B dir",
    [XM_TEXT_HT_HINT_DUTY] = "U/D duty  A toggle  B off",
    [XM_TEXT_HT_HINT_SD_UNMOUNT] = "B unmount  L/R",
    [XM_TEXT_HT_HINT_HOLD_TO_EXIT] = "Hold to exit",

    [XM_TEXT_HT_ABSENT] = "ABSENT",
    [XM_TEXT_HT_MOUNTED] = "MOUNTED",
    [XM_TEXT_HT_NO_CARD] = "NO CARD",
    [XM_TEXT_HT_OK] = "OK",
    [XM_TEXT_HT_FAIL] = "FAIL",
    [XM_TEXT_HT_ADC_FAIL] = "ADC FAIL",
    [XM_TEXT_HT_PWM_INIT_FAIL] = "PWM INIT FAIL",

    [XM_TEXT_HT_GESTURE_ABSENT] = "ABSENT",
    [XM_TEXT_HT_GESTURE_READY] = "READY",
    [XM_TEXT_HT_GESTURE_TILT_UP] = "TILT UP",
    [XM_TEXT_HT_GESTURE_TILT_DN] = "TILT DN",
    [XM_TEXT_HT_GESTURE_TILT_R] = "TILT R",
    [XM_TEXT_HT_GESTURE_TILT_L] = "TILT L",
    [XM_TEXT_HT_GESTURE_LEVEL] = "LEVEL",

    [XM_TEXT_ERR_TIMEOUT] = "TIMEOUT",
    [XM_TEXT_ERR_NOT_FOUND] = "NOT FOUND",
    [XM_TEXT_ERR_STATE] = "STATE",
    [XM_TEXT_ERR_ARG] = "ARG",
    [XM_TEXT_ERR_FAIL] = "FAIL",
    [XM_TEXT_ERR_GENERIC] = "ERR",

    [XM_TEXT_HT_ACT_MOTOR1_STOPPED] = "Motor1 stopped",
    [XM_TEXT_HT_ACT_MOTOR2_STOPPED] = "Motor2 stopped",
    [XM_TEXT_HT_ACT_MOTOR1_OUTPUT] = "Motor1 output",
    [XM_TEXT_HT_ACT_MOTOR2_OUTPUT] = "Motor2 output",
    [XM_TEXT_HT_ACT_MOTOR1_DIR] = "Motor1 dir",
    [XM_TEXT_HT_ACT_MOTOR2_DIR] = "Motor2 dir",
    [XM_TEXT_HT_ACT_MOTOR_CMD_FAIL] = "Motor cmd fail",
    [XM_TEXT_HT_ACT_PWM_ZERO] = "PWM is zero",
    [XM_TEXT_HT_ACT_PWM_CMD_FAIL] = "PWM cmd fail",
    [XM_TEXT_HT_ACT_PWM_INIT_FAIL] = "PWM init fail",
    [XM_TEXT_HT_ACT_GPIO25_OFF] = "GPIO25 off",
    [XM_TEXT_HT_ACT_GPIO26_OFF] = "GPIO26 off",
    [XM_TEXT_HT_ACT_GPIO25_PWM] = "GPIO25 PWM",
    [XM_TEXT_HT_ACT_GPIO26_PWM] = "GPIO26 PWM",
    [XM_TEXT_HT_ACT_DUTY_ZERO] = "Duty is zero",
    [XM_TEXT_HT_ACT_DUTY_SET] = "Duty set",
    [XM_TEXT_HT_ACT_SAMPLED] = "Sampled",
    [XM_TEXT_HT_ACT_ADC_READ_FAIL] = "ADC read fail",
    [XM_TEXT_HT_ACT_MPU_READY] = "MPU ready",
    [XM_TEXT_HT_ACT_MPU_ABSENT] = "MPU absent",
    [XM_TEXT_HT_ACT_LED1_TOGGLED] = "LED1 toggled",
    [XM_TEXT_HT_ACT_LED2_TOGGLED] = "LED2 toggled",
    [XM_TEXT_HT_ACT_LED1_OFF] = "LED1 off",
    [XM_TEXT_HT_ACT_LED2_OFF] = "LED2 off",
    [XM_TEXT_HT_ACT_LED_CMD_FAIL] = "LED cmd fail",
    [XM_TEXT_HT_ACT_BEEP] = "Beep",
    [XM_TEXT_HT_ACT_BUZZER_INIT_FAIL] = "Buzzer init fail",
    [XM_TEXT_HT_ACT_BUZZER_STOP] = "Buzzer stop",
    [XM_TEXT_HT_ACT_SD_MOUNTED] = "SD mounted",
    [XM_TEXT_HT_ACT_SD_UNMOUNTED] = "SD unmounted",
    [XM_TEXT_HT_ACT_SD_UNMOUNT_FAIL] = "SD unmount fail",
    [XM_TEXT_HT_ACT_NO_SD_CARD] = "No SD card",
    [XM_TEXT_HT_ACT_RESCANNED] = "Rescanned",
    [XM_TEXT_HT_ACT_I2C_INIT_FAIL] = "I2C init fail",
    [XM_TEXT_HT_ACT_PITCH_SET] = "Pitch set",
    [XM_TEXT_HT_ACT_POWER_SET] = "Power set",
    [XM_TEXT_HT_ACT_CANCELED] = "Canceled",

    [XM_TEXT_HT_ABOUT_MODEL] = "Model",
    [XM_TEXT_HT_ABOUT_AUTHOR] = "Author",
    [XM_TEXT_HT_ABOUT_CHIP_REV] = "Chip rev",
    [XM_TEXT_HT_ABOUT_SYSTEM] = "System",
    [XM_TEXT_HT_ABOUT_TARGET] = "Target",
    [XM_TEXT_HT_ABOUT_BUILD] = "Build",
    [XM_TEXT_HT_ABOUT_CLOCKS] = "Clocks",
    [XM_TEXT_HT_ABOUT_STORAGE] = "Storage",
    [XM_TEXT_HT_ABOUT_DISPLAY] = "Display",
    [XM_TEXT_HT_ABOUT_BOARD_IO] = "Board IO",
    [XM_TEXT_HT_ABOUT_KEYS] = "Keys",
};

static const char *const s_text_zh[XM_TEXT_COUNT] = {
    [XM_TEXT_APP_GAMES] = "游戏",
    [XM_TEXT_APP_PC_MONITOR] = "电脑监控",
    [XM_TEXT_APP_TOOLS] = "工具",
    [XM_TEXT_APP_SETTINGS] = "设置",
    [XM_TEXT_APP_HARDWARE_TEST] = "硬件测试",

    [XM_TEXT_LAUNCHER_HINT] = "A 打开",
    [XM_TEXT_LAUNCHER_EMPTY] = "未注册应用",

    [XM_TEXT_MENU_WIFI] = "Wi-Fi",
    [XM_TEXT_HINT_B_BACK] = "B 返回",
    [XM_TEXT_HINT_A_OPEN_B_BACK] = "A 打开  B 返回",
    [XM_TEXT_HINT_A_SELECT_B_BACK] = "A 选择  B 返回",
    [XM_TEXT_HINT_A_YES_B_NO] = "A 确认  B 取消",
    [XM_TEXT_HINT_B_CANCEL] = "B 取消",

    [XM_TEXT_GAMES_COMING_SOON] = "敬请期待",

    [XM_TEXT_PM_TITLE_CPU_RAM] = "CPU / RAM",
    [XM_TEXT_PM_TITLE_GPU_TEMP] = "GPU / 温度",
    [XM_TEXT_PM_LABEL_TEMP] = "温度",
    [XM_TEXT_PM_LABEL_GPU_TEMP] = "温度",
    [XM_TEXT_PM_LABEL_CPU_TEMP] = "温度",
    [XM_TEXT_PM_HINT] = "B 返回   < > 换页",

    [XM_TEXT_STATE_CONNECTED] = "已连接",
    [XM_TEXT_STATE_CONNECTING] = "连接中",
    [XM_TEXT_STATE_RECONNECTING] = "重连中",
    [XM_TEXT_STATE_SCANNING] = "扫描中",
    [XM_TEXT_STATE_SETUP] = "配网",
    [XM_TEXT_STATE_DISABLED] = "已关闭",
    [XM_TEXT_STATE_ON] = "已开启",
    [XM_TEXT_STATE_OFF] = "关闭",
    [XM_TEXT_STATE_NOT_CONFIGURED] = "未配置",
    [XM_TEXT_STATE_AUTH_FAILED] = "认证失败",
    [XM_TEXT_STATE_ERROR] = "错误",
    [XM_TEXT_STATE_NOT_CONNECTED] = "未连接",
    [XM_TEXT_STATE_UNAVAILABLE] = "不可用",
    [XM_TEXT_STATE_READY] = "就绪",
    [XM_TEXT_STATE_DEGRADED] = "降级",
    [XM_TEXT_STATE_UNINITIALIZED] = "未初始化",
    [XM_TEXT_STATE_UNKNOWN] = "未知",
    [XM_TEXT_STATE_NONE] = "无",
    [XM_TEXT_STATE_MOUNTED] = "已挂载",
    [XM_TEXT_STATE_UNMOUNTED] = "未挂载",

    [XM_TEXT_LEVEL_STRONG] = "强",
    [XM_TEXT_LEVEL_GOOD] = "良",
    [XM_TEXT_LEVEL_FAIR] = "一般",
    [XM_TEXT_LEVEL_WEAK] = "弱",

    [XM_TEXT_LABEL_STATUS] = "状态",
    [XM_TEXT_LABEL_SSID] = "SSID",
    [XM_TEXT_LABEL_SIGNAL] = "信号",
    [XM_TEXT_LABEL_IP] = "IP",
    [XM_TEXT_LABEL_CHIP] = "芯片",
    [XM_TEXT_LABEL_CPU] = "CPU",
    [XM_TEXT_LABEL_FLASH] = "Flash",
    [XM_TEXT_LABEL_PSRAM] = "PSRAM",
    [XM_TEXT_LABEL_IDF] = "IDF",
    [XM_TEXT_LABEL_FW] = "FW",
    [XM_TEXT_LABEL_AUTO_CONNECT] = "自动连接",
    [XM_TEXT_LABEL_CONFIGURE] = "配网",
    [XM_TEXT_LABEL_FORGET] = "忘记网络",
    [XM_TEXT_LABEL_FORGET_CONFIRM] = "确认忘记?",
    [XM_TEXT_LABEL_PASSWORD] = "密码",
    [XM_TEXT_LABEL_OPEN] = "访问",

    [XM_TEXT_TOOLS_SYSTEM_INFO] = "系统信息",
    [XM_TEXT_TOOLS_ABOUT] = "关于",
    [XM_TEXT_TOOLS_ASSETS] = "资源",
    [XM_TEXT_LABEL_PROJECT] = "项目",
    [XM_TEXT_LABEL_FIRMWARE] = "固件",
    [XM_TEXT_LABEL_AUTHOR] = "作者",
    [XM_TEXT_LABEL_REPO] = "仓库",

    [XM_TEXT_ASSETS_PARTITION] = "资源分区",
    [XM_TEXT_ASSETS_TOTAL] = "容量",
    [XM_TEXT_ASSETS_USED] = "已用",
    [XM_TEXT_ASSETS_FONT] = "字体",
    [XM_TEXT_ASSETS_GLYPHS] = "字形数",
    [XM_TEXT_ASSETS_SD] = "SD 卡",

    [XM_TEXT_SETTINGS_DISPLAY] = "显示",
    [XM_TEXT_SETTINGS_SOUND] = "声音",
    [XM_TEXT_SETTINGS_SYSTEM] = "系统",
    [XM_TEXT_SETTINGS_SERVICE] = "设置服务",
    [XM_TEXT_SETTINGS_AUDIO_SERVICE] = "音频服务",
    [XM_TEXT_SETTINGS_BRIGHTNESS_FIXED] = "亮度固定",
    [XM_TEXT_SETTINGS_BACKLIGHT_VCC] = "背光直连 VCC",
    [XM_TEXT_SETTINGS_NO_DISPLAY] = "无显示设置项",
    [XM_TEXT_SETTINGS_NODE12] = "将在节点 12 实现",
    [XM_TEXT_SETTINGS_FROM_NVS] = "已从 NVS 加载",
    [XM_TEXT_SETTINGS_RECOVERED] = "默认值已恢复",
    [XM_TEXT_SETTINGS_NOT_PERSISTED] = "未持久化",
    [XM_TEXT_SETTINGS_DEFAULTS] = "已应用默认值",
    [XM_TEXT_SETTINGS_SETTINGS_UNAVAILABLE] = "设置服务不可用",
    [XM_TEXT_SETTINGS_NVS_ERROR] = "NVS 错误",
    [XM_TEXT_SETTINGS_STORED_FIELDS] = "已保存 2 项",

    [XM_TEXT_SETTINGS_PROV_TITLE] = "Wi-Fi 配网",
    [XM_TEXT_SETTINGS_PROV_WAITING] = "等待中...",
    [XM_TEXT_SETTINGS_PROV_CONNECTING] = "连接中...",
    [XM_TEXT_SETTINGS_PROV_CONNECTED_NOT_SAVED] = "已连接，未保存",
    [XM_TEXT_SETTINGS_PROV_WRONG_PASSWORD] = "密码错误",
    [XM_TEXT_SETTINGS_PROV_NOT_FOUND] = "网络未找到",
    [XM_TEXT_SETTINGS_PROV_TIMEOUT] = "连接超时",
    [XM_TEXT_SETTINGS_PROV_FAILED] = "连接失败",
    [XM_TEXT_SETTINGS_PROV_WAIT_PHONE] = "等待手机配网",

    [XM_TEXT_SETTINGS_MSG_SAVE_FAILED] = "保存失败",
    [XM_TEXT_SETTINGS_MSG_APPLY_FAILED] = "应用失败",
    [XM_TEXT_SETTINGS_MSG_SETUP_FAILED] = "配网失败",
    [XM_TEXT_SETTINGS_MSG_FORGOTTEN] = "已忘记网络",
    [XM_TEXT_SETTINGS_MSG_FORGET_FAILED] = "忘记失败",
    [XM_TEXT_SETTINGS_MSG_PRESS_CONFIRM] = "按 A 确认",
    [XM_TEXT_SETTINGS_MSG_CANCELLED] = "已取消",
    [XM_TEXT_SETTINGS_MSG_SETUP_COMPLETE] = "配网完成",
    [XM_TEXT_SETTINGS_MSG_SETUP_CLOSED] = "配网已结束",
    [XM_TEXT_SETTINGS_MSG_SETUP_CANCELLED] = "配网已取消",
    [XM_TEXT_SETTINGS_MSG_SETUP_STOP_FAILED] = "停止配网失败",

    [XM_TEXT_HT_PAGE_LIGHT] = "光照",
    [XM_TEXT_HT_PAGE_THERM] = "热敏",
    [XM_TEXT_HT_PAGE_MOTION] = "运动",
    [XM_TEXT_HT_PAGE_LED1] = "LED1",
    [XM_TEXT_HT_PAGE_LED2] = "LED2",
    [XM_TEXT_HT_PAGE_BUZZER] = "蜂鸣器",
    [XM_TEXT_HT_PAGE_MOTOR1] = "电机1",
    [XM_TEXT_HT_PAGE_MOTOR2] = "电机2",
    [XM_TEXT_HT_PAGE_SD] = "存储卡",
    [XM_TEXT_HT_PAGE_GPIO25] = "GPIO25",
    [XM_TEXT_HT_PAGE_GPIO26] = "GPIO26",
    [XM_TEXT_HT_PAGE_GPIO32] = "ADC32",
    [XM_TEXT_HT_PAGE_GPIO33] = "ADC33",
    [XM_TEXT_HT_PAGE_SYSTEM] = "系统",
    [XM_TEXT_HT_PAGE_ABOUT] = "关于",

    [XM_TEXT_HT_HINT_PAGE] = "L/R 换页",
    [XM_TEXT_HT_HINT_SAMPLE] = "A 采样   L/R",
    [XM_TEXT_HT_HINT_RESCAN] = "A 重扫   L/R",
    [XM_TEXT_HT_HINT_TOGGLE_B_OFF] = "A 切换   B 关闭",
    [XM_TEXT_HT_HINT_BUZZER] = "U/D 频率  A 发声  B 停止",
    [XM_TEXT_HT_HINT_MOTOR_RUN] = "U/D PWM  A 关闭  B 停止",
    [XM_TEXT_HT_HINT_MOTOR_IDLE] = "U/D PWM  A 输出  B 换向",
    [XM_TEXT_HT_HINT_DUTY] = "U/D 占空  A 切换  B 关闭",
    [XM_TEXT_HT_HINT_SD_UNMOUNT] = "B 卸载  L/R",
    [XM_TEXT_HT_HINT_HOLD_TO_EXIT] = "长按退出",

    [XM_TEXT_HT_ABSENT] = "缺失",
    [XM_TEXT_HT_MOUNTED] = "已挂载",
    [XM_TEXT_HT_NO_CARD] = "无卡",
    [XM_TEXT_HT_OK] = "正常",
    [XM_TEXT_HT_FAIL] = "失败",
    [XM_TEXT_HT_ADC_FAIL] = "ADC 失败",
    [XM_TEXT_HT_PWM_INIT_FAIL] = "PWM 初始化失败",

    [XM_TEXT_HT_GESTURE_ABSENT] = "缺失",
    [XM_TEXT_HT_GESTURE_READY] = "就绪",
    [XM_TEXT_HT_GESTURE_TILT_UP] = "上倾",
    [XM_TEXT_HT_GESTURE_TILT_DN] = "下倾",
    [XM_TEXT_HT_GESTURE_TILT_R] = "右倾",
    [XM_TEXT_HT_GESTURE_TILT_L] = "左倾",
    [XM_TEXT_HT_GESTURE_LEVEL] = "水平",

    [XM_TEXT_ERR_TIMEOUT] = "超时",
    [XM_TEXT_ERR_NOT_FOUND] = "未找到",
    [XM_TEXT_ERR_STATE] = "状态错误",
    [XM_TEXT_ERR_ARG] = "参数错误",
    [XM_TEXT_ERR_FAIL] = "失败",
    [XM_TEXT_ERR_GENERIC] = "错误",

    [XM_TEXT_HT_ACT_MOTOR1_STOPPED] = "电机1已停止",
    [XM_TEXT_HT_ACT_MOTOR2_STOPPED] = "电机2已停止",
    [XM_TEXT_HT_ACT_MOTOR1_OUTPUT] = "电机1已输出",
    [XM_TEXT_HT_ACT_MOTOR2_OUTPUT] = "电机2已输出",
    [XM_TEXT_HT_ACT_MOTOR1_DIR] = "电机1换向",
    [XM_TEXT_HT_ACT_MOTOR2_DIR] = "电机2换向",
    [XM_TEXT_HT_ACT_MOTOR_CMD_FAIL] = "电机命令失败",
    [XM_TEXT_HT_ACT_PWM_ZERO] = "PWM 为零",
    [XM_TEXT_HT_ACT_PWM_CMD_FAIL] = "PWM 命令失败",
    [XM_TEXT_HT_ACT_PWM_INIT_FAIL] = "PWM 初始化失败",
    [XM_TEXT_HT_ACT_GPIO25_OFF] = "GPIO25 已关闭",
    [XM_TEXT_HT_ACT_GPIO26_OFF] = "GPIO26 已关闭",
    [XM_TEXT_HT_ACT_GPIO25_PWM] = "GPIO25 输出",
    [XM_TEXT_HT_ACT_GPIO26_PWM] = "GPIO26 输出",
    [XM_TEXT_HT_ACT_DUTY_ZERO] = "占空比为零",
    [XM_TEXT_HT_ACT_DUTY_SET] = "占空比已设置",
    [XM_TEXT_HT_ACT_SAMPLED] = "已采样",
    [XM_TEXT_HT_ACT_ADC_READ_FAIL] = "ADC 读取失败",
    [XM_TEXT_HT_ACT_MPU_READY] = "MPU 就绪",
    [XM_TEXT_HT_ACT_MPU_ABSENT] = "MPU 缺失",
    [XM_TEXT_HT_ACT_LED1_TOGGLED] = "LED1 已切换",
    [XM_TEXT_HT_ACT_LED2_TOGGLED] = "LED2 已切换",
    [XM_TEXT_HT_ACT_LED1_OFF] = "LED1 已关闭",
    [XM_TEXT_HT_ACT_LED2_OFF] = "LED2 已关闭",
    [XM_TEXT_HT_ACT_LED_CMD_FAIL] = "LED 命令失败",
    [XM_TEXT_HT_ACT_BEEP] = "已发声",
    [XM_TEXT_HT_ACT_BUZZER_INIT_FAIL] = "蜂鸣器初始化失败",
    [XM_TEXT_HT_ACT_BUZZER_STOP] = "蜂鸣器已停止",
    [XM_TEXT_HT_ACT_SD_MOUNTED] = "SD 卡已挂载",
    [XM_TEXT_HT_ACT_SD_UNMOUNTED] = "SD 卡已卸载",
    [XM_TEXT_HT_ACT_SD_UNMOUNT_FAIL] = "SD 卡卸载失败",
    [XM_TEXT_HT_ACT_NO_SD_CARD] = "无 SD 卡",
    [XM_TEXT_HT_ACT_RESCANNED] = "已重新扫描",
    [XM_TEXT_HT_ACT_I2C_INIT_FAIL] = "I2C 初始化失败",
    [XM_TEXT_HT_ACT_PITCH_SET] = "音调已设置",
    [XM_TEXT_HT_ACT_POWER_SET] = "功率已设置",
    [XM_TEXT_HT_ACT_CANCELED] = "已取消",

    [XM_TEXT_HT_ABOUT_MODEL] = "型号",
    [XM_TEXT_HT_ABOUT_AUTHOR] = "作者",
    [XM_TEXT_HT_ABOUT_CHIP_REV] = "芯片版本",
    [XM_TEXT_HT_ABOUT_SYSTEM] = "系统",
    [XM_TEXT_HT_ABOUT_TARGET] = "目标",
    [XM_TEXT_HT_ABOUT_BUILD] = "构建",
    [XM_TEXT_HT_ABOUT_CLOCKS] = "时钟",
    [XM_TEXT_HT_ABOUT_STORAGE] = "存储",
    [XM_TEXT_HT_ABOUT_DISPLAY] = "显示",
    [XM_TEXT_HT_ABOUT_BOARD_IO] = "板卡 IO",
    [XM_TEXT_HT_ABOUT_KEYS] = "按键",
};

/* Table geometry: a missing entry would read as NULL, not as a shift. */
_Static_assert(sizeof(s_text_en) / sizeof(s_text_en[0]) == XM_TEXT_COUNT,
               "en table must hold one entry per text id");
_Static_assert(sizeof(s_text_zh) / sizeof(s_text_zh[0]) == XM_TEXT_COUNT,
               "zh table must hold one entry per text id");

static bool s_language_latched;
static bool s_chinese;

static bool text_uses_chinese(void)
{
    if (!s_language_latched) {
        /* Latched once: the language must not flip under a drawn label. */
        s_chinese = xiaomiao_font_service_ready();
        s_language_latched = true;
    }
    return s_chinese;
}

const char *xiaomiao_text(xiaomiao_text_id_t id)
{
    if ((int)id < 0 || (int)id >= XM_TEXT_COUNT) {
        return TEXT_UNKNOWN;
    }

    if (text_uses_chinese()) {
        const char *zh = s_text_zh[id];
        if (zh != NULL && zh[0] != '\0') {
            return zh;
        }
    }

    const char *en = s_text_en[id];
    return (en != NULL && en[0] != '\0') ? en : TEXT_UNKNOWN;
}

bool xiaomiao_i18n_is_chinese(void)
{
    return text_uses_chinese();
}
