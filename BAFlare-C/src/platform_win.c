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

        /*
         * 必须自己补一次置顶。
         * SDL 2.32 在创建窗口时并不应用 SDL_WINDOW_ALWAYS_ON_TOP，
         * 它只在收到 SDL_WINDOWEVENT_ENTER（窗口获得鼠标焦点）时才调用
         * SetWindowPos(HWND_TOPMOST)：SDL_windowswindow.c 的 WIN_OnWindowEnter。
         * 而本窗口被加了 WS_EX_TRANSPARENT 之后，MSDN 规定鼠标事件会转发给下层窗口，
         * 也就是窗口从此收不到 WM_MOUSEMOVE，那个事件永远不会到达，置顶就永远补不上。
         * 同时 MSDN 要求：用 SetWindowLong 改过 GWL_EXSTYLE 后，必须调用
         * SetWindowPos(SWP_FRAMECHANGED) 让新样式生效，这一步也一起做掉。
         */
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

/* ---------- 鼠标输入：低层钩子（事件驱动，替代逐帧轮询） ---------- */
/*
 * 为什么不用逐帧采样：
 * SDL_GetGlobalMouseState() 在 Windows 上就是 GetCursorPos + GetAsyncKeyState，
 * 只能反映"调用那一刻"的状态。空闲时主循环 20 FPS，采样间隔 50ms，一次 30ms 的
 * 快速点击可能整段落在两次采样之间，按下这个"边沿"就丢了，那一下完全没有特效。
 * WH_MOUSE_LL 由系统在分发鼠标事件时回调，按下/抬起/移动一个都不会漏。
 *
 * 线程要求（MSDN）：低层钩子的回调运行在"安装钩子的那个线程"上，靠系统向该线程投递
 * 消息来触发，所以该线程必须有消息循环，且回调必须很快返回（超时会被系统摘掉）。
 * 本程序主线程每帧调用 SDL_PollEvent()，满足消息循环；回调里只做 O(1) 的记录。
 * 生产者与消费者都是主线程，因此下面的状态量不需要加锁。
 *
 * 坐标空间（这里踩过一次坑）：位置一律用 GetCursorPos() 取，**不要**用回调里的
 * MSLLHOOKSTRUCT.pt。低层钩子给的是"物理屏幕坐标"，而本程序渲染用的坐标系由 SDL 决定：
 * SDL 只在 SDL_HINT_WINDOWS_DPI_SCALING 打开时才做 DPI 换算（默认关闭，直接返回
 * GetCursorPos 的结果），而 GetCursorPos 在非 per-monitor aware 的进程里会被系统虚拟化成
 * 逻辑坐标。两者在缩放显示器上差一个比例，表现就是特效"追不上"光标、越往右下偏得越多。
 * 用 GetCursorPos 取位置，钩子只负责"什么时候发生了什么事件"，坐标系自然和窗口/投影一致。
 */

static volatile LONG s_pos_x = 0, s_pos_y = 0;
static volatile LONG s_has_pos = 0;
static volatile LONG s_btn_down = 0;
static volatile LONG s_click_count = 0;
static volatile LONG s_click_x = 0, s_click_y = 0;
static HHOOK s_mouse_hook = NULL;

static LRESULT CALLBACK mouse_ll_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        POINT pt;
        if (!GetCursorPos(&pt)) {
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        switch (wParam) {
            case WM_MOUSEMOVE:
                s_pos_x = pt.x;
                s_pos_y = pt.y;
                s_has_pos = 1;
                break;
            case WM_LBUTTONDOWN:
                if (s_click_count == 0) {   /* 只记第一次按下的位置 */
                    s_click_x = pt.x;
                    s_click_y = pt.y;
                }
                s_click_count++;
                s_btn_down = 1;
                break;
            case WM_LBUTTONUP:
                s_btn_down = 0;
                break;
            default:
                break;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int platform_input_init(void) {
    POINT pt;
    if (s_mouse_hook) return 1;

    /* 先播一下当前位置，让拖尾在第一帧就有起点 */
    if (GetCursorPos(&pt)) {
        s_pos_x = pt.x;
        s_pos_y = pt.y;
        s_has_pos = 1;
    }
    s_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_ll_proc, GetModuleHandleW(NULL), 0);
    return s_mouse_hook != NULL;
}

void platform_input_shutdown(void) {
    if (s_mouse_hook) {
        UnhookWindowsHookEx(s_mouse_hook);
        s_mouse_hook = NULL;
    }
}

void platform_input_read(PlatformInputFrame *out) {
    if (!out) return;
    out->has_pos     = (int)s_has_pos;
    out->pos_x       = (int)s_pos_x;
    out->pos_y       = (int)s_pos_y;
    out->is_down     = (int)s_btn_down;
    out->click_count = (int)s_click_count;
    out->click_x     = (int)s_click_x;
    out->click_y     = (int)s_click_y;

    /* 事件取走即清；位置和按下状态保留，它们表示"当前"，不是"本帧发生过" */
    s_click_count = 0;
    s_has_pos = 0;
}

int platform_cursor_visible(void) {
    CURSORINFO ci;
    ci.cbSize = sizeof(CURSORINFO);
    if (!GetCursorInfo(&ci)) return 1;
    /* ShowCursor(FALSE) 式隐藏 */
    if (!(ci.flags & CURSOR_SHOWING)) return 0;
    /* SetCursor(NULL) 式隐藏：显示计数仍然是"显示"，但句柄为空。
       LWJGL2（Minecraft 1.12 这类）走的就是这条路，只查 CURSOR_SHOWING 检测不到。 */
    if (ci.hCursor == NULL) return 0;
    return 1;
}

#else /* !_WIN32：本项目实际只在 Windows 上跑，留空实现保证能编过 */

int platform_input_init(void) { return 0; }
void platform_input_shutdown(void) {}
void platform_input_read(PlatformInputFrame *out) {
    if (out) memset(out, 0, sizeof(*out));
}
int platform_cursor_visible(void) { return 1; }

#endif /* _WIN32 */
