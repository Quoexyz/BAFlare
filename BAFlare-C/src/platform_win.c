#include "platform_win.h"

#ifdef _WIN32

#include <uxtheme.h>
#include <dwmapi.h>

/* 输入钩子用来叫醒主循环的自定义消息（只是"有输入了"的信号，不需要处理） */
#define WM_APP_WAKE (WM_APP + 1)

static HWND s_app_hwnd = NULL;
static volatile LONG s_quit = 0;

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

/* ---------- 窗口 + OpenGL 上下文 + 消息循环 ---------- */
/*
 * 这一层原来是 SDL 干的（SDL_CreateWindow / SDL_GL_* / SDL_PollEvent / SDL_PumpEvents /
 * SDL_WaitEventTimeout），现在直接用 Win32 + WGL。几个要点：
 *
 *  - 窗口类必须带 CS_OWNDC：OpenGL 要在一个固定 DC 上工作。
 *  - pixel format 必须真的带 alpha 通道（8 位）。老式 ChoosePixelFormat 只能"猜最近似"，
 *    很可能给一个没有 alpha 的格式 —— 那样 framebuffer 的 alpha 全是 0，DWM 合成出来的
 *    就是"完全透明"：窗口看得见桌面、但画什么都看不见。而且一个窗口 DC 的 pixel format
 *    只能设置一次，猜错了没得改，所以必须先引导出 wglChoosePixelFormatARB 明确要 alpha。
 *  - GL 上下文优先用 wglCreateContextAttribsARB 要 3.3 core（和原来 SDL 一致），
 *    失败再退回传统 wglCreateContext。着色器是 #version 330 core，只有退回传统上下文
 *    拿到的是低版本（比如 GDI 软件实现的 1.1）时，着色器会编译失败、什么都画不出来。
 *  - 引导扩展需要"当前有上下文"，而设置格式之前又不能建上下文 → 用一个哑窗口做引导，
 *    这也是 SDL 的做法。
 *  - wglSwapIntervalEXT(0) 关垂直同步，和原来 SDL_GL_SetSwapInterval(0) 一致。
 *  - 入口仍是 main()：子系统没有改成 windows，所以不需要 WinMain（和改造前一致）。
 */

/* wgl 扩展：函数原型和常量都不在 win32 头文件里，自己声明 */
typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);

#define WGL_DRAW_TO_WINDOW_ARB          0x2001
#define WGL_ACCELERATION_ARB            0x2003
#define WGL_SUPPORT_OPENGL_ARB          0x2010
#define WGL_DOUBLE_BUFFER_ARB           0x2011
#define WGL_PIXEL_TYPE_ARB              0x2013
#define WGL_COLOR_BITS_ARB              0x2014
#define WGL_ALPHA_BITS_ARB              0x201B
#define WGL_DEPTH_BITS_ARB              0x2022
#define WGL_STENCIL_BITS_ARB            0x2023
#define WGL_FULL_ACCELERATION_ARB       0x2027
#define WGL_TYPE_RGBA_ARB               0x202B
#define WGL_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB   0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB    0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

/* 用哑窗口取出 wgl 扩展函数指针（要在给真实窗口设 pixel format 之前做） */
static void wgl_bootstrap(HINSTANCE inst, const wchar_t *cls,
                          PFNWGLCHOOSEPIXELFORMATARBPROC *fn_choose,
                          PFNWGLCREATECONTEXTATTRIBSARBPROC *fn_create) {
    PIXELFORMATDESCRIPTOR pfd;
    HWND dummy;
    HDC dc;
    HGLRC rc;
    int fmt;

    *fn_choose = NULL;
    *fn_create = NULL;

    dummy = CreateWindowExW(0, cls, L"", WS_POPUP | WS_DISABLED, 0, 0, 1, 1,
                            NULL, NULL, inst, NULL);
    if (!dummy) return;
    dc = GetDC(dummy);
    if (!dc) { DestroyWindow(dummy); return; }

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    fmt = ChoosePixelFormat(dc, &pfd);
    if (fmt && SetPixelFormat(dc, fmt, &pfd)) {
        rc = wglCreateContext(dc);
        if (rc && wglMakeCurrent(dc, rc)) {
            *fn_choose = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
            *fn_create = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
            wglMakeCurrent(NULL, NULL);
        }
        if (rc) wglDeleteContext(rc);
    }
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
}

