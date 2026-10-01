/*
 * Framework screen idle implementation (goal 20261001-1449).
 *
 * Drawing: the confirmed single-outline little TV from
 * docs/screen-idle-preview.html (only that preview version is the
 * reference; the old two-ring candidates are not implemented). The
 * preview SVG group
 *
 *     translate(80 64) scale(1.6) translate(-82.5 -69)
 *
 * maps design coordinates onto the 160 x 128 screen; every point below
 * goes through xform() so the confirmed shape is not re-drawn by hand.
 * Lines are LVGL polylines over static point arrays:
 *
 *   body outline  design stroke 2.2 -> 3.5 px -> drawn 4 px
 *   eyes/antennae design stroke 4.5 -> 7.2 px -> drawn 7 px
 *   mouth         design stroke 1.5 -> 2.4 px -> drawn 2 px
 *
 * Animation (one LVGL timer, only alive while idle, ~20 fps):
 *   eyes     4.6 s cycle, closed (rounded horizontal line) at 42%..46%
 *   antennas 4.8 s cycle, each rotates around its root: lift 13 deg,
 *            brief outward drop 5 deg, settle; the right side mirrors
 *   mouth    5.8 s cycle, the double arc deepens at 60%..84%
 * All phases derive from the absolute monotonic clock, so a missed tick
 * never accumulates. Only the animated lines are invalidated, never the
 * whole screen.
 */

#include "xiaomiao_screen_idle.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "framework/xiaomiao_wifi_indicator.h"
#include "services/xiaomiao_settings_service.h"

static const char TAG[] = "screen_idle";

/* Confirmed palette: one light line colour on a pure black screen. */
#define IDLE_COLOR_BG    0x000000
#define IDLE_COLOR_LINE  0xE8E8E6

/* Screen geometry. */
#define IDLE_SCREEN_W    160
#define IDLE_SCREEN_H    128
/* The preview group transform. */
#define IDLE_TX          80.0f
#define IDLE_TY          64.0f
#define IDLE_SCALE       1.6f
#define IDLE_PIVOT_X     82.5f
#define IDLE_PIVOT_Y     69.0f

/* Line widths rounded from the preview strokes (see file comment). */
#define IDLE_WIDTH_BODY  4
#define IDLE_WIDTH_EYE   7
#define IDLE_WIDTH_ANT   7
#define IDLE_WIDTH_MOUTH 2

/* Animation periods in ms and the phase windows in [0, 1). */
#define IDLE_PERIOD_EYE     4600
#define IDLE_PERIOD_ANT     4800
#define IDLE_PERIOD_MOUTH   5800
#define IDLE_EYE_CLOSE_AT   0.42f
#define IDLE_EYE_OPEN_AT    0.46f
#define IDLE_MOUTH_DEEP_AT  0.60f
#define IDLE_MOUTH_REST_AT  0.84f

/* Antenna swing in degrees (goal: lift 13, brief drop 5, mirror). */
#define IDLE_ANT_LIFT_DEG   13.0f
#define IDLE_ANT_DROP_DEG   (-5.0f)

/* One LVGL timer drives all three actions; ~20 fps. */
#define IDLE_ANIM_PERIOD_MS 50

/* Default when the Settings Service cannot serve the field. */
#define IDLE_MINUTES_DEFAULT 2

/* Body outline sample plan: M + 4 cubics (4 interior points each) with
 * 3 straight runs between them and one closing segment. */
#define IDLE_BODY_PTS  21
/* Mouth: start point + two quadratic arcs, 4 points each. */
#define IDLE_MOUTH_PTS 9

typedef enum {
    IDLE_STATE_NORMAL = 0,
    /* Overlay shown, waiting for the wake key. */
    IDLE_STATE_IDLE,
    /* Woke up: swallow every input until no key is held. */
    IDLE_STATE_WAKE,
} idle_state_t;

