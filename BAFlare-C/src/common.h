#ifndef COMMON_H
#define COMMON_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glad/glad.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Windows 平台 DWM 透明支持 + 托盘 + 原生对话框 */
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_VERTS   10000
#define MAX_CMDS    8192
#define MAX_SPARKS  64
#define MAX_WAVES   64
#define MAX_TRAIL   16

#endif /* COMMON_H */
