#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <glad/glad.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Windows 平台 DWM 透明支持 + 原生对话框。
   注意要在下面的内联函数之前包含，因为 now_ms/sleep_ms 用到了 Win32 API。 */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "opengl32.lib")
/* 启用 Common Controls 6.0 视觉样式 */
#ifdef _MSC_VER
#pragma comment(linker, \
"\"/manifestdependency:type='win32' "\
"name='Microsoft.Windows.Common-Controls' "\
"version='6.0.0.0' "\
"processorArchitecture='*' "\
"publicKeyToken='6595b64144ccf1df' "\
"language='*'\"")
#endif
#endif

/* 原来这些整数别名来自 SDL，去掉 SDL 依赖后自己定义（沿用旧名字，少改代码） */
typedef uint32_t Uint32;
typedef int32_t  Sint32;

/* 单调时钟（毫秒）。语义和原来的 SDL_GetTicks() 一致：32 位、1ms 粒度，
   回绕靠无符号减法自然处理。 */
static inline Uint32 now_ms(void) {
#ifdef _WIN32
    return (Uint32)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Uint32)((uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000));
#endif
}

static inline void sleep_ms(Uint32 ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
#endif
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 以下容量参数与 JS 参考实现保持一致（JS 版：MAX_TRAIL 32 / MAX_SPARKS 180 / MAX_WAVES 12） */
#define MAX_VERTS   16384   /* 顶点预算：12 个波 + 180 个火花 + 32 点拖尾的最坏情况约 11k 顶点 */
#define MAX_CMDS    8192
#define MAX_SPARKS  180
#define MAX_WAVES   12
#define MAX_TRAIL   32

#endif /* COMMON_H */