static bool s_initialized;
static idle_state_t s_state = IDLE_STATE_NORMAL;
/* Debounced overall key state, fed from the keypad read callback. */
static bool s_any_pressed;
/* Set on the wake press edge, executed by the next poll(). */
static bool s_wake_pending;
/* 64-bit monotonic ms of the last key press/release edge. */
static int64_t s_last_activity_ms;
/* Configured idle minutes; 0 disables the screen. Refreshed in poll(). */
static uint8_t s_idle_minutes = IDLE_MINUTES_DEFAULT;

/* Overlay objects. All children of s_overlay, so deleting the overlay
 * frees every line; the pointers are only cleared alongside. */
static lv_obj_t *s_overlay;
static lv_obj_t *s_body_line;
static lv_obj_t *s_eye_left;
static lv_obj_t *s_eye_right;
static lv_obj_t *s_mouth_line;
static lv_obj_t *s_ant_left;
static lv_obj_t *s_ant_right;
static lv_timer_t *s_anim_timer;

/* Wi-Fi indicator visibility to restore on wake (goal acceptance 8: a
 * page that hid the icon, e.g. the Pomodoro detail, must not get it
 * back). */
static bool s_indicator_prev_visible = true;

/* Current poses, used to skip redundant invalidations. */
static bool s_eyes_closed;
static bool s_mouth_deep_active;
static float s_ant_deg = 0.0f;

/*
 * Static point arrays. lv_line keeps the pointer, so they must outlive
 * the objects; they do (file scope) and nothing is allocated at runtime.
 */
static lv_point_precise_t s_body_pts[IDLE_BODY_PTS];
static lv_point_precise_t s_eye_left_slant[2];
static lv_point_precise_t s_eye_left_closed[2];
static lv_point_precise_t s_eye_right_slant[2];
static lv_point_precise_t s_eye_right_closed[2];
static lv_point_precise_t s_mouth_normal[IDLE_MOUTH_PTS];
static lv_point_precise_t s_mouth_deep[IDLE_MOUTH_PTS];
static lv_point_precise_t s_ant_left_pts[2];
static lv_point_precise_t s_ant_right_pts[2];

/* Preview group transform: design -> screen coordinates. */
static void idle_xform(float sx, float sy, lv_point_precise_t *out)
{
    out->x = (int32_t)lroundf((sx - IDLE_PIVOT_X) * IDLE_SCALE + IDLE_TX);
    out->y = (int32_t)lroundf((sy - IDLE_PIVOT_Y) * IDLE_SCALE + IDLE_TY);
}

/* Cubic Bezier point at t; a and b are the control points. */
static void idle_cubic(float p0x, float p0y, float ax, float ay,
                       float bx, float by, float p1x, float p1y, float t,
                       float *out_x, float *out_y)
{
    const float u = 1.0f - t;
    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;

    *out_x = w0 * p0x + w1 * ax + w2 * bx + w3 * p1x;
    *out_y = w0 * p0y + w1 * ay + w2 * by + w3 * p1y;
}

/* Quadratic Bezier point at t; c is the single control point. */
static void idle_quad(float p0x, float p0y, float cx, float cy,
                      float p1x, float p1y, float t,
                      float *out_x, float *out_y)
{
    const float u = 1.0f - t;

    *out_x = u * u * p0x + 2.0f * u * t * cx + t * t * p1x;
    *out_y = u * u * p0y + 2.0f * u * t * cy + t * t * p1y;
}

/*
 * Sample the confirmed body path
 *   M53 65 C54 56 59 54 69 54 L96 54 C105 54 109 58 110 66
 *   L113 86 C114 93 110 95 102 95 L62 95 C53 95 51 92 52 85 Z
 * into s_body_pts (see IDLE_BODY_PTS for the count breakdown).
 */