static LRESULT CALLBACK app_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* 只认主窗口。
       注意：wgl_bootstrap() 里的哑窗口用的是同一个窗口类，它的 WM_DESTROY 会走到这里来；
       如果不加这个判断，引导完扩展就把 s_quit 置上了 —— 主循环第一帧就退出（退出码 0），
       表现为"窗口建起来了、GL 也正常，但什么都不显示"。 */
    const int is_main = (hwnd == s_app_hwnd);

    switch (msg) {
        case WM_CLOSE:
            if (is_main) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (is_main) s_quit = 1;
            return 0;
        case WM_ERASEBKGND:
            return 1;   /* 自己会画，不要让系统擦背景，免得闪 */
        case WM_APP_WAKE:
            return 0;   /* 输入钩子的唤醒消息，收到就算数 */
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void platform_desktop_bounds(int *x, int *y, int *w, int *h) {
    /* 和原来"对每个 SDL_GetDisplayBounds 求并集"等价 */
    if (x) *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    if (y) *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (w) *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    if (h) *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void *platform_gl_get_proc(const char *name) {
    /* wglGetProcAddress 只管扩展函数，OpenGL 1.1 的函数要从 opengl32.dll 取 */
    void *p = (void *)wglGetProcAddress(name);
    if (p == NULL || p == (void *)0x1 || p == (void *)0x2 ||
        p == (void *)0x3 || p == (void *)-1) {
        static HMODULE gl = NULL;
        if (!gl) gl = LoadLibraryA("opengl32.dll");
        p = gl ? (void *)GetProcAddress(gl, name) : NULL;
    }
    return p;
}

int platform_app_create(PlatformApp *app, const wchar_t *title,
                        int x, int y, int w, int h) {
    static const wchar_t *cls = L"SparkCursorEffectWindow";
    WNDCLASSEXW wc;
    PIXELFORMATDESCRIPTOR pfd;
    PFNWGLCHOOSEPIXELFORMATARBPROC fn_choose = NULL;
    PFNWGLCREATECONTEXTATTRIBSARBPROC fn_create = NULL;
    HINSTANCE inst = GetModuleHandleW(NULL);
    HDC hdc;
    HGLRC glrc = NULL;
    int fmt = 0;

    if (!app) return 0;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = app_wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = NULL;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 0;
    }

    /* 无边框弹出窗口。扩展样式分两步加，见下面 setup_platform_window() 的说明 */
    app->hwnd = CreateWindowExW(0, cls, title, WS_POPUP | WS_MINIMIZEBOX,
                                x, y, w, h, NULL, NULL, inst, NULL);
    if (!app->hwnd) return 0;
    s_app_hwnd = app->hwnd;
    s_quit = 0;

    hdc = GetDC(app->hwnd);
    if (!hdc) {
        platform_app_destroy(app);
        return 0;
    }

    /* 先用哑窗口把 wgl 扩展引导出来。必须在给本窗口设 pixel format 之前做：
       一个窗口 DC 的 pixel format 只能设置一次，猜错了就没法回头。 */
    wgl_bootstrap(inst, cls, &fn_choose, &fn_create);

    /* 选 pixel format：优先 ARB，能明确要到"8 位 alpha + 完全加速"；
       退路 ChoosePixelFormat 只是"取最近似"，很可能拿不到 alpha 通道。 */
    if (fn_choose) {
        const int fattribs[] = {
            WGL_DRAW_TO_WINDOW_ARB,    GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB,    GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB,     GL_TRUE,
            WGL_PIXEL_TYPE_ARB,        WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB,        32,
            WGL_ALPHA_BITS_ARB,        8,
            WGL_DEPTH_BITS_ARB,        0,
            WGL_STENCIL_BITS_ARB,      0,
            WGL_ACCELERATION_ARB,      WGL_FULL_ACCELERATION_ARB,
            0
        };
        UINT nfmt = 0;
        if (fn_choose(hdc, fattribs, NULL, 1, &fmt, &nfmt) && nfmt > 0) {
            DescribePixelFormat(hdc, fmt, sizeof(pfd), &pfd);
        } else {
            fmt = 0;
        }
    }
    if (fmt == 0) {
        ZeroMemory(&pfd, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;
        fmt = ChoosePixelFormat(hdc, &pfd);
    }
    if (!fmt || !SetPixelFormat(hdc, fmt, &pfd)) {
        fprintf(stderr, "[GL] SetPixelFormat failed (fmt=%d)\n", fmt);
        ReleaseDC(app->hwnd, hdc);
        app->hwnd = NULL;
        s_app_hwnd = NULL;
        return 0;
    }
    app->hdc = hdc;

    /* 上下文优先 3.3 core（和原来 SDL 一致），失败退回传统上下文 */
    if (fn_create) {
        const int cattribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB,    3,
            WGL_CONTEXT_MINOR_VERSION_ARB,    3,
            WGL_CONTEXT_PROFILE_MASK_ARB,     WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        glrc = fn_create(hdc, NULL, cattribs);
    }
    if (!glrc) {
        fprintf(stderr, "[GL] wglCreateContextAttribsARB unavailable/failed, fallback to legacy context\n");
        glrc = wglCreateContext(hdc);
    }
    if (!glrc || !wglMakeCurrent(hdc, glrc)) {
        fprintf(stderr, "[GL] context creation failed\n");
        if (glrc) wglDeleteContext(glrc);
        ReleaseDC(app->hwnd, hdc);
        app->hwnd = NULL;
        app->hdc = NULL;
        s_app_hwnd = NULL;
        return 0;
    }
    app->glrc = glrc;
    /* 兜底再清一次退出标志（wgl 引导过程的哑窗口消息不该影响主循环） */
    s_quit = 0;
    return 1;
}

void platform_app_destroy(PlatformApp *app) {
    if (!app) return;
    if (app->glrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(app->glrc);
        app->glrc = NULL;
    }
    if (app->hdc && app->hwnd) {
        ReleaseDC(app->hwnd, app->hdc);
        app->hdc = NULL;
    }
    if (app->hwnd) {
        DestroyWindow(app->hwnd);
        app->hwnd = NULL;
    }
    s_app_hwnd = NULL;
}

void platform_app_show(PlatformApp *app) {
    if (app && app->hwnd) {
        /* SW_SHOW：显示并激活。
           曾经为了"不抢焦点"改成 SW_SHOWNOACTIVATE，结果玻璃合成那条链路就失效了
           （窗口可见但客户区不再被当作玻璃、DWM 不采用我们的 alpha，画面上什么都没有），
           所以退回来。要"不抢焦点"得另找办法，不能动这里。 */
        ShowWindow(app->hwnd, SW_SHOW);
    }
}

void platform_app_vsync_off(PlatformApp *app) {
    PFNWGLSWAPINTERVALEXTPROC set_interval;
    if (!app || !app->glrc) return;
    set_interval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (set_interval) set_interval(0);
}

void platform_app_swap(PlatformApp *app) {
    if (app && app->hdc) SwapBuffers(app->hdc);
}

void platform_app_pump_now(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

int platform_app_pump(void) {
    platform_app_pump_now();
    return s_quit ? 0 : 1;
}

int platform_app_wait(int timeout_ms) {
    /* 和原来 SDL_WaitEventTimeout 在 Windows 上的实现同一套：
       MsgWaitForMultipleObjects 阻塞在消息队列上，几乎不占 CPU，消息一到就醒。
       MWMO_INPUTAVAILABLE 是必须的，否则会漏掉"调用之前就已经在队列里"的消息。 */
    DWORD r = MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)timeout_ms,
                                          QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    return r == WAIT_OBJECT_0 ? 1 : 0;
}

/* ---------- Platform Setup ---------- */
/*
 * 样式分两次设，位置很讲究（都是踩过才分出来的）：
 *
 *  1) setup_platform_window()        隐藏状态下调用：只设 WS_EX_TOOLWINDOW + 置顶。
 *     TOOLWINDOW 只管"不进任务栏 / 不进 Alt-Tab"，不影响渲染，可以提前设 ——
 *     提前设任务栏就永远不会为它建按钮（设晚了会先建再撤，闪一下）。
 *  2) platform_app_show()
 *  3) setup_platform_window_transparent()  显示之后调用：分层 + 穿透 + 玻璃合成。
 *     **这两项都不能提前**：WS_EX_LAYERED 在窗口显示之前设，窗口会变成
 *     "透明但完全不渲染"的状态 —— 表现是粒子和特效全都没有、任务栏里还一直挂着它。
 */
void setup_platform_window(PlatformApp *app) {
    HWND hwnd;
    LONG ex_style;

    if (!app || !app->hwnd) return;
    hwnd = app->hwnd;

    ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, ex_style | WS_EX_TOOLWINDOW);

    /*
     * MSDN 要求：用 SetWindowLong 改过 GWL_EXSTYLE 之后，必须调用
     * SetWindowPos(SWP_FRAMECHANGED) 让新样式生效；顺带把置顶设上。
     * （以前这里还得替 SDL 打补丁——SDL 2.3x 创建窗口时根本不应用
     * SDL_WINDOW_ALWAYS_ON_TOP，而本窗口又因为 WS_EX_TRANSPARENT 收不到鼠标事件，
     * 它那条"鼠标进入窗口才补置顶"的路径永远不会触发。自己管窗口就没这个问题了。）
     */
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void setup_platform_window_transparent(PlatformApp *app) {
    MARGINS margins = { -1, -1, -1, -1 };
    HWND hwnd;
    LONG ex_style;

    if (!app || !app->hwnd) return;
    hwnd = app->hwnd;

    /* 分层 + 点击穿透。必须在窗口已经显示之后再设，见上面的说明。 */
    ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    /* 玻璃合成：把整个客户区当成玻璃区域，DWM 才会采用我们 framebuffer 的 alpha 通道。
       同样必须在窗口**已经可见之后**调用 —— 对从未显示过的窗口调用会无功而返。 */
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

/* ---------- 鼠标输入：低层钩子（事件驱动，替代逐帧轮询） ---------- */
/*
 * 为什么不用逐帧采样：
 * GetCursorPos + GetAsyncKeyState 只能反映"调用那一刻"的状态。空闲时主循环停渲染，
 * 采样间隔可能到几十毫秒，一次 30ms 的快速点击可能整段落在两次采样之间，按下这个
 * "边沿"就丢了，那一下完全没有特效。WH_MOUSE_LL 由系统在分发鼠标事件时回调，
 * 按下/抬起/移动一个都不会漏。
 *
 * 线程要求（MSDN）：低层钩子的回调运行在"安装钩子的那个线程"上，靠系统向该线程投递
 * 消息来触发，所以该线程必须有消息循环，且回调必须很快返回（超时会被系统摘掉）。
 * 本程序主线程每帧调用 platform_app_pump()，满足消息循环；回调里只做 O(1) 的记录。
 * 生产者与消费者都是主线程，因此下面的状态量不需要加锁。
 *
 * 坐标空间：位置一律用 GetCursorPos() 取，**不要**用回调里的 MSLLHOOKSTRUCT.pt。
 * 低层钩子给的是"物理屏幕坐标"，而本程序渲染用的是 GetCursorPos 那套坐标（进程是
 * DPI-unaware 时被系统虚拟化成逻辑坐标）。两者在缩放显示器上差一个比例，表现就是
 * 特效"追不上"光标、越往右下偏得越多。用 GetCursorPos 取位置，钩子只负责
 * "什么时候发生了什么事件"，坐标系自然和窗口/投影一致。
 */

static volatile LONG s_pos_x = 0, s_pos_y = 0;
static volatile LONG s_has_pos = 0;
static volatile LONG s_btn_down = 0;
static volatile LONG s_click_count = 0;
static volatile LONG s_click_x = 0, s_click_y = 0;
static volatile LONG s_dirty = 0;       /* 有未取走的输入 */
static volatile LONG s_wake_posted = 0; /* 已经发过唤醒消息、还没被消费 */
static HHOOK s_mouse_hook = NULL;

/*
 * 通知主线程"有输入了"。
 * 空闲时主循环阻塞在 MsgWaitForMultipleObjects 上，只有收到消息才会醒；
 * 钩子本身不产生窗口消息，所以这里主动 PostMessage 一条，让主线程立刻醒过来渲染，
 * 而不是傻等到超时。
 * 限流：同一时间只挂一条（消费者在 platform_input_read 里复位），
 * 否则高刷鼠标移动会往消息队列里灌上千条。
 */
static void mouse_notify(void) {
    s_dirty = 1;
    if (s_app_hwnd == NULL || s_wake_posted) return;
    if (PostMessageW(s_app_hwnd, WM_APP_WAKE, 0, 0)) s_wake_posted = 1;
}

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
                mouse_notify();
                break;
            case WM_LBUTTONDOWN:
                if (s_click_count == 0) {   /* 只记第一次按下的位置 */
                    s_click_x = pt.x;
                    s_click_y = pt.y;
                }
                s_click_count++;
                s_btn_down = 1;
                mouse_notify();
                break;
            case WM_LBUTTONUP:
                s_btn_down = 0;
                mouse_notify();
                break;
            default:
                break;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int platform_input_init(PlatformApp *app) {
    POINT pt;
    if (s_mouse_hook) return 1;
    if (!app || !app->hwnd) return 0;

    s_app_hwnd = app->hwnd;

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
    if (out) {
        out->has_pos     = (int)s_has_pos;
        out->pos_x       = (int)s_pos_x;
        out->pos_y       = (int)s_pos_y;
        out->is_down     = (int)s_btn_down;
        out->click_count = (int)s_click_count;
        out->click_x     = (int)s_click_x;
        out->click_y     = (int)s_click_y;
    }

    /* 事件取走即清；位置和按下状态保留，它们表示"当前"，不是"本帧发生过"。
       唤醒限流也在这里复位，这样空闲之后到来的第一个新事件能再次把主线程叫醒。 */
    s_click_count = 0;
    s_has_pos = 0;
    s_dirty = 0;
    s_wake_posted = 0;
}

int platform_input_pending(void) { return (int)s_dirty; }

void platform_input_read_sampled(PlatformInputFrame *out) {
    POINT pt = { 0, 0 };
    if (!out) return;
    /* 兜底：钩子装不上时退回采样，只能靠上一帧状态补出"按下"这个边沿 */
    GetCursorPos(&pt);
    out->has_pos     = 1;
    out->pos_x       = (int)pt.x;
    out->pos_y       = (int)pt.y;
    out->is_down     = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
    out->click_count = 0;    /* 由调用方按上一帧状态补 */
    out->click_x     = (int)pt.x;
    out->click_y     = (int)pt.y;
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

int platform_app_create(PlatformApp *app, const wchar_t *title,
                        int x, int y, int w, int h) {
    (void)app; (void)title; (void)x; (void)y; (void)w; (void)h;
    return 0;
}
void platform_app_destroy(PlatformApp *app) { (void)app; }
void platform_app_show(PlatformApp *app) { (void)app; }
void platform_app_vsync_off(PlatformApp *app) { (void)app; }
void platform_app_swap(PlatformApp *app) { (void)app; }
int  platform_app_pump(void) { return 1; }
void platform_app_pump_now(void) {}
int  platform_app_wait(int timeout_ms) { (void)timeout_ms; sleep_ms(1); return 0; }
void platform_desktop_bounds(int *x, int *y, int *w, int *h) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 0;
    if (h) *h = 0;
}
void *platform_gl_get_proc(const char *name) { (void)name; return NULL; }

int platform_input_init(PlatformApp *app) { (void)app; return 0; }
void platform_input_shutdown(void) {}
void platform_input_read(PlatformInputFrame *out) {
    if (out) memset(out, 0, sizeof(*out));
}
int platform_input_pending(void) { return 0; }
void platform_input_read_sampled(PlatformInputFrame *out) {
    if (out) memset(out, 0, sizeof(*out));
}
int platform_cursor_visible(void) { return 1; }

#endif /* _WIN32 */
