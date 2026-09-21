#ifndef PLATFORM_WIN_H
#define PLATFORM_WIN_H

#include "common.h"
#include "spark.h"

#ifdef _WIN32
#include <windows.h>
#endif

/*
 * 窗口 + OpenGL 上下文 + 消息循环。
 * 原来这一层是 SDL 提供的（SDL_CreateWindow / SDL_GL_* / SDL_PollEvent / SDL_GetTicks…），
 * 去掉 SDL 依赖后直接用 Win32 + WGL，实现见 platform_win.c。
 * 对外只暴露这个句柄，main.c 里不出现任何 Win32 类型。
 */
typedef struct {
#ifdef _WIN32
    HWND  hwnd;
    HDC   hdc;
    HGLRC glrc;
#else
    void *hwnd;
    void *hdc;
    void *glrc;
#endif
} PlatformApp;

/* 建窗口 + 建 GL 上下文。成功返回 1 */
int  platform_app_create(PlatformApp *app, const wchar_t *title,
                         int x, int y, int w, int h);
void platform_app_destroy(PlatformApp *app);
void platform_app_show(PlatformApp *app);
/* 关闭垂直同步（原来 SDL_GL_SetSwapInterval(0)），上下文可用之后才能调 */
void platform_app_vsync_off(PlatformApp *app);
/* 交换缓冲（原来 SDL_GL_SwapWindow） */
void platform_app_swap(PlatformApp *app);

/* 处理所有待处理消息；返回 0 表示收到退出请求（关窗 / ESC） */
int  platform_app_pump(void);
/* 只泵消息、不判断退出：等待间隙保持消息泵运转用（原来 SDL_PumpEvents） */
void platform_app_pump_now(void);
/* 阻塞等待消息；返回 1 = 有消息，0 = 超时（原来 SDL_WaitEventTimeout） */
int  platform_app_wait(int timeout_ms);

/* 虚拟桌面包围盒，代替原来对 SDL_GetDisplayBounds 求并集 */
void  platform_desktop_bounds(int *x, int *y, int *w, int *h);
/* 喂给 glad 的 GL 函数加载器 */
void *platform_gl_get_proc(const char *name);

/* ---------- 鼠标输入：低层钩子（事件驱动） ---------- */

/*
 * 一次消费拿到的鼠标输入摘要。
 * is_down 是"状态"（按下到抬起之间一直为真），click_* 是"事件"（本帧内发生过按下）。
 */
typedef struct {
    int has_pos;          /* 本帧内是否有过移动 */
    int pos_x, pos_y;     /* 最后一次移动的屏幕坐标 */
    int is_down;          /* 左键当前是否按下：由按下/抬起事件维护，不走采样 */
    int click_count;      /* 自上次消费以来按下的次数，通常 0 或 1 */
    int click_x, click_y; /* 其中第一次按下的屏幕坐标 */
} PlatformInputFrame;

/* 安装 WH_MOUSE_LL 低层钩子，成功返回 1（返回 0 时主循环退回逐帧采样） */
int  platform_input_init(PlatformApp *app);
void platform_input_shutdown(void);

/* 取走自上次调用以来累积的输入，任何帧都可调用 */
void platform_input_read(PlatformInputFrame *out);

/* 是否有尚未取走的输入（只查询、不消费）。
   空闲停渲染时用它判断能不能继续睡：钩子有新事件会发一条唤醒消息把主线程叫醒。 */
int  platform_input_pending(void);

/* 兜底采样（钩子装不上时用）：GetCursorPos + GetAsyncKeyState 补出按下边沿 */
void platform_input_read_sampled(PlatformInputFrame *out);

/* 系统光标当前是否可见（隐藏说明有程序接管了指针，如游戏准星模式） */
int  platform_cursor_visible(void);

#ifdef _WIN32

/*
 * 从注册表加载配置
 * 成功返回 1，失败返回 0
 */
int load_registry_config(MouseSpark *spark);

/* 设 WS_EX_TOOLWINDOW（不进任务栏）+ 置顶。必须在窗口显示之前调用，否则任务栏会闪一下 */
void setup_platform_window(PlatformApp *app);

/* 设分层 + 点击穿透 + 玻璃合成。必须在窗口已经显示之后调用（提前设会导致什么都不渲染）。
   完整顺序：setup_platform_window → platform_app_show → setup_platform_window_transparent */
void setup_platform_window_transparent(PlatformApp *app);

#endif /* _WIN32 */

#endif /* PLATFORM_WIN_H */
