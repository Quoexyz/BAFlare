#include "platform_win.h"

#ifdef _WIN32

#include <uxtheme.h>
#include <dwmapi.h>

/* 辅助函数：从注册表读取 DWORD */
static DWORD reg_get_dword(HKEY hKey, const wchar_t *value_name, DWORD default_val) {
    DWORD val = default_val;
    DWORD size = sizeof(val);
    DWORD type = REG_DWORD;

    if (RegQueryValueExW(hKey, value_name, NULL, &type, (LPBYTE)&val, &size) != ERROR_SUCCESS || type != REG_DWORD) {
        val = default_val;
    }
    return val;
}

/*
 * 从注册表读取配置
 * 路径：HKEY_CURRENT_USER\Software\SparkCursorEffect
 * 键值：ColorR, ColorG, ColorB (DWORD), Scale, Opacity, Speed
 */
int load_registry_config(MouseSpark *spark) {
    HKEY hKey;
    const wchar_t *subkey = L"Software\\SparkCursorEffect";
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey);

    if (result != ERROR_SUCCESS) {
        MessageBoxW(NULL,
            L"Failed to read registry config.\nPlease run the Settings UI first.",
            L"Spark Effect Error",
            MB_OK | MB_ICONERROR);
        return 0;
    }

    /* 读取 RGB 颜色值，默认蓝色 (45, 175, 255) */
    DWORD r = reg_get_dword(hKey, L"ColorR", 45);
    DWORD g = reg_get_dword(hKey, L"ColorG", 175);
    DWORD b = reg_get_dword(hKey, L"ColorB", 255);

    // 确保值在有效范围内 (0-255)
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    apply_color(spark, (int)r, (int)g, (int)b);

    /* 读取大小 (存为 x10 整数，例如 15 = 1.5) */
    DWORD scale_val = reg_get_dword(hKey, L"Scale", 15);
    spark->scale = (float)scale_val / 10.0f;
    if (spark->scale < 0.5f) spark->scale = 0.5f;
    if (spark->scale > 5.0f) spark->scale = 5.0f;

    /* 读取透明度 (存为 x100 整数，例如 80 = 0.8) */
    DWORD opacity_val = reg_get_dword(hKey, L"Opacity", 100);
    spark->opacity = (float)opacity_val / 100.0f;
    if (spark->opacity < 0.1f) spark->opacity = 0.1f;
    if (spark->opacity > 1.0f) spark->opacity = 1.0f;

    /* 读取速度 (存为 x10 整数) */
    DWORD speed_val = reg_get_dword(hKey, L"Speed", 10);
    spark->speed = (float)speed_val / 10.0f;
    if (spark->speed < 0.2f) spark->speed = 0.2f;
    if (spark->speed > 2.0f) spark->speed = 2.0f;

    RegCloseKey(hKey);
    return 1;
}

/* ---------- Platform Setup ---------- */
void setup_platform_window(SDL_Window *window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) {
        HWND hwnd = info.info.win.window;

        LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE,
                      ex_style | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW);

        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
}

#endif /* _WIN32 */
