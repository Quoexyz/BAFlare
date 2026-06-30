#include "common.h"
#include "renderer.h"
#include "spark.h"
#include "platform_win.h"

/* ---------- Globals ---------- */
#ifdef _WIN32
HWND  g_main_hwnd       = NULL;
MouseSpark *g_spark_ref = NULL;
// DPI感知 但会在不刷新时被窗口合成器填黑 暂未解决 可能需要牺牲一直刷新? 先留在这里
//WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(HANDLE);
//#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

/* ---------- Main ---------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

//#ifdef _WIN32
    // DPI 感知防止多显示器缩放比例不同导致坐标错位
    //SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
//#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* OpenGL 属性设置 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    // 暂时关闭抗锯齿，后面可能模糊同时光晕
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* ---- 多屏支持：遍历所有显示器求并集 ---- */
    int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
    int num_displays = SDL_GetNumVideoDisplays();
    if (num_displays < 1) {
        fprintf(stderr, "SDL_GetNumVideoDisplays failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Rect bounds;
    /* 初始化为第一个显示器的边界 */
    if (SDL_GetDisplayBounds(0, &bounds) != 0) {
        fprintf(stderr, "SDL_GetDisplayBounds failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int min_x = bounds.x;
    int min_y = bounds.y;
    int max_x = bounds.x + bounds.w;
    int max_y = bounds.y + bounds.h;

    /* 遍历剩余显示器，求出所有显示器的包围盒 */
    for (int i = 1; i < num_displays; ++i) {
        if (SDL_GetDisplayBounds(i, &bounds) == 0) {
            if (bounds.x < min_x) min_x = bounds.x;
            if (bounds.y < min_y) min_y = bounds.y;
            if (bounds.x + bounds.w > max_x) max_x = bounds.x + bounds.w;
            if (bounds.y + bounds.h > max_y) max_y = bounds.y + bounds.h;
        }
    }

    win_x = min_x;
    win_y = min_y;
    win_w = max_x - min_x;
    win_h = max_y - min_y;

    SDL_Window *win = SDL_CreateWindow("Spark Cursor Effect",
                                       win_x, win_y, win_w, win_h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
                                       SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_ShowCursor(SDL_DISABLE);

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    setup_platform_window(win);
    // 关闭抗锯齿第二处
    // glEnable(GL_MULTISAMPLE);

    /* 初始化渲染器和火花效果 */
    g_prog = make_program(VS_SRC, FS_SRC);
    glUseProgram(g_prog);
    g_u_proj    = glGetUniformLocation(g_prog, "u_proj");
    g_u_use_tex = glGetUniformLocation(g_prog, "u_use_tex");
    g_u_tex     = glGetUniformLocation(g_prog, "u_tex");
    glUniform1i(g_u_tex, 0);

    glViewport(0, 0, win_w, win_h);
    float proj[16];
    make_ortho(proj, 0.0f, (float)win_w, (float)win_h, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, proj);

    batch_init(&g_batch, MAX_VERTS);

    // 在初始化时固定混合状态
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    MouseSpark spark;
    spark_init(&spark);
#ifdef _WIN32
    g_spark_ref = &spark;
#endif

    // 注册表读取
#ifdef _WIN32
    if (!load_registry_config(&spark)) {
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
#endif

    int running = 1;
    const int FPS = 60;
    const int IDLE_FPS = 20;
    const Uint32 frame_delay = 1000 / FPS;
    const Uint32 idle_delay = 1000/IDLE_FPS;

    int needs_clear = 1;

    while (running) {
        Uint32 frame_start = SDL_GetTicks();
        Uint32 now = frame_start;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = 0; break; }

            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE: running = 0; break;
                    default: break;
                }
            }
        }
        if (!running) break;

        int gmx, gmy;
        Uint32 mouse_state = SDL_GetGlobalMouseState(&gmx, &gmy);
        int mx = gmx - win_x;
        int my = gmy - win_y;
        int mouse_down = (mouse_state & SDL_BUTTON_LMASK) != 0;

        /* ---- 检测系统鼠标是否被隐藏（如游戏准星模式） ---- */
        // 发现在1.12.2的Minecraft不起作用，BASpark也有同样的问题，待解决
        int cursor_visible = 1;
#ifdef _WIN32
        CURSORINFO ci;
        ci.cbSize = sizeof(CURSORINFO);
        if (GetCursorInfo(&ci)) {
            cursor_visible = (ci.flags & CURSOR_SHOWING) != 0;
        }
#endif

        /* ---- 仅在系统光标可见时才触发特效 ---- */
        if (cursor_visible) {
            if (mouse_down) {
                if (!spark.is_down) {
                    spark.is_down = 1;
                    spark_boom(&spark, (float)mx, (float)my);
                } else {
                    spark_create_move_sparks(&spark, (float)mx, (float)my);
                }
            } else {
                spark.is_down = 0;
            }
        } else {
            // 鼠标被隐藏，重置按下状态，防止切回桌面时误触爆炸
            spark.is_down = 0;
        }


        int has_effects = (spark.spark_count > 0 || spark.wave_count > 0 || spark.trail_count > 0);

        if (has_effects) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            batch_begin(&g_batch);
            spark_update_and_draw(&spark, now);
            batch_end(&g_batch);

            SDL_GL_SwapWindow(win);
            needs_clear = 1;
        } else {
            if (needs_clear) {
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                SDL_GL_SwapWindow(win);
                needs_clear = 0;
            }
            spark.last_frame_time = now;
        }

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        Uint32 target_delay = has_effects ? frame_delay : idle_delay;
        if (frame_time < target_delay) {
            SDL_Delay(target_delay - frame_time);
        }
    }

    /* 清理资源 */
    glDeleteProgram(g_prog);
    glDeleteVertexArrays(1, &g_batch.vao);
    glDeleteBuffers(1, &g_batch.vbo);
    free(g_batch.buf);

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}