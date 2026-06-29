#include "lvgl.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include <stdbool.h>
#include <stdint.h>

#if LV_USE_SDL
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#endif

#ifndef lv_screen
#define lv_screen() lv_scr_act()
#endif

static inline uint8_t color_get_r8(lv_color_t c) {
    uint32_t v = lv_color_to_u32(c);
    return (v >> 16) & 0xFF;
}
static inline uint8_t color_get_g8(lv_color_t c) {
    uint32_t v = lv_color_to_u32(c);
    return (v >> 8) & 0xFF;
}
static inline uint8_t color_get_b8(lv_color_t c) {
    uint32_t v = lv_color_to_u32(c);
    return v & 0xFF;
}

/* ================== 常量 ================== */
#define WIN_W         420
#define WIN_H         650
#define CUSTOM_INDEX  8
#define REG_PATH      L"Software\\SparkCursorEffect"
#define RUN_REG_PATH  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define AUTOSTART_KEY L"BAFlare"
#define EXE_NAME      L"BAFlare.exe"

/* ================== 浅色调色板 ================== */
#define C_BG         lv_color_hex(0xF5F5F5)
#define C_CARD       lv_color_hex(0xFFFFFF)
#define C_CARD_SEL   lv_color_hex(0xE3F2FD)
#define C_ACCENT     lv_color_hex(0x2196F3)
#define C_ACCENT_DK  lv_color_hex(0x1976D2)
#define C_TEXT       lv_color_hex(0x212121)
#define C_TEXT_DIM   lv_color_hex(0x757575)
#define C_SUCCESS    lv_color_hex(0x43A047)
#define C_DANGER     lv_color_hex(0xE53935)
#define C_WARN       lv_color_hex(0xFB8C00)
#define C_BORDER     lv_color_hex(0xE0E0E0)
#define C_BTN_RESET  lv_color_hex(0x9E9E9E)

/* ================== 颜色预设 ================== */
typedef struct { const char *name; uint8_t r, g, b; } preset_t;
static const preset_t presets[] = {
    {"Blue",    45, 175, 255}, {"Red",    255, 69,  69 },
    {"Green",   69, 255, 137}, {"Yellow", 255, 223, 69 },
    {"Purple", 195, 69,  255}, {"Cyan",     0, 255, 255},
    {"Orange",255, 140,   0}, {"Pink",   255, 105, 180},
};
#define PRESET_COUNT ((int)(sizeof(presets) / sizeof(presets[0])))

/* ================== 应用状态 ================== */
typedef struct {
    int      sel_color;
    uint8_t  cr, cg, cb;
    int      scale10;
    int      opacity100;
    int      speed10;
    bool     autostart;
} state_t;

static state_t st = {
    .sel_color = 0,
    .cr = 255, .cg = 255, .cb = 255,
    .scale10 = 15, .opacity100 = 100, .speed10 = 10,
    .autostart = false,
};

/* ================== UI 句柄 ================== */
typedef struct {
    lv_obj_t *cards[9];
    lv_obj_t *custom_swatch;
    lv_obj_t *scale_slider,    *scale_val;
    lv_obj_t *opacity_slider,  *opacity_val;
    lv_obj_t *speed_slider,    *speed_val;
    lv_obj_t *autostart_sw,    *autostart_lbl;
    lv_obj_t *status_lbl;
} ui_t;
static ui_t ui;

static void build_ui(void);
static void sync_ui(void);
static void select_color(int idx);
static void set_status(const char *txt, lv_color_t c);
static void open_picker(void);

/* ================== 注册表辅助 ================== */
static bool reg_get_dword(const wchar_t *path, const wchar_t *name, DWORD *out)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    DWORD sz = sizeof(DWORD);
    bool ok = (RegQueryValueExW(k, name, NULL, NULL, (LPBYTE)out, &sz) == ERROR_SUCCESS);
    RegCloseKey(k);
    return ok;
}
static bool reg_set_dword(const wchar_t *path, const wchar_t *name, DWORD v)
{
    HKEY k; DWORD disp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, &disp) != ERROR_SUCCESS)
        return false;
    bool ok = (RegSetValueExW(k, name, 0, REG_DWORD, (LPBYTE)&v, sizeof(v)) == ERROR_SUCCESS);
    RegCloseKey(k);
    return ok;
}

