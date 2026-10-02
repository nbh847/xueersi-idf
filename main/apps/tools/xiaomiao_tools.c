/*
 * Tools App with the Pomodoro timer (goal 20261001-1036).
 *
 * Three views inside one App session:
 *   menu    - the single "Pomodoro" entry (the 2026-09-30 transition
 *             empty state is gone; the design gives Tools exactly one
 *             tool)
 *   detail  - the black-and-white timer page: 86 px ring, central
 *             hourglass, 16 px minute/second digits, per-state footer
 *             hints or a two-option row (resume/reset, again/rest,
 *             reset confirmation)
 *   setup   - the pomodoro alert-sound preference (persisted through
 *             the Settings Service; the system master switch still
 *             wins)
 *
 * The timer state lives in the Pomodoro Service, so leaving the App or
 * the page never stops the countdown; the page only renders snapshots.
 * While the detail or setup view is open the global coloured Wi-Fi
 * indicator is hidden through the Framework visibility interface and
 * restored on every exit path, including app close.
 *
 * Input follows the shared App convention: the App root takes the
 * focus in the LVGL default group; B is latched on the press edge
 * (LVGL keeps repeating LV_KEY_ESC and never reports the release), the
 * repeats are dropped and a 20 ms LVGL timer clears the latch once the
 * keypad reports the release. Closing the App is deferred to that
 * timer, so the focused object is never deleted from its own key
 * callback. The same timer drives the one-second page refresh and the
 * hourglass animation ticks; no extra LVGL timers are created.
 *
 * All LVGL calls stay on the UI task. The App touches no GPIO, SPI,
 * I2C, ADC, LEDC, NVS or network API directly: sound goes through the
 * Buzzer Service and the preference through the Settings Service.
 */

#include "xiaomiao_tools.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "framework/xiaomiao_fonts.h"
#include "framework/xiaomiao_i18n.h"
#include "framework/xiaomiao_navigation.h"
#include "framework/xiaomiao_screen_idle.h"
#include "framework/xiaomiao_wifi_indicator.h"
#include "services/xiaomiao_buzzer_service.h"
#include "services/xiaomiao_pomodoro_service.h"
#include "services/xiaomiao_settings_service.h"

static const char TAG[] = "tools";

#define TOOLS_APP_ID    "tools"
/* A list reads as "several entries" and does not clash with the Hardware
 * Test icon (LV_SYMBOL_SETTINGS). LV_SYMBOL_WRENCH does not exist in the
 * locked LVGL 9.5 (goal decision 3). */
#define TOOLS_APP_ICON  LV_SYMBOL_LIST

/* The menu keeps the shared palette; the detail page switches to the
 * pure black/grey/white pomodoro look confirmed in the design. */
#define TOOLS_COLOR_SCREEN_BG      0x0E1016
#define TOOLS_COLOR_TITLE          0xC8D0E0
#define TOOLS_COLOR_MUTED          0x5A6478
#define TOOLS_COLOR_ROW_BG         0x161B26
#define TOOLS_COLOR_ROW_BORDER     0x3A4356
#define TOOLS_COLOR_FOCUS_BG       0x2A3450
#define TOOLS_COLOR_FOCUS_BORDER   0x8FB6E8

#define POMO_COLOR_BG              0x000000
#define POMO_COLOR_TITLE           0xF2F2F2
#define POMO_COLOR_RING_BG         0x383838
#define POMO_COLOR_RING            0xF2F2F2
/* Hourglass palette follows the confirmed browser preview: glass stroke
 * #eee, sand fill #f2f2f2, falling thread #ddd, shine ticks #777. */
#define POMO_COLOR_GLASS           0xE8E8E8
#define POMO_COLOR_SAND            0xF2F2F2
#define POMO_COLOR_FALL            0xC8C8C8
#define POMO_COLOR_SHINE           0x777777
#define POMO_COLOR_OPT_BG          0x101010
#define POMO_COLOR_OPT_BORDER      0x404040
#define POMO_COLOR_OPT_FOCUS_BG    0x404040
#define POMO_COLOR_OPT_FOCUS_BRD   0xF2F2F2
#define POMO_COLOR_MUTED           0x9A9A9A

/* 160 x 128 layout. */
#define TOOLS_SCREEN_W   160
#define TOOLS_TITLE_Y    2
#define TOOLS_TITLE_H    16
#define TOOLS_ROW_Y      44
#define TOOLS_ROW_H      24
#define TOOLS_FOOTER_Y   110
#define TOOLS_FOOTER_H   14

/* Ring geometry (goal: circle centre about (80,64), diameter 86 px,
 * line width about 5 px). */
#define POMO_RING_SIZE   86
#define POMO_RING_X      (80 - POMO_RING_SIZE / 2)
#define POMO_RING_Y      (64 - POMO_RING_SIZE / 2)
#define POMO_RING_WIDTH  5

/* Digits: 16 px, centred at x=55 and x=105 (pulled toward the central
 * hourglass after the first on-device check), baseline y=69 (goal). */
#define POMO_DIGIT_W     30
#define POMO_DIGIT_H     18
#define POMO_MIN_X       (55 - POMO_DIGIT_W / 2)
#define POMO_SEC_X       (105 - POMO_DIGIT_W / 2)
#define POMO_DIGIT_Y     56

/* Option row at the bottom; each item keeps a readable 74 px box. */
#define POMO_OPT_Y       106
#define POMO_OPT_H       18
#define POMO_OPT_W       74
#define POMO_OPT1_X      4
#define POMO_OPT2_X      (TOOLS_SCREEN_W - 4 - POMO_OPT_W)

/* Hourglass: centred in the ring. The confirmed look is the rounded
 * glass outline from the browser preview (about 18 x 36 px at 82%),
 * approximated by sampling its bezier path into a polyline; it stays
 * clear of the digit boxes (36..66 and 94..124). */
#define HG_CX            80
#define HG_CY            64
#define HG_PTS           21

/* Animation budget (goal): pour ticks at 100 ms, pile changes about
 * every 1.1 s, flip of 10 frames at 50 ms after about 5.5 s. */
#define POUR_TICK_MS     100
#define POUR_TICKS       55
#define FLIP_FRAMES      10
#define FLIP_TICK_MS     50

/* Poll period for the B release check; the latch itself is event driven. */
#define TOOLS_B_RELEASE_POLL_MS 20
/* Page refresh cadence: digits and ring once per second. */
#define TOOLS_UI_TICK_MS        1000

typedef enum {
    VIEW_MENU = 0,
    VIEW_DETAIL,
    VIEW_SETUP,
} view_t;

/* Which bottom row the detail page shows. */
typedef enum {
    MODE_PLAIN = 0,   /* one footer hint, A does the primary action */
    MODE_OPTIONS,     /* two selectable items (paused: continue / reset) */
} detail_mode_t;

/* The input root owns the focus; the content container is rebuilt per
 * view and never holds the focus itself. */
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static lv_timer_t *s_b_release_timer;
static bool s_b_latched;
static bool s_back_pending;
static uint32_t s_quick_latched_key;
static bool s_quick_save_failed;
static uint32_t s_quick_error_ms;

#define POMO_HOLD_US 360000LL
#define POMO_BURN_US 2000000LL
static bool s_a_consumed;
static bool s_a_pending;
static bool s_a_burning;
static int64_t s_a_started_us;
static int64_t s_burn_started_us;
static uint32_t s_burn_ms;
static xiaomiao_pomodoro_state_t s_a_stage;
static lv_obj_t *s_burn_layer;

static view_t s_view;
static detail_mode_t s_mode;
static uint8_t s_option_index;
static xiaomiao_pomodoro_snapshot_t s_snap;

/* Detail page object references. */
static lv_obj_t *s_title;
static lv_obj_t *s_arc;
static lv_obj_t *s_min_label;
static lv_obj_t *s_min_box;
static lv_obj_t *s_quick_arrow[2];
static const lv_point_precise_t s_quick_arrow_pts[2][3] = {
    {{7, 0}, {0, 5}, {7, 10}},
    {{0, 0}, {7, 5}, {0, 10}},
};
static lv_obj_t *s_sec_label;
static lv_obj_t *s_glass_line;
static lv_obj_t *s_shine_line[2];
static lv_obj_t *s_fall_line;
/* Sand as three stacked 2 px bars per bulb (top-to-bottom indexes).
 * Filled bars read as a clean stepped pyramid on the small screen;
 * the line-traced sand of the first build rendered as stray slashes. */
static lv_obj_t *s_up_bars[3];
static lv_obj_t *s_pile_bars[3];
static lv_obj_t *s_footer;
static lv_obj_t *s_opt[2];
/* The text label INSIDE each option box; the boxes are plain objects,
 * so text updates must target these (a lv_label_set_text on the box is
 * type confusion that corrupts the object - the on-device crash). */