static void idle_build_body(void)
{
    static const struct {
        float x, y;
    } corners[] = {
        {53.0f, 65.0f}, {69.0f, 54.0f}, {96.0f, 54.0f},
        {110.0f, 66.0f}, {113.0f, 86.0f}, {102.0f, 95.0f},
        {62.0f, 95.0f}, {52.0f, 85.0f},
    };
    /* The four cubic segments of the path, each [ctrl1, ctrl2, end]. */
    static const struct {
        float c1x, c1y, c2x, c2y, ex, ey;
    } curves[] = {
        {54.0f, 56.0f, 59.0f, 54.0f, 69.0f, 54.0f},
        {105.0f, 54.0f, 109.0f, 58.0f, 110.0f, 66.0f},
        {114.0f, 93.0f, 110.0f, 95.0f, 102.0f, 95.0f},
        {53.0f, 95.0f, 51.0f, 92.0f, 52.0f, 85.0f},
    };
    /* Straight runs between the segments, as [from, to] corner indexes. */
    static const uint8_t lines[][2] = {
        {1, 2}, {3, 4}, {5, 6},
    };

    uint8_t n = 0;
    idle_xform(corners[0].x, corners[0].y, &s_body_pts[n++]);

    float px = corners[0].x;
    float py = corners[0].y;
    for (size_t i = 0; i < sizeof(curves) / sizeof(curves[0]); ++i) {
        for (uint8_t k = 1; k <= 4; ++k) {
            float x, y;
            idle_cubic(px, py, curves[i].c1x, curves[i].c1y,
                       curves[i].c2x, curves[i].c2y,
                       curves[i].ex, curves[i].ey,
                       (float)k / 4.0f, &x, &y);
            idle_xform(x, y, &s_body_pts[n++]);
        }
        px = curves[i].ex;
        py = curves[i].ey;

        if (i < sizeof(lines) / sizeof(lines[0])) {
            const uint8_t to = lines[i][1];
            idle_xform(corners[to].x, corners[to].y, &s_body_pts[n++]);
            px = corners[to].x;
            py = corners[to].y;
        }
    }

    /* Close the outline back to the start point (the path's Z). */
    idle_xform(corners[0].x, corners[0].y, &s_body_pts[n++]);
}

