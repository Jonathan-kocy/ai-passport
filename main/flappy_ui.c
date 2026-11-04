// main/flappy_ui.c -- the game's own LVGL screen (not the baseline demo shell).
//
// Scene: flat sky with static clouds and hills, three recycled pipe pairs,
// a scrolling ground strip, and an image-based bird rotated with its speed.
// Overlays (title, tutorial card, ready, pause, game over) are created once
// and shown or hidden when the model state changes.
#include "flappy_ui.h"

#include <stdio.h>
#include <string.h>

#include "flappy_sprite.h"
#include "flappy_text.h"
#include "lvgl.h"

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
#define C_SKY        0x86D3EE
#define C_CLOUD      0xF4FBFF
#define C_HILL_FAR   0xA7DE8C
#define C_HILL_NEAR  0x86CC6E
#define C_PIPE       0x5CBF4A
#define C_PIPE_CAP   0x6ACD55
#define C_PIPE_LIT   0x9BE37D
#define C_PIPE_EDGE  0x2D6B26
#define C_GROUND     0xE4CF8E
#define C_GROUND_DK  0xCDB070
#define C_GRASS      0x7ACC4E
#define C_GRASS_LIT  0x9BE06A
#define C_GRASS_EDGE 0x5C9A35
#define C_INK        0x3A2418
#define C_PAPER      0xFFF6E0
#define C_ACCENT     0xFF8B3D
#define C_ACCENT_DK  0xE0582A
#define C_GOLD       0xFFC93C
#define C_WHITE      0xFFFFFF
#define C_HUD_SHADOW 0x2B3A55
#define C_BATT_LOW   0xD93A3A

#define CAP_W        (FLAPPY_PIPE_W + 8)
#define CAP_H        14
#define GROUND_H     (FLAPPY_H - FLAPPY_GROUND_Y)
#define STRIPE_PERIOD 24
#define HIT_FLASH_MS 110

typedef struct {
    lv_obj_t *top_body, *top_cap, *bot_body, *bot_cap;
    int gap_y, gap_h;   // geometry currently applied (-1 = none)
    bool shown;
} pipe_view_t;

// Bird frames, ARGB8888 so LVGL's software transform can rotate them.
static uint8_t s_bird_px[FLAPPY_SPRITE_FRAMES][FLAPPY_SPRITE_W * FLAPPY_SPRITE_H * 4];
static lv_image_dsc_t s_bird_dsc[FLAPPY_SPRITE_FRAMES];

static lv_obj_t *s_scr;
static lv_obj_t *s_bird;
static pipe_view_t s_pipes[FLAPPY_PIPE_COUNT];
static lv_obj_t *s_ground_scroller;
static lv_obj_t *s_score, *s_score_shadow;
static lv_obj_t *s_batt;
static lv_obj_t *s_footer;
static lv_obj_t *s_flash;

static lv_obj_t *s_title_layer;
static lv_obj_t *s_menu_btn[FLAPPY_MENU_COUNT];
static lv_obj_t *s_menu_lbl[FLAPPY_MENU_COUNT];
static lv_obj_t *s_title_best;

static lv_obj_t *s_tut_card, *s_tut_step, *s_tut_title, *s_tut_body, *s_tut_key;
static lv_obj_t *s_tut_key_lbl;

static lv_obj_t *s_ready_card;
static lv_obj_t *s_pause_layer;
static lv_obj_t *s_over_layer, *s_over_score, *s_over_best, *s_over_badge;

static int s_frame = -1;
static int s_angle = 0x7fffffff;
static int s_score_val = -1;
static int s_battery = -2;
static uint32_t s_flash_ms;
static uint16_t s_last_attempts;
static bool s_tut_flying;

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *blob(lv_obj_t *parent, int cx, int cy, int r, uint32_t color) {
    lv_obj_t *o = box(parent, cx - r, cy - r, 2 * r, 2 * r, color);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}

