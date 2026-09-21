#include "common.h"
#include "renderer.h"
#include "spark.h"
#include "platform_win.h"

/* ---------- Globals ---------- */
#ifdef _WIN32
#include <psapi.h>
HWND  g_main_hwnd       = NULL;
MouseSpark *g_spark_ref = NULL;
// DPI感知 但会在不刷新时被窗口合成器填黑 暂未解决 可能需要牺牲一直刷新? 先留在这里
//WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(HANDLE);
//#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)

/* 查询系统 CPU 使用率（两次调用间的时间间隔越大越准确） */
static float get_system_cpu_usage(void) {
    static int first = 1;
    static ULARGE_INTEGER prev_idle, prev_kernel, prev_user;
    FILETIME ft_idle, ft_kernel, ft_user;
    GetSystemTimes(&ft_idle, &ft_kernel, &ft_user);
    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart   = ft_idle.dwLowDateTime;
    idle.HighPart  = ft_idle.dwHighDateTime;
    kernel.LowPart = ft_kernel.dwLowDateTime;
    kernel.HighPart= ft_kernel.dwHighDateTime;
    user.LowPart   = ft_user.dwLowDateTime;
    user.HighPart  = ft_user.dwHighDateTime;
    if (first) {
        prev_idle = idle; prev_kernel = kernel; prev_user = user;
        first = 0;
        return 0.0f;
    }
    ULONGLONG idle_diff   = idle.QuadPart   - prev_idle.QuadPart;
    ULONGLONG kernel_diff = kernel.QuadPart - prev_kernel.QuadPart;
    ULONGLONG user_diff   = user.QuadPart   - prev_user.QuadPart;
    ULONGLONG total       = kernel_diff + user_diff;
    prev_idle = idle; prev_kernel = kernel; prev_user = user;
    if (total == 0) return 0.0f;
    return (float)(total - idle_diff) / (float)total * 100.0f;
}
#endif

/* ---------- Frame pacing ---------- */
/*
 * 等待时保持消息泵运转。
 * 低层鼠标钩子的回调要在主线程的消息循环里被调用，如果一口气 SDL_Delay 很久，
 * 系统在分发鼠标事件时会被我们拖住（表现为全系统鼠标发滞）。所以把长等待切成小片，
 * 每片之间泵一次消息；一帧内的短补偿直接睡，避免影响帧率。
 */
static void wait_keeping_pump(Uint32 remaining_ms) {
    Uint32 deadline;
    if (remaining_ms == 0) return;
    if (remaining_ms <= 8) {
        SDL_Delay(remaining_ms);
        return;
    }
    deadline = SDL_GetTicks() + remaining_ms;
    while ((Sint32)(deadline - SDL_GetTicks()) > 0) {
        Sint32 left;
        SDL_PumpEvents();
        left = (Sint32)(deadline - SDL_GetTicks());
        if (left <= 0) break;
        SDL_Delay((Uint32)(left < 4 ? left : 4));
    }
}

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

    /* 事件驱动的鼠标输入源：WH_MOUSE_LL 低层钩子，取代原来的逐帧采样。
       它的回调在主线程泵消息时被调用，所以每帧的 SDL_PollEvent 就是驱动；
       线程要求和坐标空间说明见 platform_win.c。装不上时返回 0，主循环退回采样。 */
    int input_event_driven = platform_input_init();

    /* 初始化渲染器和火花效果 */
    g_prog = make_program(VS_SRC, FS_SRC);
    glUseProgram(g_prog);
    g_u_proj    = glGetUniformLocation(g_prog, "u_proj");

    glViewport(0, 0, win_w, win_h);
    float proj[16];
    make_ortho(proj, 0.0f, (float)win_w, (float)win_h, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, proj);

    batch_init(&g_batch, MAX_VERTS);

    // 在初始化时固定混合状态
    // 必须用 glBlendFuncSeparate，不能用 glBlendFunc：后者的因子对 RGBA 四个通道都生效，
    // 在清空的透明底上画 alpha=a 的东西会得到 dst.a = a*a（alpha 被平方），而 canvas 的
    // source-over 里 alpha 是线性叠加的（dst.a = a）。窗口是靠 DwmExtendFrameIntoClientArea
    // 的玻璃合成显示透明的，合成公式是 底色*(1-dst.a) + dst.rgb，所以 alpha 被平方后整个
    // 特效都比 JS 更透明 —— 白色背景上几乎看不见（深色背景因为 rgb 仍按 a 走，反而还行）。
    // 分开设置后：RGB 走标准 alpha 混合、alpha 走线性叠加，与 canvas 的 source-over 等价，
    // 输出的正好是预乘 alpha 格式，也就是 DWM 合成期望的格式。
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

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
    Uint32 last_trim_time = 0;   /* 上次裁剪工作集的时间戳 */

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

        /* ---- 取这一帧累积的鼠标输入 ---- */
        PlatformInputFrame in;
        if (input_event_driven) {
            platform_input_read(&in);
        } else {
            /* 兜底：钩子没装上时退回逐帧采样（会漏短按，但至少能用） */
            int gmx = 0, gmy = 0;
            Uint32 mouse_state = SDL_GetGlobalMouseState(&gmx, &gmy);
            in.has_pos     = 1;
            in.pos_x       = gmx;
            in.pos_y       = gmy;
            in.is_down     = (mouse_state & SDL_BUTTON_LMASK) != 0;
            in.click_x     = gmx;
            in.click_y     = gmy;
            /* 采样没有事件，只能靠和上一帧比较补出"按下"这个边沿 */
            in.click_count = (in.is_down && !spark.is_down) ? 1 : 0;
        }

        /* ---- 仅在系统光标可见时才触发特效 ---- */
        if (platform_cursor_visible()) {
            if (in.click_count > 0) {
                /* 按下是"事件"：即使按下和抬起落在同一帧内，也一定炸出这一下 */
                spark.is_down = 1;
                spark_boom(&spark, (float)(in.click_x - win_x), (float)(in.click_y - win_y));
            } else {
                spark.is_down = in.is_down;
            }
            /* 拖尾：按住左键才产生，每次喂"这一帧最后的位置"。
               不逐事件喂是有依据的：浏览器的 mousemove 本来就被合并到每帧一次，
               JS 参考实现实际也是每帧一个点；而每帧喂几百个原始事件会把 MAX_TRAIL
               的环形缓冲瞬间填满，尾巴反而缩成一截。 */
            if (spark.is_down && in.has_pos) {
                spark_create_move_sparks(&spark,
                                         (float)(in.pos_x - win_x),
                                         (float)(in.pos_y - win_y));
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

#ifdef _WIN32
            // 每 30s 空闲且系统 CPU < 50% 时裁剪工作集
            if (now - last_trim_time >= 30000) {
                if (get_system_cpu_usage() < 50.0f) {
                    EmptyWorkingSet(GetCurrentProcess());
                }
                last_trim_time = now;
            }
#endif
        }

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        Uint32 target_delay = has_effects ? frame_delay : idle_delay;
        if (frame_time < target_delay) {
            wait_keeping_pump(target_delay - frame_time);
        }
    }

    /* 清理资源 */
    platform_input_shutdown();

    glDeleteProgram(g_prog);
    glDeleteVertexArrays(1, &g_batch.vao);
    glDeleteBuffers(1, &g_batch.vbo);
    free(g_batch.buf);

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}