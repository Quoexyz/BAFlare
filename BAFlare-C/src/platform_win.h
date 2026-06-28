#ifndef PLATFORM_WIN_H
#define PLATFORM_WIN_H

#include "common.h"
#include "spark.h"

#ifdef _WIN32

#include <windows.h>

/*
 * 从注册表加载配置
 * 成功返回 1，失败返回 0
 */
int load_registry_config(MouseSpark *spark);

/* 设置平台窗口属性（透明、穿透等） */
void setup_platform_window(SDL_Window *window);

#endif /* _WIN32 */

#endif /* PLATFORM_WIN_H */