/* ================== 配置读写 ================== */
static void load_config(void)
{
    DWORD r, g, b, s, o, sp, a;
    if (reg_get_dword(REG_PATH, L"ColorR", &r) &&
        reg_get_dword(REG_PATH, L"ColorG", &g) &&
        reg_get_dword(REG_PATH, L"ColorB", &b)) {
        bool match = false;
        for (int i = 0; i < PRESET_COUNT; i++) {
            if (presets[i].r == (uint8_t)r && presets[i].g == (uint8_t)g &&
                presets[i].b == (uint8_t)b) {
                st.sel_color = i; match = true; break;
            }
        }
        if (!match) {
            st.cr = (uint8_t)r; st.cg = (uint8_t)g; st.cb = (uint8_t)b;
            st.sel_color = CUSTOM_INDEX;
        }
    }
    if (reg_get_dword(REG_PATH, L"Scale",   &s))  st.scale10    = (int)s;
    if (reg_get_dword(REG_PATH, L"Opacity", &o))  st.opacity100 = (int)o;
    if (reg_get_dword(REG_PATH, L"Speed",   &sp)) st.speed10    = (int)sp;
    if (reg_get_dword(REG_PATH, L"AutoStart", &a)) st.autostart = (a == 1);
}

static bool save_config(void)
{
    uint8_t r, g, b;
    if (st.sel_color == CUSTOM_INDEX) { r = st.cr; g = st.cg; b = st.cb; }
    else { r = presets[st.sel_color].r; g = presets[st.sel_color].g; b = presets[st.sel_color].b; }

    bool ok = true;
    ok &= reg_set_dword(REG_PATH, L"ColorR",   r);
    ok &= reg_set_dword(REG_PATH, L"ColorG",   g);
    ok &= reg_set_dword(REG_PATH, L"ColorB",   b);
    ok &= reg_set_dword(REG_PATH, L"Scale",    (DWORD)st.scale10);
    ok &= reg_set_dword(REG_PATH, L"Opacity",  (DWORD)st.opacity100);
    ok &= reg_set_dword(REG_PATH, L"Speed",    (DWORD)st.speed10);
    ok &= reg_set_dword(REG_PATH, L"AutoStart", st.autostart ? 1 : 0);
    return ok;
}

/* ================== 自启管理 ================== */
static void get_exe_path(wchar_t *buf, size_t cap)
{
    GetModuleFileNameW(NULL, buf, (DWORD)cap);
    wchar_t *p = wcsrchr(buf, L'\\');
    if (p) { *(p + 1) = 0; wcscat_s(buf, cap, EXE_NAME); }
}

static bool set_autostart(bool en)
{
    if (en) {
        wchar_t p[MAX_PATH]; get_exe_path(p, MAX_PATH);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return false;
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_REG_PATH, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
            return false;
        bool ok = (RegSetValueExW(k, AUTOSTART_KEY, 0, REG_SZ, (const BYTE *)p,
                                  ((DWORD)wcslen(p) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS);
        RegCloseKey(k);
        return ok;
    } else {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_REG_PATH, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
            return true;
        RegDeleteValueW(k, AUTOSTART_KEY);
        RegCloseKey(k);
        return true;
    }
}

/* ================== 终止 BAFlare.exe ================== */
static bool kill_baflare(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, EXE_NAME) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); found = true; }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* ================== 状态栏 ================== */
static void set_status(const char *txt, lv_color_t c)
{
    lv_label_set_text(ui.status_lbl, txt);
    lv_obj_set_style_text_color(ui.status_lbl, c, 0);
}

