#include "common.h"
#include "renderer.h"
#include "spark.h"
#include "platform_win.h"

/* ---------- Globals ---------- */
#ifdef _WIN32
HWND  g_main_hwnd       = NULL;
MouseSpark *g_spark_ref = NULL;
#endif

/* ---------- Main ---------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

#ifdef _WIN32
    /* 移除了 InitCommonControlsEx，因为不再使用 Win32 高级控件 */
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* OpenGL 属性设置 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Rect display_bounds;
    SDL_GetDisplayBounds(0, &display_bounds);
    int win_x = display_bounds.x;
    int win_y = display_bounds.y;
    int win_w = display_bounds.w;
    int win_h = display_bounds.h;

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

    /* ---- 新增：读取注册表配置，失败则退出 ---- */
#ifdef _WIN32
    if (!load_registry_config(&spark)) {
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1; // load_registry_config 已经显示了错误消息框
    }
#else
    /* 非 Windows 平台的默认配置（防止未初始化） */
    apply_color(&spark, g_presets[0].r, g_presets[0].g, g_presets[0].b);
#endif

    int running = 1;
    const int FPS = 60;
    const Uint32 frame_delay = 1000 / FPS;
    const Uint32 idle_delay = 50;

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
                    /* 移除了其他快捷键，由外部设置 UI 控制 */
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
            int gmx, gmy;
            Uint32 mouse_state = SDL_GetGlobalMouseState(&gmx, &gmy);
            int mx = gmx - win_x;
            int my = gmy - win_y;
            int mouse_down = (mouse_state & SDL_BUTTON_LMASK) != 0;

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