static lv_obj_t *s_opt_label[2];

/* Hourglass data. The base outline samples the confirmed rounded-glass
 * bezier path from the browser preview (18 x 36 px box at 82%, centred)
 * into a closed polyline; the flip frames are its y-scaled copies, built
 * once per page entry (goal: lookup frames, no per-frame rotation).
 * LVGL 9.5 lines take the precise point type. */
static lv_point_precise_t s_glass_base[HG_PTS];
static lv_point_precise_t s_glass_frames[FLIP_FRAMES][HG_PTS];

/* Glass shine ticks from the preview (left-wall marks); static shape,
 * rescaled with the 90% outline. */
static const lv_point_precise_t s_shine_pts[2][2] = {
    { {76, 52}, {76, 56} },
    { {76, 73}, {76, 76} },
};

/* Animation state: pouring ticks advance the pile, then the flip runs
 * and the cycle restarts with the sand on top again. */
static uint32_t s_anim_last_ms;
static uint32_t s_ui_last_ms;
static uint16_t s_pour_ticks;
static int8_t s_flip_frame; /* -1 while pouring */

static lv_obj_t *tools_create_label(lv_obj_t *parent, const char *text,
                                    const lv_font_t *font, uint32_t color,
                                    lv_label_long_mode_t mode);

static void tools_build_menu(lv_obj_t *content);
static void tools_show_view(view_t view);
static void detail_refresh(void);
static void detail_hold_abort(void);

static lv_obj_t *tools_create_label(lv_obj_t *parent, const char *text,
                                    const lv_font_t *font, uint32_t color,
                                    lv_label_long_mode_t mode)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        ESP_LOGW(TAG, "label allocation failed");
        return NULL;
    }

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, mode);
    lv_label_set_text(label, text);
    return label;
}

/* Single-line label with an explicit box; DOTS keeps overlong text inside
 * the box instead of overflowing it (goal decision 14). */
static lv_obj_t *tools_place_label(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, uint32_t color,
                                   int32_t x, int32_t y, int32_t width,
                                   int32_t height, lv_text_align_t align)
{
    lv_obj_t *label = tools_create_label(parent, text, font, color,
                                         LV_LABEL_LONG_MODE_DOTS);
    if (label == NULL) {
        return NULL;
    }

    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    return label;
}

/* ------------------------------------------------------------------ */
/* Focus sweep band (directional focus transition, goal 20260928-1037; */
/* 2026-10-01 user rule: every key-driven focus switch carries it)     */
/* ------------------------------------------------------------------ */
/* The band is a temporary child of the newly focused row, so the row
 * clips it and no full-screen effect is created. The static focus is
 * applied before the band starts, so an allocation failure or an
 * overloaded board only costs the animation, never the focus state.
 * Setup rows move vertically (8 px band, as in Settings); the detail
 * option row moves horizontally (12 px band, as in the Launcher).
 * Fast repeated keys cancel the previous band, and every page teardown
 * stops the animation before deleting objects. */
#define TOOLS_SWEEP_DURATION_MS 140
#define TOOLS_SWEEP_BAND_V      8
#define TOOLS_SWEEP_BAND_H      12
#define TOOLS_COLOR_SWEEP       0xE8F0FF

static lv_obj_t *s_sweep_band;

static void tools_band_stop(void)
{
    if (s_sweep_band == NULL) {
        return;
    }

    /* Remove the animation first: no exec or completed callback may
     * run against a deleted object afterwards. */
    lv_anim_delete(s_sweep_band, NULL);
    lv_obj_delete(s_sweep_band);
    s_sweep_band = NULL;
}

/* Runs after the animation left the animation list (lv_anim.c), so
 * deleting the band object here is safe. */
static void tools_band_completed_cb(lv_anim_t *anim)
{
    if (s_sweep_band != NULL && anim->var == s_sweep_band) {
        lv_obj_delete(s_sweep_band);
        s_sweep_band = NULL;
    }
}

static void tools_band_y_exec_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void tools_band_x_exec_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

/* Build the strip child of `row`; returns NULL when allocation failed,
 * which only costs the decoration. */
static lv_obj_t *tools_band_create(lv_obj_t *row, bool horizontal)
{
    lv_obj_t *band = lv_obj_create(row);
    if (band == NULL) {
        return NULL;
    }

    lv_obj_remove_style_all(band);
    if (horizontal) {
        lv_obj_set_size(band, TOOLS_SWEEP_BAND_H, LV_PCT(100));
    }
    else {
        lv_obj_set_size(band, LV_PCT(100), TOOLS_SWEEP_BAND_V);
    }
    lv_obj_set_style_bg_color(band, lv_color_hex(TOOLS_COLOR_SWEEP), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, 1, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
    return band;
}

/* Vertical sweep (setup rows): `from_top` is true when the old row was
 * above the new one, so the band enters at the top edge. */
static void tools_band_start_vertical(lv_obj_t *row, bool from_top,
                                      int32_t row_h)
{
    tools_band_stop();
    if (row == NULL) {
        return;
    }

    lv_obj_t *band = tools_band_create(row, false);
    if (band == NULL) {
        return;
    }

    const int32_t from = from_top ? -TOOLS_SWEEP_BAND_V : row_h;
    const int32_t to = from_top ? row_h : -TOOLS_SWEEP_BAND_V;
    lv_obj_set_pos(band, 0, from);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, band);
    lv_anim_set_exec_cb(&anim, tools_band_y_exec_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, TOOLS_SWEEP_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, tools_band_completed_cb);
    if (lv_anim_start(&anim) == NULL) {
        lv_obj_delete(band);
        return;
    }

    s_sweep_band = band;
}

/* Horizontal sweep (detail options): `from_left` is true when the old
 * option was left of the new one, so the band enters at the left edge. */
static void tools_band_start_horizontal(lv_obj_t *row, bool from_left,
                                        int32_t row_w, int32_t row_h)
{
    tools_band_stop();
    if (row == NULL) {
        return;
    }

    lv_obj_t *band = tools_band_create(row, true);
    if (band == NULL) {
        return;
    }

    const int32_t from = from_left ? -TOOLS_SWEEP_BAND_H : row_w;
    const int32_t to = from_left ? row_w : -TOOLS_SWEEP_BAND_H;
    lv_obj_set_pos(band, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, band);
    lv_anim_set_exec_cb(&anim, tools_band_x_exec_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, TOOLS_SWEEP_DURATION_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, tools_band_completed_cb);
    if (lv_anim_start(&anim) == NULL) {
        lv_obj_delete(band);
        return;
    }

    s_sweep_band = band;
}

/* ------------------------------------------------------------------ */
/* Hourglass drawing                                                   */
/* ------------------------------------------------------------------ */

/*
 * Base outline: the rounded glass silhouette, sampled from the preview
 * path "M-9 -18H9C9 -8 5 -5 2 -2Q0 0 2 2C5 5 9 8 9 18H-9C-9 8 -5 5
 * -2 2Q0 0 -2 -2C-5 -5 -9 -8 -9 -18Z" at 90% (about 16 x 32 px, centred
 * at 80,64 - enlarged from the first 82% cut after the on-device
 * check): flat top and bottom bars, convex walls easing into a thin
 * neck. Symmetric about HG_CY, which is what makes the 180 degree flip
 * land back on this exact shape. The count MUST match HG_PTS: a spare
 * slot zero-fills to (0,0) and the closing segment would stroke a
 * diagonal to the screen corner.
 */
static void hourglass_build_base(void)
{
    static const lv_point_precise_t base[HG_PTS] = {
        {72, 48}, {88, 48},                                   /* top bar   */
        {88, 54}, {86, 57}, {84, 60}, {82, 62},               /* right wall in  */
        {81, 64},                                             /* neck      */
        {84, 68}, {86, 71}, {88, 75}, {88, 80},               /* right wall out */
        {72, 80},                                             /* bottom bar */
        {72, 75}, {74, 71}, {76, 68}, {78, 66},               /* left wall in   */
        {79, 64},                                             /* neck      */
        {76, 60}, {74, 57}, {72, 54},                         /* left wall out  */
        {72, 48},                                             /* close     */
    };

    memcpy(s_glass_base, base, sizeof(base));

    /* Frame k scales y around HG_CY by cos(18 deg * (k+1)); at k = 9
     * the factor is -1 and the polyline maps onto itself. */
    for (uint8_t k = 0; k < FLIP_FRAMES; ++k) {
        const float factor = cosf((float)(k + 1) * 0.3141593f);
        for (uint8_t i = 0; i < HG_PTS; ++i) {
            s_glass_frames[k][i].x = s_glass_base[i].x;
            s_glass_frames[k][i].y =
                HG_CY + (int32_t)((float)(s_glass_base[i].y - HG_CY) * factor);
        }
    }
}