/* ================== 颜色选择 ================== */
static void card_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == CUSTOM_INDEX) open_picker();
    else                     select_color(idx);
}

static void select_color(int idx)
{
    st.sel_color = idx;
    for (int i = 0; i < 9; i++) {
        if (!ui.cards[i]) continue;
        if (i == idx) {
            lv_obj_set_style_border_color(ui.cards[i], C_ACCENT,   0);
            lv_obj_set_style_border_width(ui.cards[i], 2,          0);
            lv_obj_set_style_bg_color     (ui.cards[i], C_CARD_SEL, 0);
        } else {
            lv_obj_set_style_border_color(ui.cards[i], C_BORDER, 0);
            lv_obj_set_style_border_width(ui.cards[i], 1,        0);
            lv_obj_set_style_bg_color     (ui.cards[i], C_CARD,  0);
        }
    }
}

/* ================== Windows 原生颜色选择器 ================== */
static void open_picker(void)
{
    CHOOSECOLORW cc;
    static DWORD rgbCurrents[16];

    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = GetActiveWindow();
    cc.lpCustColors = (LPDWORD)rgbCurrents;
    cc.rgbResult = RGB(st.cr, st.cg, st.cb);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        st.cr = GetRValue(cc.rgbResult);
        st.cg = GetGValue(cc.rgbResult);
        st.cb = GetBValue(cc.rgbResult);

        lv_obj_set_style_bg_color(ui.custom_swatch, lv_color_make(st.cr, st.cg, st.cb), 0);
        select_color(CUSTOM_INDEX);

        char buf[64];
        lv_snprintf(buf, sizeof(buf), "Custom color: #%02X%02X%02X", st.cr, st.cg, st.cb);
        set_status(buf, C_SUCCESS);
    } else {
        set_status("Color selection canceled", C_TEXT_DIM);
    }
}

/* ================== 滑块回调 ================== */
static void scale_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    st.scale10 = v;
    char b[16]; lv_snprintf(b, sizeof(b), "%d.%dx", v / 10, v % 10);
    lv_label_set_text(ui.scale_val, b);
}
static void opacity_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    st.opacity100 = v;
    char b[16]; lv_snprintf(b, sizeof(b), "%d%%", v);
    lv_label_set_text(ui.opacity_val, b);
}
static void speed_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    st.speed10 = v;
    char b[16]; lv_snprintf(b, sizeof(b), "%d.%dx", v / 10, v % 10);
    lv_label_set_text(ui.speed_val, b);
}

/* ================== 按钮回调 ================== */
static void autostart_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (on) {
        wchar_t p[MAX_PATH]; get_exe_path(p, MAX_PATH);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
            st.autostart = false;
            set_status("BAFlare.exe not found, cannot enable auto-start", C_WARN);
            return;
        }
    }
    st.autostart = on;
    if (set_autostart(on)) {
        if (on) {
            lv_label_set_text(ui.autostart_lbl, "(enabled)");
            lv_obj_set_style_text_color(ui.autostart_lbl, C_SUCCESS, 0);
            set_status("Auto-start enabled", C_SUCCESS);
        } else {
            lv_label_set_text(ui.autostart_lbl, "(disabled)");
            lv_obj_set_style_text_color(ui.autostart_lbl, C_TEXT_DIM, 0);
            set_status("Auto-start disabled", C_TEXT);
        }
    }
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    if (kill_baflare()) set_status("BAFlare.exe terminated", C_SUCCESS);
    else                set_status("BAFlare.exe is not running", C_TEXT_DIM);
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    st.cr = 255; st.cg = 255; st.cb = 255;
    lv_obj_set_style_bg_color(ui.custom_swatch, lv_color_white(), 0);

    st.scale10 = 15; st.opacity100 = 100; st.speed10 = 10;
    lv_slider_set_value(ui.scale_slider,   15,  LV_ANIM_OFF);
    lv_slider_set_value(ui.opacity_slider, 100, LV_ANIM_OFF);
    lv_slider_set_value(ui.speed_slider,   10,  LV_ANIM_OFF);
    lv_obj_send_event(ui.scale_slider,   LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(ui.opacity_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(ui.speed_slider,   LV_EVENT_VALUE_CHANGED, NULL);

    st.autostart = false;
    lv_obj_remove_state(ui.autostart_sw, LV_STATE_CHECKED);
    set_autostart(false);
    lv_label_set_text(ui.autostart_lbl, "(disabled)");
    lv_obj_set_style_text_color(ui.autostart_lbl, C_TEXT_DIM, 0);

    select_color(0);
    set_status("Reset to defaults", C_TEXT);
}

static void launch_cb(lv_event_t *e)
{
    (void)e;
    save_config();
    wchar_t p[MAX_PATH]; get_exe_path(p, MAX_PATH);
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
        HINSTANCE h = ShellExecuteW(NULL, L"open", p, NULL, NULL, SW_SHOWNORMAL);
        if ((intptr_t)h > 32) set_status("Effect launched!", C_SUCCESS);
        else                  set_status("Failed to launch effect", C_DANGER);
    } else {
        set_status("BAFlare.exe not found", C_WARN);
    }
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    if (save_config()) set_status("Settings saved!", C_SUCCESS);
    else               set_status("Failed to save settings", C_DANGER);
}

