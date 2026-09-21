cmake_minimum_required(VERSION 3.12.4)
project(BA-Spark-C C CXX)

# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output path for the executable
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# 不再依赖 SDL2：窗口 / GL 上下文 / 消息循环 / 时钟都在 src/platform_win.c 和 common.h 里
# 直接用 Win32 + WGL 实现，所以这里不需要 find_package(SDL2)

# Sources: main.c and glad.c
set(MAIN_SOURCES
        src/main.c
        src/glad.c
        src/common.h
        src/config.h
        src/renderer.h
        src/renderer.c
        src/spark.h
        src/spark.c
        src/platform_win.h
        src/platform_win.c
)

# Required libraries（SDL2 已经不用了，SDL2main 那个 WinMain 垫片也就不需要了）
set(MAIN_LIBS "")

# Add OpenGL library depending on the OS
if(WIN32)
    list(APPEND MAIN_LIBS opengl32)
else()
    find_package(OpenGL REQUIRED)
    list(APPEND MAIN_LIBS OpenGL::GL)
endif()

# Add math and pthread for non-MSVC compilers (like MinGW/GCC)
if(NOT MSVC)
    list(APPEND MAIN_LIBS m pthread)
endif()

# Custom target to run the executable
add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)

# Create the main executable
if(WIN32)
    set(GUI_TYPE WIN32) # 这会自动添加 -mwindows
endif()
add_executable(main ${MAIN_SOURCES} app.manifest)

# Include directories: add 'src' so it can find <glad/glad.h>
target_include_directories(main PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(main ${MAIN_LIBS} dwmapi comctl32 uxtheme psapi)


# On Windows, GUI applications do not show a console by default,
# which hides log output. This ensures a console is available for logging.
# Release 模式下应被注释以隐藏黑框
if (WIN32)
    if (MSVC)
        #target_link_options(main PRIVATE "/SUBSYSTEM:CONSOLE")
    else()
        #target_link_options(main PRIVATE "-mconsole")
    endif()
endif()