/* Precompute both eye poses and both mouth poses from the preview. */
static void idle_build_face(void)
{
    /* Eyes: slanted lines "M64 73 L73 67" / "M91 67 L100 73"; closed
     * becomes the rounded horizontal line at y=71. */
    idle_xform(64.0f, 73.0f, &s_eye_left_slant[0]);
    idle_xform(73.0f, 67.0f, &s_eye_left_slant[1]);
    idle_xform(64.0f, 71.0f, &s_eye_left_closed[0]);
    idle_xform(73.0f, 71.0f, &s_eye_left_closed[1]);
    idle_xform(91.0f, 67.0f, &s_eye_right_slant[0]);
    idle_xform(100.0f, 73.0f, &s_eye_right_slant[1]);
    idle_xform(91.0f, 71.0f, &s_eye_right_closed[0]);
    idle_xform(100.0f, 71.0f, &s_eye_right_closed[1]);

    /* Mouth: "M76 81 Q78 86 82 81 Q86 86 89 81" and its deepened
     * 60%..84% pose "Q78 88 82 82 Q86 88 89 81". */
    static const struct {
        float c1x, c1y, m1x, m1y, c2x, c2y, ex, ey;
    } mouths[2] = {
        /* normal */
        {78.0f, 86.0f, 82.0f, 81.0f, 86.0f, 86.0f, 89.0f, 81.0f},
        /* deepened */
        {78.0f, 88.0f, 82.0f, 82.0f, 86.0f, 88.0f, 89.0f, 81.0f},
    };

    for (uint8_t pose = 0; pose < 2; ++pose) {
        lv_point_precise_t *pts = (pose == 0) ? s_mouth_normal : s_mouth_deep;
        uint8_t n = 0;
        idle_xform(76.0f, 81.0f, &pts[n++]);

        float x, y;
        idle_quad(76.0f, 81.0f, mouths[pose].c1x, mouths[pose].c1y,
                  mouths[pose].m1x, mouths[pose].m1y, 0.25f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_quad(76.0f, 81.0f, mouths[pose].c1x, mouths[pose].c1y,
                  mouths[pose].m1x, mouths[pose].m1y, 0.50f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_quad(76.0f, 81.0f, mouths[pose].c1x, mouths[pose].c1y,
                  mouths[pose].m1x, mouths[pose].m1y, 0.75f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_xform(mouths[pose].m1x, mouths[pose].m1y, &pts[n++]);

        idle_quad(mouths[pose].m1x, mouths[pose].m1y, mouths[pose].c2x,
                  mouths[pose].c2y, mouths[pose].ex, mouths[pose].ey,
                  0.25f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_quad(mouths[pose].m1x, mouths[pose].m1y, mouths[pose].c2x,
                  mouths[pose].c2y, mouths[pose].ex, mouths[pose].ey,
                  0.50f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_quad(mouths[pose].m1x, mouths[pose].m1y, mouths[pose].c2x,
                  mouths[pose].c2y, mouths[pose].ex, mouths[pose].ey,
                  0.75f, &x, &y);
        idle_xform(x, y, &pts[n++]);
        idle_xform(mouths[pose].ex, mouths[pose].ey, &pts[n++]);
    }
}

static lv_obj_t *idle_add_line(lv_obj_t *parent, const lv_point_precise_t *points,
                               uint32_t count, int32_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    if (line == NULL) {
        return NULL;
    }

    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(IDLE_COLOR_LINE), 0);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static int64_t idle_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Antenna angle in degrees for the 0..1 phase of the 4.8 s cycle. */
static float idle_antenna_deg(float phase)
{
    if (phase >= 0.82f || phase < 0.60f) {
        return 0.0f;
    }
    if (phase < 0.66f) {
        /* 0 -> lift */
        return IDLE_ANT_LIFT_DEG * (phase - 0.60f) / 0.06f;
    }
    if (phase < 0.71f) {
        /* lift -> outward drop */
        return IDLE_ANT_LIFT_DEG +
               (IDLE_ANT_DROP_DEG - IDLE_ANT_LIFT_DEG) * (phase - 0.66f) / 0.05f;
    }
    if (phase < 0.76f) {
        /* drop -> lift again */
        return IDLE_ANT_DROP_DEG +
               (IDLE_ANT_LIFT_DEG - IDLE_ANT_DROP_DEG) * (phase - 0.71f) / 0.05f;
    }
    /* lift -> rest */
    return IDLE_ANT_LIFT_DEG * (1.0f - (phase - 0.76f) / 0.06f);
}

/*
 * Recompute the two antenna tips for `deg`. The left root sits at
 * (72, 54) with the unrotated tip offset (-6, -10); the right side is
 * the exact mirror, which is what the preview's mirrored keyframes do.
 */
static void idle_antenna_points(float deg)
{
    const float rad = deg * 3.14159265f / 180.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);
    const float dx = -6.0f * c + 10.0f * s;
    const float dy = -6.0f * s - 10.0f * c;

    idle_xform(72.0f, 54.0f, &s_ant_left_pts[0]);
    idle_xform(72.0f + dx, 54.0f + dy, &s_ant_left_pts[1]);
    idle_xform(92.0f, 54.0f, &s_ant_right_pts[0]);
    idle_xform(92.0f - dx, 54.0f + dy, &s_ant_right_pts[1]);
}

/* Swap an animated line to another precomputed pose. The object is
 * invalidated before and after so the old and the new geometry both
 * repaint (the pose boxes differ in size). */
static void idle_swap_points(lv_obj_t *line, const lv_point_precise_t *points,
                             uint32_t count)
{
    if (line == NULL) {
        return;
    }

    lv_obj_invalidate(line);
    lv_line_set_points(line, points, count);
    lv_obj_invalidate(line);
}

static void idle_anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    const int64_t now = idle_now_ms();

    /* Antennas: recompute both tips every tick. */
    float phase = (float)(now % IDLE_PERIOD_ANT) / (float)IDLE_PERIOD_ANT;
    const float deg = idle_antenna_deg(phase);
    if (deg != s_ant_deg) {
        s_ant_deg = deg;
        idle_antenna_points(deg);
        lv_obj_invalidate(s_ant_left);
        lv_obj_invalidate(s_ant_right);
    }

    /* Eyes: closed only inside the short 42%..46% window. */
    phase = (float)(now % IDLE_PERIOD_EYE) / (float)IDLE_PERIOD_EYE;
    const bool closed = (phase >= IDLE_EYE_CLOSE_AT) && (phase < IDLE_EYE_OPEN_AT);
    if (closed != s_eyes_closed) {
        s_eyes_closed = closed;
        idle_swap_points(s_eye_left, closed ? s_eye_left_closed : s_eye_left_slant, 2);
        idle_swap_points(s_eye_right, closed ? s_eye_right_closed : s_eye_right_slant, 2);
    }

    /* Mouth: deepened inside 60%..84%. */
    phase = (float)(now % IDLE_PERIOD_MOUTH) / (float)IDLE_PERIOD_MOUTH;
    const bool deep = (phase >= IDLE_MOUTH_DEEP_AT) && (phase < IDLE_MOUTH_REST_AT);
    if (deep != s_mouth_deep_active) {
        s_mouth_deep_active = deep;
        idle_swap_points(s_mouth_line, deep ? s_mouth_deep : s_mouth_normal,
                         IDLE_MOUTH_PTS);
    }
}

/* Tear the overlay down: animation first, then the object tree, then
 * the Wi-Fi indicator returns to the visibility the page underneath
 * expects. Safe to call in any state. */
static void idle_overlay_destroy(void)
{
    if (s_anim_timer != NULL) {
        lv_timer_delete(s_anim_timer);
        s_anim_timer = NULL;
    }

    if (s_overlay != NULL) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
    s_body_line = NULL;
    s_eye_left = NULL;
    s_eye_right = NULL;
    s_mouth_line = NULL;
    s_ant_left = NULL;
    s_ant_right = NULL;

    xiaomiao_wifi_indicator_set_visible(s_indicator_prev_visible);
}

/* Build the overlay on the top layer. A failure at any point cleans the
 * partially built objects and reports the error; the page underneath
 * keeps working and the state machine stays NORMAL (goal: entry failure
 * cleans up partial objects, and a black empty layer must never lock
 * the device). */
static esp_err_t idle_overlay_create(void)
{
    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_indicator_prev_visible = xiaomiao_wifi_indicator_is_visible();

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    if (overlay == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, IDLE_SCREEN_W, IDLE_SCREEN_H);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(IDLE_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    /* Decoration only: the App below keeps the group focus. */
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    s_overlay = overlay;

    idle_antenna_points(0.0f);
    s_ant_left = idle_add_line(overlay, s_ant_left_pts, 2, IDLE_WIDTH_ANT);
    s_ant_right = idle_add_line(overlay, s_ant_right_pts, 2, IDLE_WIDTH_ANT);
    s_body_line = idle_add_line(overlay, s_body_pts, IDLE_BODY_PTS, IDLE_WIDTH_BODY);
    s_eye_left = idle_add_line(overlay, s_eye_left_slant, 2, IDLE_WIDTH_EYE);
    s_eye_right = idle_add_line(overlay, s_eye_right_slant, 2, IDLE_WIDTH_EYE);
    s_mouth_line = idle_add_line(overlay, s_mouth_normal, IDLE_MOUTH_PTS,
                                 IDLE_WIDTH_MOUTH);

    if (s_ant_left == NULL || s_ant_right == NULL || s_body_line == NULL ||
        s_eye_left == NULL || s_eye_right == NULL || s_mouth_line == NULL) {
        ESP_LOGW(TAG, "overlay line allocation failed");
        idle_overlay_destroy();
        return ESP_ERR_NO_MEM;
    }

    s_eyes_closed = false;
    s_mouth_deep_active = false;
    s_ant_deg = 0.0f;

    s_anim_timer = lv_timer_create(idle_anim_timer_cb, IDLE_ANIM_PERIOD_MS, NULL);
    if (s_anim_timer == NULL) {
        ESP_LOGW(TAG, "animation timer creation failed");
        idle_overlay_destroy();
        return ESP_ERR_NO_MEM;
    }

    xiaomiao_wifi_indicator_set_visible(false);
    return ESP_OK;
}

/* Refresh the configured idle minutes; a missing Service keeps the last
 * value and a fresh boot starts from the default. */
static void idle_refresh_config(void)
{
    xiaomiao_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    if (xiaomiao_settings_get(&settings) == ESP_OK &&
        (settings.screen_idle_minutes == 0 || settings.screen_idle_minutes == 1 ||
         settings.screen_idle_minutes == 2 || settings.screen_idle_minutes == 5 ||
         settings.screen_idle_minutes == 10)) {
        s_idle_minutes = settings.screen_idle_minutes;
    }
}

esp_err_t xiaomiao_screen_idle_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    idle_build_body();
    idle_build_face();

    s_initialized = true;
    s_state = IDLE_STATE_NORMAL;
    s_any_pressed = false;
    s_wake_pending = false;
    s_last_activity_ms = idle_now_ms();
    s_idle_minutes = IDLE_MINUTES_DEFAULT;
    idle_refresh_config();

    ESP_LOGI(TAG, "screen idle ready (minutes=%u)", (unsigned)s_idle_minutes);
    return ESP_OK;
}

void xiaomiao_screen_idle_poll(void)
{
    if (!s_initialized) {
        return;
    }

    switch (s_state) {
    case IDLE_STATE_NORMAL:
        idle_refresh_config();
        if (s_idle_minutes != 0 && !s_any_pressed &&
            idle_now_ms() - s_last_activity_ms >=
                (int64_t)s_idle_minutes * 60000) {
            if (idle_overlay_create() == ESP_OK) {
                s_state = IDLE_STATE_IDLE;
                ESP_LOGI(TAG, "idle screen on");
            }
            else {
                /* Stay awake rather than blank the UI without the
                 * overlay: the next poll retries after the timeout. */
                s_last_activity_ms = idle_now_ms();
            }
        }
        break;

    case IDLE_STATE_IDLE:
        if (s_wake_pending) {
            s_wake_pending = false;
            /* Visual wake happens on the press; the swallow keeps
             * running until every key is released. */
            idle_overlay_destroy();
            s_state = IDLE_STATE_WAKE;
            ESP_LOGI(TAG, "idle screen off, swallowing until release");
        }
        break;

    case IDLE_STATE_WAKE:
        if (!s_any_pressed) {
            s_state = IDLE_STATE_NORMAL;
        }
        break;

    default:
        s_state = IDLE_STATE_NORMAL;
        break;
    }
}

void xiaomiao_screen_idle_report_key(bool any_pressed)
{
    if (!s_initialized || any_pressed == s_any_pressed) {
        return;
    }

    s_any_pressed = any_pressed;
    /* Press and release edges both count as activity; a held key can
     * never reach the timeout and a release restarts the countdown. */
    s_last_activity_ms = idle_now_ms();

    if (any_pressed && s_state == IDLE_STATE_IDLE) {
        s_wake_pending = true;
    }
}

bool xiaomiao_screen_idle_input_swallowed(void)
{
    return s_initialized &&
           (s_state == IDLE_STATE_IDLE || s_state == IDLE_STATE_WAKE ||
            s_wake_pending);
}