/* ================== UI 构造 ================== */
static lv_obj_t *make_card(lv_obj_t *parent, int idx,
                           const char *name, lv_color_t c)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_shadow_width(card, 2, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
    lv_obj_set_style_pad_all(card, 2, 0); /* Reduced padding */
    lv_obj_set_style_pad_row(card, 2, 0); /* Reduced row pad */
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_layout(card, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *sw = lv_obj_create(card);
    lv_obj_set_size(sw, 24, 24); /* Reduced swatch size */
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sw, c, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw, 0, 0);
    lv_obj_set_style_shadow_width(sw, 2, 0); /* Smaller shadow */
    lv_obj_set_style_shadow_color(sw, c, 0);
    lv_obj_set_style_shadow_opa(sw, LV_OPA_30, 0);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0); /* Smaller font for label */

    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)idx);

    ui.cards[idx] = card;
    if (idx == CUSTOM_INDEX) ui.custom_swatch = sw;
    return card;
}

static lv_obj_t *make_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *sec = lv_obj_create(parent);
    lv_obj_set_width(sec, LV_PCT(100));
    lv_obj_set_height(sec, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(sec, C_CARD, 0);
    lv_obj_set_style_bg_opa(sec, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sec, 10, 0); /* Slightly smaller radius */
    lv_obj_set_style_border_width(sec, 1, 0);
    lv_obj_set_style_border_color(sec, C_BORDER, 0);
    lv_obj_set_style_pad_all(sec, 10, 0); /* Reduced padding from 14 */
    lv_obj_set_style_pad_row(sec, 8, 0);  /* Reduced row padding */
    lv_obj_set_style_layout(sec, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(sec, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(sec);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, C_ACCENT, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0); /* Smaller font */
    return sec;
}

typedef struct {
    const char  *title;
    int          min_v, max_v, default_v;
    lv_obj_t   **slider;
    lv_obj_t   **value;
    lv_event_cb_t cb;
    const char  *initial_text;
} slider_cfg_t;

static lv_obj_t *make_slider_row(lv_obj_t *parent, const slider_cfg_t *cfg)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_row(row, 2, 0); /* Tighter */
    lv_obj_set_style_layout(row, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(row);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_style_layout(head, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(head);
    lv_label_set_text(tl, cfg->title);
    lv_obj_set_style_text_color(tl, C_TEXT, 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0); /* Smaller font */

    lv_obj_t *vl = lv_label_create(head);
    lv_label_set_text(vl, cfg->initial_text);
    lv_obj_set_style_text_color(vl, C_ACCENT_DK, 0);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_12, 0); /* Smaller font */
    *(cfg->value) = vl;

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_width(sl, LV_PCT(100));
    lv_slider_set_range(sl, cfg->min_v, cfg->max_v);
    lv_slider_set_value(sl, cfg->default_v, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(sl, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_add_event_cb(sl, cfg->cb, LV_EVENT_VALUE_CHANGED, NULL);
    *(cfg->slider) = sl;

    return row;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *main = lv_obj_create(scr);
    lv_obj_set_size(main, WIN_W, WIN_H);
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 10, 0); /* Reduced from 18 */
    lv_obj_set_style_pad_row(main, 8, 0);  /* Reduced from 12 */
    lv_obj_set_style_pad_bottom(main, 6, 0); /* Reduced from 28 */
    lv_obj_set_style_layout(main, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(main, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(main);
    lv_label_set_text(title, "BA Flare Settings");
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0); /* Reduced from 24 */

    lv_obj_t *sub = lv_label_create(main);
    lv_label_set_text(sub, "Save settings to registry.");
    lv_obj_set_style_text_color(sub, C_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0); /* Smaller hint text */

    lv_obj_t *csec = make_section(main, "Color Scheme");

    lv_obj_t *grid = lv_obj_create(csec);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, 6, 0); /* Reduced from 8 */
    lv_obj_set_style_pad_row(grid, 6, 0);    /* Reduced from 8 */
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static int32_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    /* Reduced row height from 92 to 68 */
    static int32_t rows[] = { 68, 68, 68, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_layout(grid, LV_LAYOUT_GRID, 0);

    for (int i = 0; i < PRESET_COUNT; i++) {
        lv_color_t c = lv_color_make(presets[i].r, presets[i].g, presets[i].b);
        lv_obj_t *card = make_card(grid, i, presets[i].name, c);
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, i % 3, 1,
                                  LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }
    lv_obj_t *cc = make_card(grid, CUSTOM_INDEX, "Custom",
                             lv_color_make(st.cr, st.cg, st.cb));
    lv_obj_set_grid_cell(cc, LV_GRID_ALIGN_STRETCH, 2, 1,
                              LV_GRID_ALIGN_STRETCH, 2, 1);

    lv_obj_t *psec = make_section(main, "Parameters");
    slider_cfg_t s1 = {"Particle Size",   5, 50,  15,
                       &ui.scale_slider,   &ui.scale_val,   scale_cb,   "1.5x"};
    slider_cfg_t s2 = {"Opacity",        10, 100, 100,
                       &ui.opacity_slider, &ui.opacity_val, opacity_cb, "100%"};
    slider_cfg_t s3 = {"Animation Speed", 2, 20,  10,
                       &ui.speed_slider,   &ui.speed_val,   speed_cb,   "1.0x"};
    make_slider_row(psec, &s1);
    make_slider_row(psec, &s2);
    make_slider_row(psec, &s3);

    lv_obj_t *asec = make_section(main, "Quick Actions");

    lv_obj_t *r1 = lv_obj_create(asec);
    lv_obj_set_width(r1, LV_PCT(100));
    lv_obj_set_height(r1, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    lv_obj_set_style_layout(r1, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(r1);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_column(left, 6, 0); /* Reduced spacing */
    lv_obj_set_style_layout(left, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_CENTER,
                                LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sw = lv_switch_create(left);
    lv_obj_set_style_bg_color(sw, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_event_cb(sw, autostart_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui.autostart_sw = sw;

    lv_obj_t *sw_label = lv_label_create(left);
    lv_label_set_text(sw_label, "Auto-start");
    lv_obj_set_style_text_color(sw_label, C_TEXT, 0);
    lv_obj_set_style_text_font(sw_label, &lv_font_montserrat_12, 0);

    ui.autostart_lbl = lv_label_create(left);
    lv_label_set_text(ui.autostart_lbl, "(disabled)");
    lv_obj_set_style_text_color(ui.autostart_lbl, C_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ui.autostart_lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *stop_btn = lv_button_create(r1);
    lv_obj_set_size(stop_btn, 100, 32); /* Reduced size */
    lv_obj_set_style_bg_color(stop_btn, C_DANGER, 0);
    lv_obj_set_style_radius(stop_btn, 6, 0);
    lv_obj_t *sb_lbl = lv_label_create(stop_btn);
    lv_label_set_text(sb_lbl, LV_SYMBOL_STOP "  Stop");
    lv_obj_set_style_text_color(sb_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sb_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(sb_lbl);
    lv_obj_add_event_cb(stop_btn, stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *r2 = lv_obj_create(asec);
    lv_obj_set_width(r2, LV_PCT(100));
    lv_obj_set_height(r2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2, 0, 0);
    lv_obj_set_style_pad_all(r2, 0, 0);
    lv_obj_set_style_pad_column(r2, 4, 0); /* Reduced gap */
    lv_obj_set_style_layout(r2, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r2, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r2, LV_OBJ_FLAG_SCROLLABLE);

    const struct { const char *txt; lv_color_t c; lv_event_cb_t cb; } btns[] = {
        { LV_SYMBOL_REFRESH "  Reset",   C_BTN_RESET, reset_cb  },
        { LV_SYMBOL_PLAY    "  Launch",  C_SUCCESS,   launch_cb },
        { LV_SYMBOL_SAVE    "  Save",    C_ACCENT,    save_cb   },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(r2);
        lv_obj_set_height(b, 32); /* Reduced height */
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, btns[i].c, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, btns[i].txt);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);

        /* 修复：使用 lv_color_eq 比较颜色结构体 */
        if (lv_color_eq(btns[i].c, C_BTN_RESET)) {
             lv_obj_set_style_text_color(l, C_TEXT, 0);
        } else {
             lv_obj_set_style_text_color(l, lv_color_white(), 0);
        }

        lv_obj_center(l);
        lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
    }

    ui.status_lbl = lv_label_create(main);
    lv_label_set_text(ui.status_lbl, LV_SYMBOL_BULLET "  Ready");
    lv_obj_set_style_text_color(ui.status_lbl, C_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ui.status_lbl, &lv_font_montserrat_12, 0);
}

/* ================== UI 同步状态 ================== */
static void sync_ui(void)
{
    select_color(st.sel_color);
    lv_obj_set_style_bg_color(ui.custom_swatch,
                              lv_color_make(st.cr, st.cg, st.cb), 0);

    lv_slider_set_value(ui.scale_slider,   st.scale10,    LV_ANIM_OFF);
    lv_slider_set_value(ui.opacity_slider, st.opacity100, LV_ANIM_OFF);
    lv_slider_set_value(ui.speed_slider,   st.speed10,    LV_ANIM_OFF);
    lv_obj_send_event(ui.scale_slider,   LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(ui.opacity_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(ui.speed_slider,   LV_EVENT_VALUE_CHANGED, NULL);

    if (st.autostart) {
        lv_obj_add_state(ui.autostart_sw, LV_STATE_CHECKED);
        lv_label_set_text(ui.autostart_lbl, "(enabled)");
        lv_obj_set_style_text_color(ui.autostart_lbl, C_SUCCESS, 0);
    } else {
        lv_obj_remove_state(ui.autostart_sw, LV_STATE_CHECKED);
        lv_label_set_text(ui.autostart_lbl, "(disabled)");
        lv_obj_set_style_text_color(ui.autostart_lbl, C_TEXT_DIM, 0);
    }
}

/* ================== 主入口 ================== */
int SDL_main(int argc, char **argv)
{
    (void)argc; (void)argv;

    lv_init();
#if LV_USE_SDL
    lv_sdl_window_create(WIN_W, WIN_H);
    lv_sdl_mouse_create();
#endif

    load_config();
    build_ui();
    sync_ui();

    for (;;) {
        uint32_t ms = lv_timer_handler();
        Sleep(ms < 5 ? 5 : ms);
    }
    return 0;
}
