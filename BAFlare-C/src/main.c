#include "common.h"
#include "renderer.h"
#include "spark.h"
#include "platform_win.h"

/* ---------- Globals ---------- */
#ifdef _WIN32
#include <psapi.h>
/* 老一点的 SDK 头文件里可能没有这个原型（用来判断控制台是不是我们自己独占的） */
WINBASEAPI DWORD WINAPI GetConsoleProcessList(DWORD *lpdwProcessList, DWORD dwProcessCount);

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
 * 低层鼠标钩子的回调要在主线程的消息循环里被调用，如果一口气 Sleep 很久，
 * 系统在分发鼠标事件时会被我们拖住（表现为全系统鼠标发滞）。所以把长等待切成小片，
 * 每片之间泵一次消息；一帧内的短补偿直接睡，避免影响帧率。
 */
static void wait_keeping_pump(Uint32 remaining_ms) {
    Uint32 deadline;
    if (remaining_ms == 0) return;
    if (remaining_ms <= 8) {
        sleep_ms(remaining_ms);
        return;
    }
    deadline = now_ms() + remaining_ms;
    while ((Sint32)(deadline - now_ms()) > 0) {
        Sint32 left;
        platform_app_pump_now();
        left = (Sint32)(deadline - now_ms());
        if (left <= 0) break;
        sleep_ms((Uint32)(left < 4 ? left : 4));
    }
}

/* ---------- Main ---------- */
/*
 * 入口是 WinMain：CMakeLists 里 add_executable 带了 WIN32 关键字（GUI 子系统 / -mwindows），
 * 这样进程**根本不创建控制台** —— 任务栏里那个"黑色控制台"按钮才会彻底消失
 * （只把控制台窗口 ShowWindow(SW_HIDE) 是消不掉它的，试过）。
 * GUI 子系统的启动代码找的是 WinMain，没有它链接就过不去。
 * 注意：GUI 子系统下从终端启动时 stderr 仍可用，但双击运行时没有控制台，
 * 启动阶段的 fprintf 报错看不到（要提示得用 MessageBox）。
 */
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
#else
int main(int argc, char **argv) {
    (void)argc; (void)argv;
#endif
    srand((unsigned)time(NULL));

#ifdef _WIN32
    /* 正常情况下 GUI 子系统没有控制台，这段不会生效。
       留着是为了兜底：万一有启动器给我们单独分配了控制台（且只有我们一个进程在里面），
       就把它藏掉；从终端/IDE 启动时控制台是调用者的，绝不动它。 */
    {
        DWORD procs[2];
        if (GetConsoleProcessList(procs, 2) == 1) {
            HWND con = GetConsoleWindow();
            if (con) ShowWindow(con, SW_HIDE);
        }
    }
#endif

    // DPI 感知的尝试先留在这里：开之后不刷新会被合成器填黑，暂未解决
    //SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    /* ---- 窗口铺满整个虚拟桌面 ---- */
    int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
    platform_desktop_bounds(&win_x, &win_y, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) {
        fprintf(stderr, "platform_desktop_bounds failed\n");
        return 1;
    }

    /* 建窗口 + GL 上下文（原来这一层是 SDL，现在走 WGL）。
       pixel format 里已经要了 8 位 alpha，透明靠 DwmExtendFrameIntoClientArea 的玻璃合成。 */
    PlatformApp app;
    memset(&app, 0, sizeof(app));
    if (!platform_app_create(&app, L"Spark Cursor Effect", win_x, win_y, win_w, win_h)) {
        fprintf(stderr, "platform_app_create failed\n");
        return 1;
    }
    platform_app_vsync_off(&app);   /* 需要当前上下文 */

    if (!gladLoadGLLoader((GLADloadproc)platform_gl_get_proc)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        platform_app_destroy(&app);
        return 1;
    }

    /* 三步顺序不能换（每一步的位置都是踩出来的）：
       1) 隐藏状态下先设 WS_EX_TOOLWINDOW + 置顶：任务栏从此不会为它建按钮
          （设晚了会先建再撤 —— 启动时"任务栏闪一下"）。
       2) 显示窗口。
       3) 显示之后再设 分层 / 点击穿透 + 玻璃合成：
          WS_EX_LAYERED 若在显示之前就设上，窗口会变成"透明但完全不渲染"，
          表现是粒子和特效全都没有、任务栏里还一直挂着它。 */
    setup_platform_window(&app);
    platform_app_show(&app);
    setup_platform_window_transparent(&app);
    // 关闭抗锯齿第二处
    // glEnable(GL_MULTISAMPLE);

    /* 事件驱动的鼠标输入源：WH_MOUSE_LL 低层钩子，取代逐帧采样。
       它的回调在主线程泵消息时被调用，所以每帧的 platform_app_pump() 就是驱动；
       线程要求和坐标空间说明见 platform_win.c。装不上时返回 0，主循环退回采样。 */
    int input_event_driven = platform_input_init(&app);

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
        platform_app_destroy(&app);
        return 1;
    }
