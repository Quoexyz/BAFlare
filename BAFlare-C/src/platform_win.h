#ifndef PLATFORM_WIN_H
#define PLATFORM_WIN_H

#include "common.h"
#include "spark.h"

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

/* 安装 WH_MOUSE_LL 低层鼠标钩子，成功返回 1（返回 0 时调用方应退回逐帧采样） */
int  platform_input_init(void);
void platform_input_shutdown(void);

/* 取走自上次调用以来累积的输入，任何帧都可调用 */
void platform_input_read(PlatformInputFrame *out);

/* 系统光标当前是否可见（隐藏说明有程序接管了指针，如游戏准星模式） */
int  platform_cursor_visible(void);

#ifdef _WIN32

#include <windows.h>

/*
 * 从注册表加载配置
 * 成功返回 1，失败返回 0
 */
int load_registry_config(MouseSpark *spark);

/* 设置平台窗口属性（透明、穿透、置顶等） */
void setup_platform_window(SDL_Window *window);

#endif /* _WIN32 */

#endif /* PLATFORM_WIN_H */