/* Draw one polyline; points must live in static storage. */
static void hourglass_set_line(lv_obj_t *line, const lv_point_precise_t *points,
                               uint8_t count, uint32_t color)
{
    if (line == NULL) {
        return;
    }

    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
}

/* Falling-thread geometry is tiny, so it is rebuilt into this static
 * buffer on every apply. Sand itself is drawn as stacked 2 px bars
 * (see s_up_bars), which reads as a clean stepped pyramid instead of
 * the stray slashes the first line-traced build produced. */
static lv_point_precise_t s_fall_pts[2];

/* Show/hide one 2 px sand bar centred on the glass axis at row `cy`
 * with half-width `hw`; hidden bars keep their last geometry. */
static void sand_bar_set(lv_obj_t *bar, bool visible, int32_t cy, int32_t hw)
{
    if (bar == NULL) {
        return;
    }

    if (!visible) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(bar, hw * 2, 2);
    lv_obj_set_pos(bar, HG_CX - hw, cy);
}

static void hourglass_apply(void)
{
    if (s_glass_line == NULL) {
        return;
    }

    if (s_flip_frame >= 0) {
        hourglass_set_line(s_glass_line, s_glass_frames[s_flip_frame],
                           HG_PTS, POMO_COLOR_GLASS);
        lv_obj_add_flag(s_shine_line[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_shine_line[1], LV_OBJ_FLAG_HIDDEN);
        for (uint8_t i = 0; i < 3; ++i) {
            lv_obj_add_flag(s_up_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_pile_bars[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_fall_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    hourglass_set_line(s_glass_line, s_glass_base, HG_PTS, POMO_COLOR_GLASS);
    lv_obj_clear_flag(s_shine_line[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_shine_line[1], LV_OBJ_FLAG_HIDDEN);

    /* Sand level 0..6 drives two stepped pyramids of 2 px bars. The
     * upper mound sits on the neck and depletes as the lower pile
     * grows; each bar widens toward its bulb's wide side so the stacks
     * stay inside the glass walls. */
    const uint8_t pile = (uint8_t)(s_pour_ticks * 6U / POUR_TICKS);
    const uint8_t upper = (uint8_t)(6 - pile);

    /* Upper bars: rows at y=54/57/60, widest on top. */
    sand_bar_set(s_up_bars[0], upper >= 5, 54, (uint8_t)(5 + (upper - 5) / 2));
    sand_bar_set(s_up_bars[1], upper >= 3, 57, (uint8_t)(3 + (upper - 3) / 2));
    sand_bar_set(s_up_bars[2], upper >= 1, 60, 2);

    /* Pile bars: rows at y=70/73/76, widest at the bottom; the bottom
     * row clamps to the bulb floor. */
    sand_bar_set(s_pile_bars[0], pile >= 5, 70, (uint8_t)(2 + (pile - 5) / 2));
    sand_bar_set(s_pile_bars[1], pile >= 3, 73, (uint8_t)(3 + (pile - 3) / 2));
    sand_bar_set(s_pile_bars[2], pile >= 1, 76, (uint8_t)(2 + MIN(pile, 5)));

    /* The falling thread animates only while a phase is running; it
     * hangs between the neck and the pile tip. */
    const bool running = (s_snap.state == XIAOMIAO_POMODORO_FOCUS_RUNNING ||
                          s_snap.state == XIAOMIAO_POMODORO_BREAK_RUNNING);
    if (running && s_fall_line != NULL) {
        s_fall_pts[0].x = HG_CX;
        s_fall_pts[0].y = 65;
        s_fall_pts[1].x = HG_CX;
        s_fall_pts[1].y = (int32_t)(67 + (s_pour_ticks % 3));
        hourglass_set_line(s_fall_line, s_fall_pts, 2, POMO_COLOR_FALL);
        lv_obj_clear_flag(s_fall_line, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_fall_line, LV_OBJ_FLAG_HIDDEN);
    }
}

/* One animation step; called from the shared 20 ms timer. Paused and
 * done phases freeze exactly where they are (goal). */
static void hourglass_tick(void)
{
    const bool running = (s_snap.state == XIAOMIAO_POMODORO_FOCUS_RUNNING ||
                          s_snap.state == XIAOMIAO_POMODORO_BREAK_RUNNING);
    if (!running) {
        return;
    }

    const uint32_t now = lv_tick_get();
    if ((int32_t)(now - s_anim_last_ms) < 0) {
        return;
    }

    if (s_flip_frame >= 0) {
        if (now - s_anim_last_ms < FLIP_TICK_MS) {
            return;
        }
        s_anim_last_ms = now;
        s_flip_frame++;
        if (s_flip_frame >= FLIP_FRAMES) {
            /* Turned over: the sand is back on top, the cycle starts
             * again. Pure decoration - the timer never reads this. */
            s_flip_frame = -1;
            s_pour_ticks = 0;
        }
    }
    else {
        if (now - s_anim_last_ms < POUR_TICK_MS) {
            return;
        }
        s_anim_last_ms = now;
        s_pour_ticks++;
        if (s_pour_ticks >= POUR_TICKS) {
            s_flip_frame = 0;
        }
    }

    hourglass_apply();
}

/* ------------------------------------------------------------------ */
/* Detail page rendering                                               */
/* ------------------------------------------------------------------ */

static const char *detail_title_text(void)
{
    switch (s_snap.state) {
    case XIAOMIAO_POMODORO_FOCUS_RUNNING:
    case XIAOMIAO_POMODORO_FOCUS_PAUSED:
        return (s_snap.state == XIAOMIAO_POMODORO_FOCUS_PAUSED)
                   ? xiaomiao_text(XM_TEXT_POMO_TITLE_PAUSED)
                   : xiaomiao_text(XM_TEXT_POMO_TITLE_FOCUS);
    case XIAOMIAO_POMODORO_BREAK_RUNNING:
    case XIAOMIAO_POMODORO_BREAK_PAUSED:
        return (s_snap.state == XIAOMIAO_POMODORO_BREAK_PAUSED)
                   ? xiaomiao_text(XM_TEXT_POMO_TITLE_PAUSED)
                   : xiaomiao_text(XM_TEXT_POMO_TITLE_BREAK);
    case XIAOMIAO_POMODORO_BREAK_DONE:
        return xiaomiao_text(XM_TEXT_POMO_TITLE_BREAK_DONE);
    case XIAOMIAO_POMODORO_FOCUS_DONE:
        /* Not reachable: focus completion rolls into the break, the
         * mapping only guards a future Service change. */
        return xiaomiao_text(XM_TEXT_POMO_TITLE_FOCUS_DONE);
    case XIAOMIAO_POMODORO_IDLE:
    default:
        return xiaomiao_text(XM_TEXT_POMO_TITLE_FOCUS);
    }
}

static void detail_option_texts(const char **out_first, const char **out_second)
{
    *out_first = xiaomiao_text(XM_TEXT_POMO_OPT_RESUME);
    *out_second = xiaomiao_text(XM_TEXT_POMO_OPT_RESET);
}

static const char *detail_footer_text(void)
{
    switch (s_snap.state) {
    case XIAOMIAO_POMODORO_FOCUS_RUNNING:
    case XIAOMIAO_POMODORO_BREAK_RUNNING:
        return xiaomiao_text(XM_TEXT_POMO_HINT_RUN);
    case XIAOMIAO_POMODORO_BREAK_DONE:
        return xiaomiao_text(XM_TEXT_POMO_HINT_DONE_FOCUS);
    case XIAOMIAO_POMODORO_IDLE:
    default:
        return xiaomiao_text(XM_TEXT_POMO_HINT_IDLE);
    }
}

/* Re-render every dynamic element from s_snap. */
static void detail_apply(void)
{
    if (s_title == NULL) {
        return;
    }

    lv_label_set_text(s_title, detail_title_text());

    /* Remaining seconds round up, so a fresh phase really shows 25:00
     * and only drops to 24:59 after the first full second (goal). */
    const uint32_t rem_s = (s_snap.remaining_ms + 999U) / 1000U;
    char text[8];
    snprintf(text, sizeof(text), "%02u", (unsigned)(rem_s / 60U));
    if (s_min_label != NULL) {
        lv_label_set_text(s_min_label, text);
    }
    snprintf(text, sizeof(text), "%02u", (unsigned)(rem_s % 60U));
    if (s_sec_label != NULL) {
        lv_label_set_text(s_sec_label, text);
    }

    if (s_arc != NULL) {
        lv_arc_set_value(s_arc, (int)s_snap.progress_percent);
    }
    for (uint8_t i = 0; i < 2; ++i) {
        if (s_quick_arrow[i] != NULL) {
            if (s_snap.state == XIAOMIAO_POMODORO_IDLE) {
                lv_obj_clear_flag(s_quick_arrow[i], LV_OBJ_FLAG_HIDDEN);
            }
            else {
                lv_obj_add_flag(s_quick_arrow[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    hourglass_apply();

    if (s_mode == MODE_PLAIN) {
        lv_obj_add_flag(s_opt[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_opt[1], LV_OBJ_FLAG_HIDDEN);
        if (s_footer != NULL) {
            lv_label_set_text(s_footer, s_quick_save_failed
                ? xiaomiao_text(XM_TEXT_SETTINGS_MSG_SAVE_FAILED)
                : s_a_burning ? xiaomiao_text(XM_TEXT_POMO_HINT_RELEASE)
                : detail_footer_text());
            lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    const char *first;
    const char *second;
    detail_option_texts(&first, &second);
    if (s_opt_label[0] != NULL) {
        lv_label_set_text(s_opt_label[0], first);
    }
    if (s_opt_label[1] != NULL) {
        lv_label_set_text(s_opt_label[1], second);
    }
    lv_obj_clear_flag(s_opt[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_opt[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < 2; ++i) {
        const bool focused = (i == s_option_index);
        lv_obj_set_style_bg_color(s_opt[i],
                                  lv_color_hex(focused ? POMO_COLOR_OPT_FOCUS_BG
                                                       : POMO_COLOR_OPT_BG),
                                  0);
        lv_obj_set_style_border_color(s_opt[i],
                                      lv_color_hex(focused ? POMO_COLOR_OPT_FOCUS_BRD
                                                           : POMO_COLOR_OPT_BORDER),
                                      0);
    }
}

/* Read the Service and refresh; called once per second and after every
 * user action. */
static void detail_refresh(void)
{
    xiaomiao_pomodoro_snapshot_t snap;
    if (xiaomiao_pomodoro_service_get_snapshot(&snap) == ESP_OK) {
        s_snap = snap;
    }

    /* A paused phase always shows options; everything else is a plain
     * page. A completed focus never rests in FOCUS_DONE: the Service
     * rolls straight into the break (2026-10-01 flow decision). */
    switch (s_snap.state) {
    case XIAOMIAO_POMODORO_FOCUS_PAUSED:
    case XIAOMIAO_POMODORO_BREAK_PAUSED:
        if (s_mode == MODE_PLAIN) {
            s_mode = MODE_OPTIONS;
            s_option_index = 0;
        }
        break;
    default:
        s_mode = MODE_PLAIN;
        break;
    }

    detail_apply();
}

/* ------------------------------------------------------------------ */
/* Detail actions                                                      */
/* ------------------------------------------------------------------ */

static void detail_start_running_phase(void)
{
    if (s_snap.state == XIAOMIAO_POMODORO_BREAK_DONE ||
        s_snap.state == XIAOMIAO_POMODORO_IDLE) {
        xiaomiao_pomodoro_service_start_focus();
    }
}

static void detail_action(void)
{
    tools_band_stop();
    s_quick_save_failed = false;
    switch (s_mode) {
    case MODE_OPTIONS: {
        /* Options exist only for a paused phase (continue / reset);
         * a finished focus rolls into the break by itself, and a
         * finished break is a plain "again" page. Reset runs at once
         * - no confirmation step (2026-10-01 user decision). */
        const bool first = (s_option_index == 0);
        if (first) {
            xiaomiao_pomodoro_service_resume();
        }
        else {
            xiaomiao_pomodoro_service_reset();
        }
        break;
    }
    case MODE_PLAIN:
    default:
        if (s_snap.state == XIAOMIAO_POMODORO_FOCUS_RUNNING ||
            s_snap.state == XIAOMIAO_POMODORO_BREAK_RUNNING) {
            xiaomiao_pomodoro_service_pause();
        }
        else {
            detail_start_running_phase();
        }
        break;
    }

    detail_refresh();
}

/* The confirmation never owns the timer deadline or preferences. */
static bool detail_running(xiaomiao_pomodoro_state_t state)
{
    return state == XIAOMIAO_POMODORO_FOCUS_RUNNING ||
           state == XIAOMIAO_POMODORO_BREAK_RUNNING;
}

static void detail_hold_abort(void)
{
    s_a_pending = false;
    s_a_burning = false;
    s_burn_ms = 0;
    if (s_burn_layer != NULL) {
        lv_obj_delete(s_burn_layer);
        s_burn_layer = NULL;
    }
}

static void spark_line(lv_layer_t *layer, float x, float y, float dx,
                       float dy, uint32_t color, uint8_t opa)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1.x = (int32_t)lroundf(x);
    line.p1.y = (int32_t)lroundf(y);
    line.p2.x = (int32_t)lroundf(x + dx);
    line.p2.y = (int32_t)lroundf(y + dy);
    line.width = 1;
    line.color = lv_color_hex(color);
    line.opa = opa;
    lv_draw_line(layer, &line);
}

/* One transparent object, fixed-phase sparks, no per-particle objects. */
static void detail_burn_draw(lv_event_t *event)
{
    if (!s_a_burning) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(s_burn_layer, &area);
    const float cx = area.x1 + 80;
    const float cy = area.y1 + 64;
    const float tau = 6.2831853f;
    const float progress = (float)s_burn_ms / 2000.0f;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t)cx;
    arc.center.y = (int32_t)cy;
    arc.radius = POMO_RING_SIZE / 2;
    arc.width = POMO_RING_WIDTH;
    arc.start_angle = 270;
    arc.end_angle = 270 + (int32_t)(progress * 360);
    arc.color = lv_color_hex(POMO_COLOR_RING_BG);
    arc.opa = LV_OPA_COVER;
    if (s_burn_ms > 0) {
        lv_draw_arc(layer, &arc);
    }
    const float angle = progress * tau - tau / 4;
    const float radius = POMO_RING_SIZE / 2.0f - POMO_RING_WIDTH / 2.0f;
    const float hx = cx + radius * cosf(angle);
    const float hy = cy + radius * sinf(angle);
    const uint32_t phase = s_burn_ms / 45;
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t seed = (phase * 97 + i * 53 + 17) % 251;
        const float a = seed * tau / 251;
        const float length = 2.5f + ((seed * 31) % 61) / 10.0f;
        spark_line(layer, hx + cosf(a), hy + sinf(a),
                   cosf(a) * length, sinf(a) * length,
                   i % 3 == 0 ? 0xFFFFFF : 0xDFEDFF,
                   (uint8_t)(150 + seed % 106));
    }
    for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t age = (s_burn_ms + i * 19) % 180;
        if (age > s_burn_ms) {
            continue;
        }
        const uint32_t birth = s_burn_ms - age;
        const uint32_t seed = (birth / 19 * 73 + i * 29) % 251;
        const float a = seed * tau / 251;
        const float u = age / 180.0f;
        const float old_angle = birth / 2000.0f * tau - tau / 4;
        const float distance = (3 + seed % 8) * u;
        const float x = cx + radius * cosf(old_angle) + cosf(a) * distance;
        const float y = cy + radius * sinf(old_angle) + sinf(a) * distance + u * u;
        spark_line(layer, x, y, -cosf(a) * (2.6f * (1 - u) + .3f),
                   -sinf(a) * (2.6f * (1 - u) + .3f),
                   0xDFEDFF, (uint8_t)(220 * (1 - u)));
        if (i % 4 == 0) {
            spark_line(layer, x - 1, y, 2, 0, 0xFFFFFF, (uint8_t)(150 * (1 - u)));
            spark_line(layer, x, y - 1, 0, 2, 0xFFFFFF, (uint8_t)(150 * (1 - u)));
        }
    }
    spark_line(layer, hx, hy, 1, 0, 0xFFFFFF, LV_OPA_COVER);
}

static bool detail_burn_create(void)
{
    s_burn_layer = lv_obj_create(s_content);
    if (s_burn_layer == NULL) {
        return false;
    }
    lv_obj_remove_style_all(s_burn_layer);
    lv_obj_set_size(s_burn_layer, TOOLS_SCREEN_W, 128);
    lv_obj_set_pos(s_burn_layer, 0, 0);
    lv_obj_clear_flag(s_burn_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_burn_layer, detail_burn_draw, LV_EVENT_DRAW_MAIN, NULL);
    return true;
}

/* Called both on input samples (including release) and on the App timer.
 * Poll the Service before deciding: natural phase completion wins. */
static void detail_hold_update(bool a_down)
{
    if (!s_a_pending) {
        if (!a_down) {
            s_a_consumed = false;
        }
        return;
    }
    xiaomiao_pomodoro_service_poll();
    xiaomiao_pomodoro_snapshot_t snap;
    if (s_view != VIEW_DETAIL || s_back_pending ||
        xiaomiao_screen_idle_input_swallowed() ||
        xiaomiao_pomodoro_service_get_snapshot(&snap) != ESP_OK ||
        snap.state != s_a_stage) {
        detail_hold_abort();
        s_a_consumed = a_down;
        if (s_view == VIEW_DETAIL) {
            detail_refresh();
        }
        return;
    }
    const int64_t elapsed = esp_timer_get_time() - s_a_started_us;
    if (!a_down) {
        const bool short_press = elapsed < POMO_HOLD_US &&
            s_keypad != NULL &&
            lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED;
        detail_hold_abort();
        s_a_consumed = false;
        if (short_press) {
            xiaomiao_pomodoro_service_pause();
        }
        detail_refresh();
        return;
    }
    if (elapsed < POMO_HOLD_US) {
        return;
    }
    if (!s_a_burning) {
        if (!detail_burn_create()) {
            detail_hold_abort();
            s_a_consumed = true;
            return;
        }
        s_a_burning = true;
        s_burn_started_us = esp_timer_get_time();
        detail_refresh();
    }
    const int64_t burn_elapsed = esp_timer_get_time() - s_burn_started_us;
    if (burn_elapsed >= POMO_BURN_US) {
        detail_hold_abort();
        s_a_consumed = true;
        xiaomiao_pomodoro_service_reset();
        detail_refresh();
        return;
    }
    s_burn_ms = (uint32_t)(burn_elapsed / 1000);
    lv_obj_invalidate(s_burn_layer);
}

static uint8_t detail_quick_minutes(uint8_t current, int step)
{
    static const uint8_t presets[] = {1, 5, 15, 25, 30, 45, 60};
    const size_t count = sizeof(presets) / sizeof(presets[0]);
    if (step > 0) {
        for (size_t i = 0; i < count; ++i) {
            if (presets[i] > current) {
                return presets[i];
            }
        }
        return presets[0];
    }
    for (size_t i = count; i > 0; --i) {
        if (presets[i - 1] < current) {
            return presets[i - 1];
        }
    }
    return presets[count - 1];
}

static void detail_quick_adjust(uint32_t key)
{
    if (s_quick_latched_key == key) {
        return;
    }
    s_quick_latched_key = key;
    tools_band_stop();
    xiaomiao_settings_t settings;
    esp_err_t err = xiaomiao_settings_get(&settings);
    if (err == ESP_OK) {
        const uint8_t next = detail_quick_minutes(settings.focus_minutes,
                                                 key == LV_KEY_RIGHT ? 1 : -1);
        if (next == settings.focus_minutes) {
            return;
        }
        settings.focus_minutes = next;
        err = xiaomiao_settings_set(&settings);
    }
    s_quick_save_failed = err != ESP_OK;
    if (s_quick_save_failed) {
        s_quick_error_ms = lv_tick_get();
        ESP_LOGW(TAG, "quick focus save failed: %s", esp_err_to_name(err));
    }
    detail_refresh();
    if (!s_quick_save_failed) {
        tools_band_start_horizontal(s_min_box, key == LV_KEY_RIGHT,
                                    POMO_DIGIT_W, POMO_DIGIT_H);
    }
}

static void detail_move(int step)
{
    /* The step is accepted for signature symmetry; with two options
     * any move just toggles the index. */
    (void)step;

    if (s_mode == MODE_PLAIN) {
        /* Arrows never skip a phase and never reset anything (goal). */
        return;
    }

    s_option_index = (s_option_index == 0) ? 1 : 0;
    detail_apply();

    /* Key-driven option switch carries the sweep (2026-10-01 user
     * rule): the band crosses the newly focused box from the old one's
     * side. */
    tools_band_start_horizontal(s_opt[s_option_index], step > 0,
                                POMO_OPT_W, POMO_OPT_H);
}

/* ------------------------------------------------------------------ */
/* Setup page                                                          */
/* ------------------------------------------------------------------ */

/* Setup page state: three rows (focus length / break length / alert
 * sound). Duration edits live in page-local copies and are persisted
 * when the row is left, when A confirms or on B - never per keypress -
 * so a failed write can revert to the stored values (goal: no flash
 * writes on decoration, and visible save-failure feedback). */
#define SETUP_ROW_COUNT  3
/* Row order on the page: durations first, the sound switch last. */
#define SETUP_ROW_FOCUS  0
#define SETUP_ROW_BREAK  1
#define SETUP_ROW_SOUND  2
#define SETUP_ROW_Y      28
#define SETUP_ROW_H      20
#define SETUP_ROW_STEP   24
#define SETUP_NOTE_Y     96

static uint8_t s_setup_index;
static uint8_t s_focus_minutes;
static uint8_t s_break_minutes;
static bool s_prefs_dirty;
static lv_obj_t *s_setup_row[SETUP_ROW_COUNT];
static lv_obj_t *s_setup_value[SETUP_ROW_COUNT];
static lv_obj_t *s_setup_note;

static void setup_note_set(const char *text)
{
    if (s_setup_note == NULL) {
        return;
    }

    lv_label_set_text(s_setup_note, text);
    lv_obj_set_style_text_color(s_setup_note,
                                lv_color_hex(TOOLS_COLOR_MUTED), 0);
}

static void setup_row_highlight(void)
{
    for (uint8_t i = 0; i < SETUP_ROW_COUNT; ++i) {
        if (s_setup_row[i] == NULL) {
            continue;
        }
        const bool focused = (i == s_setup_index);
        lv_obj_set_style_bg_color(s_setup_row[i],
                                  lv_color_hex(focused ? TOOLS_COLOR_FOCUS_BG
                                                       : TOOLS_COLOR_ROW_BG),
                                  0);
        lv_obj_set_style_border_color(s_setup_row[i],
                                      lv_color_hex(focused ? TOOLS_COLOR_FOCUS_BORDER
                                                           : TOOLS_COLOR_ROW_BORDER),
                                      0);
        lv_obj_set_style_border_width(s_setup_row[i], focused ? 2 : 1, 0);
    }
}

static void setup_row_set_value(uint8_t row)
{
    if (s_setup_value[row] == NULL) {
        return;
    }

    char text[16];
    if (row == SETUP_ROW_SOUND) {
        xiaomiao_settings_t settings;
        const bool on = (xiaomiao_settings_get(&settings) == ESP_OK) &&
                        settings.pomodoro_sound_enabled;
        lv_label_set_text(s_setup_value[SETUP_ROW_SOUND],
                          xiaomiao_text(on ? XM_TEXT_STATE_ON
                                           : XM_TEXT_STATE_OFF));
        return;
    }

    const uint8_t minutes = (row == SETUP_ROW_FOCUS) ? s_focus_minutes
                                                     : s_break_minutes;
    snprintf(text, sizeof(text), "%u %s", (unsigned)minutes,
             xiaomiao_text(XM_TEXT_POMO_UNIT_MIN));
    lv_label_set_text(s_setup_value[row], text);
}

static void setup_build(lv_obj_t *content)
{
    tools_place_label(content, xiaomiao_text(XM_TEXT_POMO_SETTINGS),
                      xiaomiao_font_small(), TOOLS_COLOR_TITLE, 0,
                      TOOLS_TITLE_Y, TOOLS_SCREEN_W, TOOLS_TITLE_H,
                      LV_TEXT_ALIGN_CENTER);

    /* Page-local edit copies; the stored values are only touched when
     * the row is left or confirmed. */
    xiaomiao_settings_t settings;
    if (xiaomiao_settings_get(&settings) == ESP_OK) {
        s_focus_minutes = settings.focus_minutes;
        s_break_minutes = settings.break_minutes;
    }
    else {
        s_focus_minutes = 25;
        s_break_minutes = 5;
    }
    s_prefs_dirty = false;
    s_setup_index = 0;

    static const xiaomiao_text_id_t row_labels[SETUP_ROW_COUNT] = {
        XM_TEXT_POMO_LABEL_FOCUS_LEN,
        XM_TEXT_POMO_LABEL_BREAK_LEN,
        XM_TEXT_POMO_LABEL_ALERT,
    };

    for (uint8_t i = 0; i < SETUP_ROW_COUNT; ++i) {
        lv_obj_t *row = lv_obj_create(content);
        if (row == NULL) {
            ESP_LOGW(TAG, "setup row allocation failed");
            s_setup_row[i] = NULL;
            s_setup_value[i] = NULL;
            continue;
        }
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 8, SETUP_ROW_Y + (int32_t)i * SETUP_ROW_STEP);
        lv_obj_set_size(row, TOOLS_SCREEN_W - 16, SETUP_ROW_H);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        s_setup_row[i] = row;

        lv_obj_t *name = tools_place_label(row, xiaomiao_text(row_labels[i]),
                                           xiaomiao_font_small(),
                                           TOOLS_COLOR_TITLE, 8, 3, 92,
                                           SETUP_ROW_H - 6,
                                           LV_TEXT_ALIGN_LEFT);
        if (name != NULL) {
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
        }
        s_setup_value[i] = tools_place_label(row, "",
                                             xiaomiao_font_small(),
                                             TOOLS_COLOR_TITLE, 100, 3,
                                             44, SETUP_ROW_H - 6,
                                             LV_TEXT_ALIGN_RIGHT);
        setup_row_set_value(i);
    }
    setup_row_highlight();

    s_setup_note = tools_place_label(content, "", xiaomiao_font_small(),
                                     TOOLS_COLOR_MUTED, 0, SETUP_NOTE_Y,
                                     TOOLS_SCREEN_W, 12,
                                     LV_TEXT_ALIGN_CENTER);

    /* The system master sound switch still wins over everything on
     * this page, and the page says so instead of leaving a silent
     * switch. */
    xiaomiao_settings_t check;
    if (xiaomiao_settings_get(&check) == ESP_OK && !check.sound_enabled) {
        setup_note_set(xiaomiao_text(XM_TEXT_POMO_SYSTEM_SOUND_OFF));
    }

    tools_place_label(content, xiaomiao_text(XM_TEXT_POMO_HINT_SETUP),
                      xiaomiao_font_small(), TOOLS_COLOR_MUTED, 0,
                      TOOLS_FOOTER_Y, TOOLS_SCREEN_W, TOOLS_FOOTER_H,
                      LV_TEXT_ALIGN_CENTER);
}

/*
 * Persist the page-local edits (and optionally flip the alert sound).
 * On a write failure the stored values win: the page-local copies are
 * reverted, the values re-rendered and the note line shows the save
 * failure (goal CP2).
 */
static bool setup_store(bool toggle_sound)
{
    xiaomiao_settings_t stored;
    if (xiaomiao_settings_get(&stored) != ESP_OK) {
        setup_note_set(xiaomiao_text(XM_TEXT_SETTINGS_MSG_SAVE_FAILED));
        return false;
    }

    const xiaomiao_settings_t previous = stored;
    stored.focus_minutes = s_focus_minutes;
    stored.break_minutes = s_break_minutes;
    if (toggle_sound) {
        stored.pomodoro_sound_enabled = !stored.pomodoro_sound_enabled;
    }

    const esp_err_t err = xiaomiao_settings_set(&stored);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storing pomodoro preferences failed: %s (0x%x)",
                 esp_err_to_name(err), (unsigned)err);
        s_focus_minutes = previous.focus_minutes;
        s_break_minutes = previous.break_minutes;
        setup_row_set_value(SETUP_ROW_FOCUS);
        setup_row_set_value(SETUP_ROW_BREAK);
        setup_note_set(xiaomiao_text(XM_TEXT_SETTINGS_MSG_SAVE_FAILED));
        return false;
    }

    s_prefs_dirty = false;
    if (toggle_sound && !stored.pomodoro_sound_enabled) {
        /* Turning the alert off also cancels a reminder that is still
         * playing; turning it on never replays the past (goal CP0). */
        xiaomiao_buzzer_service_cancel_alert();
    }
    setup_row_set_value(SETUP_ROW_SOUND);
    return true;
}

/* Left/right on a duration row: adjust by one minute inside the schema
 * range. The value is page-local until the row is left or confirmed. */
static void setup_adjust(int step)
{
    if (s_setup_index == SETUP_ROW_SOUND) {
        /* The sound row has nothing to adjust. */
        return;
    }

    const bool focus_row = (s_setup_index == SETUP_ROW_FOCUS);
    const int value = focus_row ? (int)s_focus_minutes : (int)s_break_minutes;
    const int max = focus_row ? 180 : 60;
    const int adjusted = MAX(1, MIN(value + step, max));
    if (adjusted == value) {
        return;
    }

    if (focus_row) {
        s_focus_minutes = (uint8_t)adjusted;
    }
    else {
        s_break_minutes = (uint8_t)adjusted;
    }
    s_prefs_dirty = true;
    setup_row_set_value(s_setup_index);
}

/* Up/down moves the row focus; leaving a dirty row persists it first. */
static void setup_move(int step)
{
    (void)setup_store(false);

    const int next = (int)s_setup_index + step;
    if (next < 0 || next >= SETUP_ROW_COUNT) {
        return;
    }

    s_setup_index = (uint8_t)next;
    setup_row_highlight();

    /* Key-driven row switch carries the sweep (2026-10-01 user rule). */
    tools_band_start_vertical(s_setup_row[s_setup_index], step > 0,
                              SETUP_ROW_H);
}

/* ------------------------------------------------------------------ */
/* View switching                                                      */
/* ------------------------------------------------------------------ */

static void detail_build(lv_obj_t *content);

/* Rebuild the content container for one view. The Wi-Fi indicator is
 * hidden for exactly the timer views and restored everywhere else. */
static void tools_show_view(view_t view)
{
    if (s_content == NULL) {
        return;
    }

    detail_hold_abort();
    s_a_consumed = s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_PRESSED &&
        lv_indev_get_key(s_keypad) == LV_KEY_ENTER;
    s_view = view;
    s_mode = MODE_PLAIN;
    s_option_index = 0;
    s_setup_index = 0;
    s_pour_ticks = 0;
    s_flip_frame = -1;

    /* The band is a child of the content rows: stop it before the
     * rebuild deletes them (goal decision: delete the animation before
     * the object). */
    tools_band_stop();

    lv_obj_clean(s_content);
    s_min_box = NULL;
    s_quick_arrow[0] = NULL;
    s_quick_arrow[1] = NULL;
    s_quick_save_failed = false;

    if (view == VIEW_MENU) {
        xiaomiao_wifi_indicator_set_visible(true);
        tools_build_menu(s_content);
        return;
    }

    if (view == VIEW_SETUP) {
        /* The setup page belongs to the timer flow: keep the icon
         * hidden so both timer views stay black and white (goal). */
        xiaomiao_wifi_indicator_set_visible(false);
        setup_build(s_content);
        return;
    }

    xiaomiao_wifi_indicator_set_visible(false);
    detail_build(s_content);
    detail_refresh();
}

/* ------------------------------------------------------------------ */
/* Detail page construction                                            */
/* ------------------------------------------------------------------ */

/* Build one option box plus its inner text label and store BOTH
 * references; `detail_apply` writes the label, never the box. */
static void detail_option_box_create(lv_obj_t *content, uint8_t index,
                                     int32_t x)
{
    lv_obj_t *box = lv_obj_create(content);
    if (box == NULL) {
        ESP_LOGW(TAG, "option box allocation failed");
        s_opt[index] = NULL;
        s_opt_label[index] = NULL;
        return;
    }

    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, POMO_OPT_Y);
    lv_obj_set_size(box, POMO_OPT_W, POMO_OPT_H);
    lv_obj_set_style_bg_color(box, lv_color_hex(POMO_COLOR_OPT_BG), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(POMO_COLOR_OPT_BORDER), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    s_opt[index] = box;

    lv_obj_t *label = lv_label_create(box);
    if (label == NULL) {
        ESP_LOGW(TAG, "option label allocation failed");
        s_opt_label[index] = NULL;
        return;
    }
    lv_obj_set_style_text_font(label, xiaomiao_font_small(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(POMO_COLOR_RING), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, 2, 2);
    lv_obj_set_size(label, POMO_OPT_W - 4, POMO_OPT_H - 4);
    lv_label_set_text(label, "");
    s_opt_label[index] = label;
}

static void detail_build(lv_obj_t *content)
{
    lv_obj_set_style_bg_color(s_root, lv_color_hex(POMO_COLOR_BG), 0);

    s_title = tools_place_label(content, xiaomiao_text(XM_TEXT_POMO_TITLE_FOCUS),
                                xiaomiao_font_small(), POMO_COLOR_TITLE, 0,
                                TOOLS_TITLE_Y, TOOLS_SCREEN_W, TOOLS_TITLE_H,
                                LV_TEXT_ALIGN_CENTER);

    /* Ring: rotation puts the range start at 12 o'clock; REVERSE draws
     * the remaining fraction ending at the start, so the gap grows
     * clockwise from the top as time runs out (goal). */
    s_arc = lv_arc_create(content);
    if (s_arc != NULL) {
        lv_obj_remove_style_all(s_arc);
        lv_obj_set_size(s_arc, POMO_RING_SIZE, POMO_RING_SIZE);
        lv_obj_set_pos(s_arc, POMO_RING_X, POMO_RING_Y);
        lv_arc_set_rotation(s_arc, 270);
        lv_arc_set_bg_angles(s_arc, 0, 360);
        lv_arc_set_range(s_arc, 0, 100);
        lv_arc_set_value(s_arc, 100);
        lv_arc_set_mode(s_arc, LV_ARC_MODE_REVERSE);
        lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_width(s_arc, POMO_RING_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_arc, POMO_RING_WIDTH, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_arc, lv_color_hex(POMO_COLOR_RING_BG),
                                   LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_arc, lv_color_hex(POMO_COLOR_RING),
                                   LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    }

    s_min_box = lv_obj_create(content);
    if (s_min_box != NULL) {
        lv_obj_remove_style_all(s_min_box);
        lv_obj_set_pos(s_min_box, POMO_MIN_X, POMO_DIGIT_Y);
        lv_obj_set_size(s_min_box, POMO_DIGIT_W, POMO_DIGIT_H);
        lv_obj_clear_flag(s_min_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s_min_box, LV_OBJ_FLAG_CLICKABLE);
    }
    s_min_label = tools_place_label(s_min_box != NULL ? s_min_box : content,
                                    "25", xiaomiao_font_body(), POMO_COLOR_RING,
                                    s_min_box != NULL ? 0 : POMO_MIN_X,
                                    s_min_box != NULL ? 0 : POMO_DIGIT_Y,
                                    POMO_DIGIT_W, POMO_DIGIT_H,
                                    LV_TEXT_ALIGN_CENTER);
    for (uint8_t i = 0; i < 2; ++i) {
        s_quick_arrow[i] = lv_line_create(content);
        if (s_quick_arrow[i] != NULL) {
            lv_line_set_points(s_quick_arrow[i], s_quick_arrow_pts[i], 3);
            lv_obj_set_pos(s_quick_arrow[i], i == 0 ? 22 : 131, 59);
            lv_obj_set_style_line_width(s_quick_arrow[i], 1, 0);
            lv_obj_set_style_line_color(s_quick_arrow[i],
                                        lv_color_hex(POMO_COLOR_RING), 0);
            lv_obj_add_flag(s_quick_arrow[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_sec_label = tools_place_label(content, "00", xiaomiao_font_body(),
                                    POMO_COLOR_RING, POMO_SEC_X, POMO_DIGIT_Y,
                                    POMO_DIGIT_W, POMO_DIGIT_H,
                                    LV_TEXT_ALIGN_CENTER);

    /* Hourglass: three line objects on static point buffers. */
    hourglass_build_base();

    s_glass_line = lv_line_create(content);
    if (s_glass_line != NULL) {
        lv_obj_set_pos(s_glass_line, 0, 0);
        lv_obj_set_style_line_width(s_glass_line, 1, 0);
        lv_obj_set_style_line_opa(s_glass_line, LV_OPA_COVER, 0);
    }
    for (uint8_t i = 0; i < 2; ++i) {
        s_shine_line[i] = lv_line_create(content);
        if (s_shine_line[i] != NULL) {
            lv_obj_set_pos(s_shine_line[i], 0, 0);
            lv_obj_set_style_line_width(s_shine_line[i], 1, 0);
            lv_obj_set_style_line_color(s_shine_line[i],
                                        lv_color_hex(POMO_COLOR_SHINE), 0);
            lv_line_set_points(s_shine_line[i], s_shine_pts[i], 2);
        }
    }
    /* Sand: six 2 px bars (three per bulb), hidden until the first
     * hourglass_apply() sizes them. */
    for (uint8_t i = 0; i < 3; ++i) {
        s_up_bars[i] = lv_obj_create(content);
        s_pile_bars[i] = lv_obj_create(content);
        for (uint8_t j = 0; j < 2; ++j) {
            lv_obj_t *bar = (j == 0) ? s_up_bars[i] : s_pile_bars[i];
            if (bar == NULL) {
                continue;
            }
            lv_obj_remove_style_all(bar);
            lv_obj_set_style_bg_color(bar, lv_color_hex(POMO_COLOR_SAND), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_fall_line = lv_line_create(content);
    if (s_fall_line != NULL) {
        lv_obj_set_pos(s_fall_line, 0, 0);
        lv_obj_set_style_line_width(s_fall_line, 1, 0);
    }

    /* One shared builder for both option boxes: box + text label, with
     * both references stored (the label is what detail_apply writes). */
    detail_option_box_create(content, 0, POMO_OPT1_X);
    detail_option_box_create(content, 1, POMO_OPT2_X);

    s_footer = tools_place_label(content, "", xiaomiao_font_small(),
                                 POMO_COLOR_MUTED, 0, TOOLS_FOOTER_Y,
                                 TOOLS_SCREEN_W, TOOLS_FOOTER_H,
                                 LV_TEXT_ALIGN_CENTER);

    s_anim_last_ms = lv_tick_get();
    s_ui_last_ms = lv_tick_get();
}

/* ------------------------------------------------------------------ */
/* Menu page                                                           */
/* ------------------------------------------------------------------ */

static void tools_build_menu(lv_obj_t *content)
{
    lv_obj_set_style_bg_color(s_root, lv_color_hex(TOOLS_COLOR_SCREEN_BG), 0);

    tools_place_label(content, xiaomiao_text(XM_TEXT_APP_TOOLS),
                      xiaomiao_font_small(), TOOLS_COLOR_TITLE, 0,
                      TOOLS_TITLE_Y, TOOLS_SCREEN_W, TOOLS_TITLE_H,
                      LV_TEXT_ALIGN_CENTER);

    /* The single entry row carries the static focus highlight; the App
     * root below stays the one focusable object. */
    lv_obj_t *row = lv_obj_create(content);
    if (row != NULL) {
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 8, TOOLS_ROW_Y);
        lv_obj_set_size(row, TOOLS_SCREEN_W - 16, TOOLS_ROW_H);
        lv_obj_set_style_bg_color(row, lv_color_hex(TOOLS_COLOR_FOCUS_BG), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(TOOLS_COLOR_FOCUS_BORDER), 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = tools_place_label(row, xiaomiao_text(XM_TEXT_TOOL_POMODORO),
                                            xiaomiao_font_body(),
                                            TOOLS_COLOR_TITLE, 8, 2,
                                            TOOLS_SCREEN_W - 32,
                                            TOOLS_ROW_H - 4,
                                            LV_TEXT_ALIGN_LEFT);
        if (label != NULL) {
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        }
    }

    tools_place_label(content, xiaomiao_text(XM_TEXT_HINT_A_OPEN_B_BACK),
                      xiaomiao_font_small(), TOOLS_COLOR_MUTED, 0,
                      TOOLS_FOOTER_Y, TOOLS_SCREEN_W, TOOLS_FOOTER_H,
                      LV_TEXT_ALIGN_CENTER);
}

/* ------------------------------------------------------------------ */
/* Keys and the shared timer                                           */
/* ------------------------------------------------------------------ */

/*
 * One physical B press moves exactly one level. The first ESC event of
 * a press latches, every repeat LVGL sends while B is held is dropped,
 * and the timer clears the latch once the key is released (goal
 * decision 9). Back means: setup -> detail -> menu -> Launcher; a
 * running timer is never touched.
 */
static void tools_handle_escape(void)
{
    if (s_b_latched) {
        return;
    }
    s_b_latched = true;
    s_back_pending = true;
}

static void tools_apply_back(void)
{
    /* CP0: B always returns one level - setup -> detail -> menu ->
     * Launcher - and never pauses or changes the timer. A pending
     * duration edit is persisted first, so B doubles as confirm. */
    if (s_view == VIEW_SETUP) {
        (void)setup_store(false);
        tools_show_view(VIEW_DETAIL);
        return;
    }

    if (s_view == VIEW_DETAIL) {
        tools_show_view(VIEW_MENU);
        return;
    }

    /* Menu: leave the App, deferred to the timer, so the App root is
     * not deleted while its own key callback is still running. */
    const esp_err_t err = xiaomiao_navigation_back();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "return to launcher failed: %s (0x%x)",
                 esp_err_to_name(err), (unsigned)err);
    }
}

/* LVGL sends the indev KEY event on every keypad sample, including
 * release samples. Observe it before object key dispatch so a short
 * release cannot be missed between the App's 20 ms timer ticks. */
static void tools_keypad_sample_cb(lv_event_t *event)
{
    (void)event;
    if (s_keypad != NULL) {
        detail_hold_update(lv_indev_get_state(s_keypad) == LV_INDEV_STATE_PRESSED &&
                           lv_indev_get_key(s_keypad) == LV_KEY_ENTER);
    }
    if (s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED) {
        s_quick_latched_key = 0;
    }
}

static void tools_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (key != s_quick_latched_key) {
        s_quick_latched_key = 0;
    }

    if (key == LV_KEY_ENTER && s_a_consumed) {
        return;
    }

    if (key == LV_KEY_ESC) {
        tools_handle_escape();
        return;
    }

    if (s_view == VIEW_MENU) {
        if (key == LV_KEY_ENTER) {
            tools_show_view(VIEW_DETAIL);
        }
        return;
    }

    if (s_view == VIEW_SETUP) {
        switch (key) {
        case LV_KEY_ENTER:
            /* A on a duration row confirms the pending edit; A on the
             * sound row toggles it. */
            (void)setup_store(s_setup_index == SETUP_ROW_SOUND);
            break;
        case LV_KEY_UP:
            setup_move(-1);
            break;
        case LV_KEY_DOWN:
            setup_move(1);
            break;
        case LV_KEY_LEFT:
            setup_adjust(-1);
            break;
        case LV_KEY_RIGHT:
            setup_adjust(1);
            break;
        default:
            break;
        }
        return;
    }

    /* Detail view. */
    switch (key) {
    case LV_KEY_ENTER:
        detail_refresh();
        s_a_consumed = true;
        if (detail_running(s_snap.state)) {
            s_a_pending = true;
            s_a_stage = s_snap.state;
            s_a_started_us = esp_timer_get_time();
        }
        else {
            detail_action();
        }
        break;
    case LV_KEY_LEFT:
    case LV_KEY_RIGHT:
        detail_refresh();
        if (s_snap.state == XIAOMIAO_POMODORO_IDLE) {
            detail_quick_adjust(key);
        }
        else {
            detail_move((key == LV_KEY_RIGHT) ? 1 : -1);
        }
        break;
    case LV_KEY_UP:
        /* CP0: the setup entry never pauses or changes the timer. */
        tools_show_view(VIEW_SETUP);
        break;
    default:
        break;
    }
}

/*
 * Runs from lv_timer_handler(): clears the B latch on release, applies
 * deferred backs, advances the hourglass decoration and refreshes the
 * digits and ring once per second. One timer for all of it, so the
 * page adds no LVGL timers beyond the App baseline.
 */
static void tools_b_release_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_b_latched && s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED) {
        s_b_latched = false;
    }

    if (s_back_pending) {
        s_back_pending = false;
        tools_apply_back();
        return;
    }

    detail_hold_update(s_keypad != NULL &&
                       lv_indev_get_state(s_keypad) == LV_INDEV_STATE_PRESSED &&
                       lv_indev_get_key(s_keypad) == LV_KEY_ENTER);
    if (s_view != VIEW_DETAIL) {
        return;
    }

    /* Read the timer once per second; the Service converts a deadline
     * on its own poll in the main loop, so the page cannot miss a
     * phase end even if this timer never fires. */
    const uint32_t now = lv_tick_get();
    if (s_quick_save_failed &&
        (uint32_t)(now - s_quick_error_ms) >= 2000U) {
        s_quick_save_failed = false;
        detail_refresh();
    }
    if ((uint32_t)(now - s_ui_last_ms) >= TOOLS_UI_TICK_MS) {
        s_ui_last_ms = now;
        detail_refresh();
        return;
    }

    hourglass_tick();
}

static lv_indev_t *tools_find_keypad(void)
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev != NULL;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
    }

    return NULL;
}

static void tools_open(void)
{
    if (s_root != NULL) {
        ESP_LOGE(TAG, "tools is already open");
        return;
    }

    lv_obj_t *root_parent = xiaomiao_navigation_app_root();
    if (root_parent == NULL) {
        ESP_LOGE(TAG, "no App content root to build Tools in");
        return;
    }

    lv_group_t *group = lv_group_get_default();
    if (group == NULL) {
        ESP_LOGE(TAG, "LVGL default group is not ready");
        return;
    }

    lv_indev_t *keypad = tools_find_keypad();
    if (keypad == NULL) {
        ESP_LOGE(TAG, "keypad indev missing, cannot detect the B release");
        return;
    }

    lv_obj_t *root = lv_obj_create(root_parent);
    if (root == NULL) {
        ESP_LOGE(TAG, "tools root allocation failed");
        return;
    }
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(TOOLS_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    s_root = root;

    lv_obj_t *content = lv_obj_create(root);
    if (content == NULL) {
        ESP_LOGE(TAG, "tools content allocation failed");
        lv_obj_delete(root);
        s_root = NULL;
        return;
    }
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    s_content = content;

    lv_timer_t *timer = lv_timer_create(tools_b_release_timer_cb,
                                        TOOLS_B_RELEASE_POLL_MS, NULL);
    if (timer == NULL) {
        ESP_LOGE(TAG, "B release timer creation failed");
        lv_obj_delete(root);
        s_root = NULL;
        s_content = NULL;
        return;
    }
    s_b_release_timer = timer;

    s_group = group;
    s_keypad = keypad;
    s_quick_latched_key = 0;
    s_a_consumed = false;
    s_a_pending = false;
    s_a_burning = false;
    s_burn_layer = NULL;
    s_b_latched = false;
    s_back_pending = false;
    s_view = VIEW_MENU;
    s_mode = MODE_PLAIN;
    s_option_index = 0;
    s_setup_index = 0;
    s_prefs_dirty = false;
    s_pour_ticks = 0;
    s_flip_frame = -1;

    lv_obj_add_event_cb(s_root, tools_key_cb, LV_EVENT_KEY, NULL);
    lv_indev_add_event_cb(s_keypad, tools_keypad_sample_cb, LV_EVENT_KEY, s_root);
    lv_group_add_obj(s_group, s_root);
    lv_group_focus_obj(s_root);

    tools_show_view(VIEW_MENU);

    ESP_LOGI(TAG, "tools opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void tools_close(void)
{
    /* Own resources first, then hand the object tree back to
     * Navigation, which deletes the content root after this callback
     * returns (goal decision 17). */
    if (s_b_release_timer != NULL) {
        lv_timer_delete(s_b_release_timer);
        s_b_release_timer = NULL;
    }

    detail_hold_abort();
    s_a_consumed = false;

    /* Stop the sweep before the content tree goes away. */
    tools_band_stop();

    /* Every exit path restores the global indicator; close re-checks
     * so even an allocation-failure detour cannot leave it hidden. */
    xiaomiao_wifi_indicator_set_visible(true);
    if (s_keypad != NULL) {
        lv_indev_remove_event_cb_with_user_data(s_keypad,
                                                tools_keypad_sample_cb, s_root);
    }

    if (s_root != NULL && s_group != NULL) {
        lv_group_remove_obj(s_root);
    }

    s_content = NULL;
    s_root = NULL;
    s_group = NULL;
    s_keypad = NULL;
    s_title = NULL;
    s_arc = NULL;
    s_min_label = NULL;
    s_min_box = NULL;
    s_quick_arrow[0] = NULL;
    s_quick_arrow[1] = NULL;
    s_quick_latched_key = 0;
    s_quick_save_failed = false;
    s_sec_label = NULL;
    s_glass_line = NULL;
    s_shine_line[0] = NULL;
    s_shine_line[1] = NULL;
    for (uint8_t i = 0; i < 3; ++i) {
        s_up_bars[i] = NULL;
        s_pile_bars[i] = NULL;
    }
    s_fall_line = NULL;
    s_footer = NULL;
    s_opt[0] = NULL;
    s_opt[1] = NULL;
    s_opt_label[0] = NULL;
    s_opt_label[1] = NULL;
    s_b_latched = false;
    s_back_pending = false;
    s_view = VIEW_MENU;
    s_mode = MODE_PLAIN;
    s_option_index = 0;
    s_setup_index = 0;
    s_prefs_dirty = false;
    s_pour_ticks = 0;
    s_flip_frame = -1;

    ESP_LOGI(TAG, "tools closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static xiaomiao_app_t s_tools_app = {
    .id = TOOLS_APP_ID,
    .name = NULL,
    .icon = TOOLS_APP_ICON,
    .init = NULL,
    .open = tools_open,
    .close = tools_close,
};

const xiaomiao_app_t *xiaomiao_tools_app(void)
{
    /* The Launcher registers apps after the Font Service has run, so the
     * localized name is resolved on the first access. */
    s_tools_app.name = xiaomiao_text(XM_TEXT_APP_TOOLS);
    return &s_tools_app;
}