// Text with a hard drop shadow (a second label offset down-right).
static lv_obj_t *shadow_label(lv_obj_t *parent, const lv_font_t *font, int y, int off,
                              uint32_t fg, uint32_t bg, const char *text, lv_obj_t **shadow_out) {
    lv_obj_t *sh = label(parent, font, bg, text);
    lv_obj_align(sh, LV_ALIGN_TOP_MID, off, y + off);
    lv_obj_t *l = label(parent, font, fg, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    if (shadow_out) *shadow_out = sh;
    return l;
}

// Retro card: cream panel, ink border, solid offset shadow.
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *drop = box(parent, x + 4, y + 4, w, h, C_INK);
    lv_obj_set_style_radius(drop, 12, 0);
    lv_obj_t *c = box(parent, x, y, w, h, C_PAPER);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(c, 3, 0);
    // Keep the drop with its card for show/hide: reparent under a holder.
    lv_obj_t *holder = lv_obj_create(parent);
    lv_obj_remove_style_all(holder);
    lv_obj_remove_flag(holder, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(holder, 0, 0);
    lv_obj_set_size(holder, FLAPPY_W, FLAPPY_H);
    lv_obj_set_parent(drop, holder);
    lv_obj_set_parent(c, holder);
    lv_obj_set_user_data(holder, c);
    return holder;
}

static lv_obj_t *card_body(lv_obj_t *holder) {
    return (lv_obj_t *)lv_obj_get_user_data(holder);
}

static lv_obj_t *layer(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_size(o, FLAPPY_W, FLAPPY_H);
    return o;
}

static void show(lv_obj_t *o, bool visible) {
    if (!o) return;
    if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void build_bird_frames(void) {
    for (int f = 0; f < FLAPPY_SPRITE_FRAMES; f++) {
        uint8_t *px = s_bird_px[f];
        for (int y = 0; y < FLAPPY_SPRITE_H; y++) {
            for (int x = 0; x < FLAPPY_SPRITE_W; x++) {
                flappy_px_t p = flappy_sprite_px(f, x, y);
                uint32_t rgb = flappy_sprite_rgb(p);
                uint8_t *d = &px[(y * FLAPPY_SPRITE_W + x) * 4];
                d[0] = (uint8_t)(rgb & 0xFF);         // B
                d[1] = (uint8_t)((rgb >> 8) & 0xFF);  // G
                d[2] = (uint8_t)((rgb >> 16) & 0xFF); // R
                d[3] = p == FLAPPY_PX_CLEAR ? 0 : 0xFF;
            }
        }
        lv_image_dsc_t *dsc = &s_bird_dsc[f];
        memset(dsc, 0, sizeof(*dsc));
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
        dsc->header.w = FLAPPY_SPRITE_W;
        dsc->header.h = FLAPPY_SPRITE_H;
        dsc->header.stride = FLAPPY_SPRITE_W * 4;
        dsc->data_size = sizeof(s_bird_px[f]);
        dsc->data = px;
    }
}

static lv_obj_t *pipe_part(lv_obj_t *parent, int w, uint32_t color) {
    lv_obj_t *o = box(parent, -100, 0, w, 10, color);
    lv_obj_set_style_border_color(o, lv_color_hex(C_PIPE_EDGE), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    // Highlight stripe gives the pipe a rounded, lit look.
    lv_obj_t *lit = box(o, 5, 0, 6, LV_PCT(100), C_PIPE_LIT);
    (void)lit;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void build_scenery(void) {
    // Clouds: clusters of overlapping circles.
    static const int16_t clouds[][3] = {
        { 40, 70, 14 }, { 58, 64, 18 }, { 76, 72, 12 },
        { 168, 116, 12 }, { 184, 108, 16 }, { 200, 116, 11 },
        { 110, 188, 10 }, { 124, 182, 13 }, { 138, 190, 9 },
    };
    for (size_t i = 0; i < sizeof(clouds) / sizeof(clouds[0]); i++) {
        blob(s_scr, clouds[i][0], clouds[i][1], clouds[i][2], C_CLOUD);
    }
    // Rolling hills peeking above the ground line.
    blob(s_scr, 30, FLAPPY_GROUND_Y + 30, 62, C_HILL_FAR);
    blob(s_scr, 200, FLAPPY_GROUND_Y + 40, 70, C_HILL_FAR);
    blob(s_scr, 120, FLAPPY_GROUND_Y + 34, 44, C_HILL_NEAR);
}

static void build_ground(void) {
    lv_obj_t *ground = box(s_scr, 0, FLAPPY_GROUND_Y, FLAPPY_W, GROUND_H, C_GROUND);
    // Children are clipped to the ground so the scroller can slide freely.
    s_ground_scroller = lv_obj_create(ground);
    lv_obj_remove_style_all(s_ground_scroller);
    lv_obj_remove_flag(s_ground_scroller, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_ground_scroller, 0, 0);
    lv_obj_set_size(s_ground_scroller, FLAPPY_W + STRIPE_PERIOD, GROUND_H);
    box(s_ground_scroller, 0, 0, FLAPPY_W + STRIPE_PERIOD, 8, C_GRASS);
    box(s_ground_scroller, 0, 8, FLAPPY_W + STRIPE_PERIOD, 2, C_GRASS_EDGE);
    for (int x = 0; x < FLAPPY_W + STRIPE_PERIOD; x += STRIPE_PERIOD) {
        box(s_ground_scroller, x + 2, 2, 8, 3, C_GRASS_LIT);            // grass tufts
        box(s_ground_scroller, x + 6, 15, 12, 4, C_GROUND_DK);          // sand ripples
        box(s_ground_scroller, x + 18, 25, 10, 3, C_GROUND_DK);
    }
}

static void build_hud(void) {
    s_score = shadow_label(s_scr, &lv_font_montserrat_48, 26, 3, C_WHITE, C_HUD_SHADOW,
                           "0", &s_score_shadow);

    s_batt = label(s_scr, &lv_font_montserrat_14, C_INK, "");
    lv_obj_set_style_bg_color(s_batt, lv_color_hex(C_WHITE), 0);
    lv_obj_set_style_bg_opa(s_batt, LV_OPA_60, 0);
    lv_obj_set_style_radius(s_batt, 8, 0);
    lv_obj_set_style_pad_hor(s_batt, 6, 0);
    lv_obj_set_style_pad_ver(s_batt, 1, 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -24, 10);

    s_footer = label(s_scr, &lv_font_montserrat_14, 0x6B4E1E, "");
    lv_obj_set_width(s_footer, 210);
    lv_obj_align(s_footer, LV_ALIGN_TOP_MID, 0, FLAPPY_GROUND_Y + 14);
}

static void build_title(void) {
    s_title_layer = layer(s_scr);
    shadow_label(s_title_layer, &lv_font_montserrat_48, 38, 3, C_WHITE, C_INK, FLAPPY_TXT_TITLE, NULL);
    lv_obj_t *ribbon = box(s_title_layer, 60, 94, 120, 20, C_GOLD);
    lv_obj_set_style_radius(ribbon, 10, 0);
    lv_obj_set_style_border_color(ribbon, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(ribbon, 2, 0);
    lv_obj_t *sub = label(ribbon, &lv_font_montserrat_14, C_INK, FLAPPY_TXT_SUBTITLE);
    lv_obj_center(sub);

    static const char *names[FLAPPY_MENU_COUNT] = { FLAPPY_TXT_PLAY, FLAPPY_TXT_TUTORIAL };
    for (int i = 0; i < FLAPPY_MENU_COUNT; i++) {
        lv_obj_t *b = box(s_title_layer, 45, 180 + i * 42, 150, 34, C_PAPER);
        lv_obj_set_style_radius(b, 17, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_INK), 0);
        lv_obj_set_style_border_width(b, 3, 0);
        s_menu_btn[i] = b;
        s_menu_lbl[i] = label(b, &lv_font_montserrat_20, C_INK, names[i]);
        lv_obj_center(s_menu_lbl[i]);
    }
    lv_obj_t *pill = box(s_title_layer, 80, 258, 80, 20, C_PAPER);
    lv_obj_set_style_radius(pill, 10, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    s_title_best = label(pill, &lv_font_montserrat_14, C_INK, "");
    lv_obj_center(s_title_best);
}

static void build_tutorial(void) {
    s_tut_card = card(s_scr, 12, 40, 210, 92);
    lv_obj_t *c = card_body(s_tut_card);
    s_tut_step = label(c, &lv_font_montserrat_14, C_ACCENT_DK, "");
    lv_obj_set_pos(s_tut_step, 12, 8);
    s_tut_title = label(c, &lv_font_montserrat_20, C_INK, "");
    lv_obj_set_pos(s_tut_title, 12, 26);
    s_tut_body = label(c, &lv_font_montserrat_14, C_INK, "");
    lv_obj_set_style_text_align(s_tut_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(s_tut_body, 184);
    lv_obj_set_pos(s_tut_body, 12, 58);

    // Key badge: which physical button this step is about.
    s_tut_key = box(c, 136, 10, 58, 30, C_ACCENT);
    lv_obj_set_style_radius(s_tut_key, 8, 0);
    lv_obj_set_style_border_color(s_tut_key, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(s_tut_key, 2, 0);
    s_tut_key_lbl = label(s_tut_key, &lv_font_montserrat_14, C_WHITE, "");
    lv_obj_center(s_tut_key_lbl);
}

static void build_ready(void) {
    s_ready_card = card(s_scr, 40, 60, 160, 62);
    lv_obj_t *c = card_body(s_ready_card);
    lv_obj_t *t = label(c, &lv_font_montserrat_20, C_ACCENT_DK, FLAPPY_TXT_READY);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *b = label(c, &lv_font_montserrat_14, C_INK, FLAPPY_TXT_READY_BODY);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 34);
}

static void build_pause(void) {
    s_pause_layer = layer(s_scr);
    lv_obj_set_style_bg_color(s_pause_layer, lv_color_hex(0x10203A), 0);
    lv_obj_set_style_bg_opa(s_pause_layer, LV_OPA_40, 0);
    lv_obj_t *holder = card(s_pause_layer, 30, 112, 180, 86);
    lv_obj_t *c = card_body(holder);
    lv_obj_t *t = label(c, &lv_font_montserrat_32, C_INK, FLAPPY_TXT_PAUSED);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *b = label(c, &lv_font_montserrat_14, C_INK, FLAPPY_TXT_PAUSED_BODY);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 52);
}

static void build_game_over(void) {
    s_over_layer = layer(s_scr);
    lv_obj_t *banner_drop = box(s_over_layer, 12, 62, 224, 44, C_INK);
    lv_obj_set_style_radius(banner_drop, 10, 0);
    lv_obj_t *banner = box(s_over_layer, 8, 58, 224, 44, C_GOLD);
    lv_obj_set_style_radius(banner, 10, 0);
    lv_obj_set_style_border_color(banner, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(banner, 3, 0);
    lv_obj_t *go = label(banner, &lv_font_montserrat_32, C_INK, FLAPPY_TXT_GAME_OVER);
    lv_obj_center(go);
    lv_obj_t *holder = card(s_over_layer, 30, 112, 180, 112);
    lv_obj_t *c = card_body(holder);
    lv_obj_t *sl = label(c, &lv_font_montserrat_14, C_ACCENT_DK, FLAPPY_TXT_SCORE);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 18, 10);
    s_over_score = label(c, &lv_font_montserrat_32, C_INK, "0");
    lv_obj_align(s_over_score, LV_ALIGN_TOP_LEFT, 18, 28);
    lv_obj_t *bl = label(c, &lv_font_montserrat_14, C_ACCENT_DK, FLAPPY_TXT_BEST);
    lv_obj_align(bl, LV_ALIGN_TOP_RIGHT, -18, 10);
    s_over_best = label(c, &lv_font_montserrat_32, C_INK, "0");
    lv_obj_align(s_over_best, LV_ALIGN_TOP_RIGHT, -18, 28);

    s_over_badge = box(c, 34, 72, 106, 24, C_GOLD);
    lv_obj_set_style_radius(s_over_badge, 12, 0);
    lv_obj_set_style_border_color(s_over_badge, lv_color_hex(C_INK), 0);
    lv_obj_set_style_border_width(s_over_badge, 2, 0);
    lv_obj_t *nb = label(s_over_badge, &lv_font_montserrat_14, C_INK, FLAPPY_TXT_NEW_BEST);
    lv_obj_center(nb);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void flappy_ui_create(const flappy_model_t *m) {
    build_bird_frames();

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_SKY), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);

    build_scenery();
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        pipe_view_t *v = &s_pipes[i];
        v->top_body = pipe_part(s_scr, FLAPPY_PIPE_W, C_PIPE);
        v->bot_body = pipe_part(s_scr, FLAPPY_PIPE_W, C_PIPE);
        v->top_cap = pipe_part(s_scr, CAP_W, C_PIPE_CAP);
        v->bot_cap = pipe_part(s_scr, CAP_W, C_PIPE_CAP);
        lv_obj_set_style_radius(v->top_cap, 3, 0);
        lv_obj_set_style_radius(v->bot_cap, 3, 0);
        v->gap_y = v->gap_h = -1;
        v->shown = false;
    }
    build_ground();

    s_bird = lv_image_create(s_scr);
    lv_image_set_src(s_bird, &s_bird_dsc[1]);
    lv_image_set_pivot(s_bird, FLAPPY_SPRITE_W / 2, FLAPPY_SPRITE_H / 2);

    build_hud();
    build_title();
    build_tutorial();
    build_ready();
    build_pause();
    build_game_over();

    s_flash = layer(s_scr);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(C_WHITE), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_70, 0);
    show(s_flash, false);

    flappy_ui_sync(m, FLAPPY_EV_STATE, 0);
    lv_screen_load(s_scr);
}

static void sync_pipes(const flappy_model_t *m) {
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        const flappy_pipe_t *p = &m->pipes[i];
        pipe_view_t *v = &s_pipes[i];
        const int x = flappy_pipe_x(p);
        const bool visible = p->active && x < FLAPPY_W && x + CAP_W > -4;
        if (visible != v->shown) {
            show(v->top_body, visible);
            show(v->top_cap, visible);
            show(v->bot_body, visible);
            show(v->bot_cap, visible);
            v->shown = visible;
        }
        if (!visible) continue;
        const int gap_top = p->gap_y - p->gap_h / 2;
        const int gap_bot = p->gap_y + p->gap_h / 2;
        if (v->gap_y != p->gap_y || v->gap_h != p->gap_h) {
            // Bodies overlap their caps by 2 px so the borders read as one pipe.
            lv_obj_set_height(v->top_body, gap_top - CAP_H + 6);
            lv_obj_set_height(v->top_cap, CAP_H);
            lv_obj_set_height(v->bot_cap, CAP_H);
            lv_obj_set_height(v->bot_body, FLAPPY_GROUND_Y - (gap_bot + CAP_H) + 4);
            v->gap_y = p->gap_y;
            v->gap_h = p->gap_h;
        }
        lv_obj_set_pos(v->top_body, x, -4);
        lv_obj_set_pos(v->top_cap, x - 4, gap_top - CAP_H);
        lv_obj_set_pos(v->bot_cap, x - 4, gap_bot);
        lv_obj_set_pos(v->bot_body, x, gap_bot + CAP_H - 2);
    }
}

static void sync_bird(const flappy_model_t *m) {
    const int frame = flappy_bird_frame(m);
    if (frame != s_frame) {
        lv_image_set_src(s_bird, &s_bird_dsc[frame]);
        s_frame = frame;
    }
    const int angle = flappy_bird_angle_deci(m);
    if (angle != s_angle) {
        lv_image_set_rotation(s_bird, angle);
        s_angle = angle;
    }
    lv_obj_set_pos(s_bird, FLAPPY_BIRD_X - FLAPPY_SPRITE_W / 2,
                   flappy_bird_y(m) - FLAPPY_SPRITE_H / 2);
}

static void sync_score(const flappy_model_t *m) {
    if ((int)m->score == s_score_val) return;
    s_score_val = m->score;
    lv_label_set_text_fmt(s_score, "%u", (unsigned)m->score);
    lv_label_set_text_fmt(s_score_shadow, "%u", (unsigned)m->score);
}

static const char *tutorial_key_name(flappy_tutorial_step_t step) {
    switch (step) {
    case FLAPPY_TUT_FLAP:
    case FLAPPY_TUT_GATE: return "UP";
    case FLAPPY_TUT_DIVE: return "DOWN";
    case FLAPPY_TUT_DONE: return NULL;
    default:              return "OK";
    }
}

static void sync_tutorial(const flappy_model_t *m) {
    const flappy_tutorial_text_t *t = &FLAPPY_TUTORIAL_TEXT[m->tut_step];
    const int n = (int)m->tut_step;   // FLAP..GATE are steps 1..5
    if (n >= 1 && n <= FLAPPY_TUTORIAL_NUMBERED_STEPS) {
        lv_label_set_text_fmt(s_tut_step, FLAPPY_TXT_STEP_FMT, n, FLAPPY_TUTORIAL_NUMBERED_STEPS);
    } else {
        lv_label_set_text(s_tut_step, m->tut_step == FLAPPY_TUT_DONE ? "TUTORIAL" : "HOW TO PLAY");
    }
    lv_label_set_text(s_tut_title, t->title);
    lv_label_set_text(s_tut_body, t->body);
    const char *key = tutorial_key_name(m->tut_step);
    show(s_tut_key, key != NULL);
    if (key) lv_label_set_text(s_tut_key_lbl, key);
    lv_label_set_text(s_footer, t->hint);
    s_last_attempts = m->tut_attempts;
    s_tut_flying = m->tut_flying;
}

static void sync_title(const flappy_model_t *m) {
    for (int i = 0; i < FLAPPY_MENU_COUNT; i++) {
        const bool sel = (int)m->menu_sel == i;
        lv_obj_set_style_bg_color(s_menu_btn[i], lv_color_hex(sel ? C_ACCENT : C_PAPER), 0);
        lv_obj_set_style_text_color(s_menu_lbl[i], lv_color_hex(sel ? C_WHITE : C_INK), 0);
    }
    lv_label_set_text_fmt(s_title_best, FLAPPY_TXT_BEST_FMT, (unsigned)m->best);
}

static void sync_overlays(const flappy_model_t *m) {
    const flappy_state_t s = m->state;
    show(s_title_layer, s == FLAPPY_TITLE);
    show(s_tut_card, s == FLAPPY_TUTORIAL);
    show(s_ready_card, s == FLAPPY_READY);
    show(s_pause_layer, s == FLAPPY_PAUSED);
    show(s_over_layer, s == FLAPPY_GAME_OVER);
    const bool hud = s == FLAPPY_PLAYING || s == FLAPPY_PAUSED || s == FLAPPY_DYING;
    show(s_score, hud);
    show(s_score_shadow, hud);

    switch (s) {
    case FLAPPY_TITLE:
        sync_title(m);
        lv_label_set_text(s_footer, FLAPPY_TXT_TITLE_HINT);
        break;
    case FLAPPY_TUTORIAL:
        sync_tutorial(m);
        break;
    case FLAPPY_READY:
        lv_label_set_text(s_footer, FLAPPY_TXT_READY_HINT);
        break;
    case FLAPPY_PAUSED:
        lv_label_set_text(s_footer, FLAPPY_TXT_PAUSED_HINT);
        break;
    case FLAPPY_GAME_OVER:
        lv_label_set_text_fmt(s_over_score, "%u", (unsigned)m->score);
        lv_label_set_text_fmt(s_over_best, "%u", (unsigned)m->best);
        show(s_over_badge, m->new_best);
        lv_label_set_text(s_footer, FLAPPY_TXT_OVER_HINT);
        break;
    default:
        lv_label_set_text(s_footer, "");
        break;
    }
}

void flappy_ui_sync(const flappy_model_t *m, uint32_t events, uint32_t elapsed_ms) {
    if (!s_scr) return;
    if (events & FLAPPY_EV_STATE) sync_overlays(m);
    if (m->state == FLAPPY_TUTORIAL && m->tut_flying != s_tut_flying) {
        // Practice gate: hide the card while flying so the top pipe is visible.
        s_tut_flying = m->tut_flying;
        const bool gate = m->tut_step == FLAPPY_TUT_GATE;
        show(s_tut_card, !(gate && s_tut_flying));
        lv_label_set_text(s_footer, gate && s_tut_flying ? FLAPPY_TXT_GATE_FLYING
                                                         : FLAPPY_TUTORIAL_TEXT[m->tut_step].hint);
    }
    if (m->state == FLAPPY_TUTORIAL && m->tut_attempts != s_last_attempts) {
        char buf[32];
        snprintf(buf, sizeof(buf), FLAPPY_TXT_RETRY_FMT, (unsigned)m->tut_attempts);
        lv_label_set_text(s_footer, buf);
        s_last_attempts = m->tut_attempts;
    }

    sync_pipes(m);
    sync_bird(m);
    sync_score(m);
    lv_obj_set_x(s_ground_scroller, -flappy_ground_offset(m, STRIPE_PERIOD));

    if (events & FLAPPY_EV_HIT) {
        s_flash_ms = HIT_FLASH_MS;
        show(s_flash, true);
    } else if (s_flash_ms) {
        s_flash_ms = elapsed_ms >= s_flash_ms ? 0 : s_flash_ms - elapsed_ms;
        if (!s_flash_ms) show(s_flash, false);
    }
}

void flappy_ui_set_battery(int soc) {
    if (!s_batt || soc == s_battery) return;
    s_battery = soc;
    if (soc < 0) {
        // Unavailable: show the empty symbol only, never a made-up number.
        lv_label_set_text(s_batt, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(s_batt, lv_color_hex(0x7A8A99), 0);
        return;
    }
    const char *sym = soc >= 80 ? LV_SYMBOL_BATTERY_FULL
                    : soc >= 55 ? LV_SYMBOL_BATTERY_3
                    : soc >= 30 ? LV_SYMBOL_BATTERY_2
                    : soc >= 10 ? LV_SYMBOL_BATTERY_1
                    : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(s_batt, "%s %d%%", sym, soc);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(soc < 15 ? C_BATT_LOW : C_INK), 0);
}