#endif

    const Uint32 frame_delay = 1000 / 60;
    const int IDLE_WAIT_MS = 100;   /* 空闲时阻塞等待的上限，纯兜底，不是帧率 */

    int needs_clear = 1;
    Uint32 last_trim_time = 0;   /* 上次裁剪工作集的时间戳 */

    /* 帧首先泵消息：退出请求（关窗 / ESC）由窗口过程置标志，platform_app_pump 返回 0 */
    while (platform_app_pump()) {
        Uint32 frame_start = now_ms();
        Uint32 now = frame_start;

        /* ---- 取这一帧累积的鼠标输入 ---- */
        PlatformInputFrame in;
        if (input_event_driven) {
            platform_input_read(&in);
        } else {
            /* 兜底：钩子没装上时退回逐帧采样（会漏短按，但至少能用） */
            platform_input_read_sampled(&in);
            /* 采样没有事件，只能靠和上一帧比较补出"按下"这个边沿 */
            in.click_count = (in.is_down && !spark.is_down) ? 1 : 0;
        }

        /* ---- 仅在系统光标可见时才触发特效 ---- */
        if (platform_cursor_visible()) {
            if (in.click_count > 0) {
                spark.is_down = 1;
                spark_boom(&spark, (float)(in.click_x - win_x), (float)(in.click_y - win_y));
            } else {
                spark.is_down = in.is_down;
            }
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
            /* ---- 有动画：正常渲染，按 60 FPS 节流 ---- */
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            batch_begin(&g_batch);
            spark_update_and_draw(&spark, now);
            batch_end(&g_batch);

            platform_app_swap(&app);
            needs_clear = 1;

            Uint32 frame_time = now_ms() - frame_start;
            if (frame_time < frame_delay) {
                wait_keeping_pump(frame_delay - frame_time);
            }
            continue;
        }

        /* ---- 没有动画：擦掉最后一帧后彻底停渲染 ----
           不再像以前那样降到 20 FPS 空转（那时每帧仍要做 clear + swap）。
           现在空闲期间不 clear、不 swap，主线程阻塞在消息等待上：
           MsgWaitForMultipleObjects 几乎不占 CPU，同时消息泵仍在转，低层鼠标钩子的
           回调照常被派发。钩子收到事件会 PostMessage 一条把我们立刻叫醒，
           所以起效延迟仍是毫秒级；超时只是兜底（钩子被系统摘掉、或光标刚从游戏里
           恢复可见这类情况）。 */
        if (needs_clear) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            platform_app_swap(&app);
            needs_clear = 0;
        }
        /* 记一下时间，免得醒来后的第一帧 delta 因为"睡了很久"而跳变 */
        spark.last_frame_time = now;

        if (!input_event_driven) {
            /* 兜底：钩子没装上就没有唤醒源，只能按帧轮询（不渲染，代价很低） */
            wait_keeping_pump(frame_delay);
        } else {
            int quit = 0;
            while (!quit) {
                if (!platform_app_wait(IDLE_WAIT_MS)) {
#ifdef _WIN32
                    // 每 30s 空闲且系统 CPU < 50% 时裁剪工作集（借超时这个机会做）
                    Uint32 t = now_ms();
                    if (t - last_trim_time >= 30000) {
                        if (get_system_cpu_usage() < 50.0f) {
                            EmptyWorkingSet(GetCurrentProcess());
                        }
                        last_trim_time = t;
                    }
#endif
                    continue;
                }
                /* 有消息就处理掉（退出请求在这里生效），再看是不是该醒来干活 */
                if (!platform_app_pump()) { quit = 1; break; }
                /* 有输入且光标可见 → 回主循环开始渲染。
                   光标被隐藏时（游戏准星）继续睡：否则会被上千个移动事件反复叫醒空转。 */
                if (platform_input_pending() && platform_cursor_visible()) break;
            }
            if (quit) break;
        }
    }

    /* 清理资源 */
    platform_input_shutdown();

    glDeleteProgram(g_prog);
    glDeleteVertexArrays(1, &g_batch.vao);
    glDeleteBuffers(1, &g_batch.vbo);
    free(g_batch.buf);

    platform_app_destroy(&app);
    return 0;
}