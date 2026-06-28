# BAFlare
A lightweight, native Windows mouse effect tool that reconstructs the Blue Archive UI style using C, SDL2, and Modern OpenGL.WITHOUT WebView; Inspired by BASpark

![BAFlare Effect](./images/2.PNG)

如果你喜欢这个项目请送个STAR吧谢谢la

## Overview
**BAFlare** is a high-performance rewrite of the original [BASpark](https://github.com/DoomVoss/BASpark).
The original version used a "WPF + WebView2" architecture. While effective, it relied on Webview. This project replaces that stack entirely with a pure **C / SDL2 / OpenGL 3.3 Core** implementation.(And a python GUI) 
### Key Changes
*   **No WebView**: Removes the dependency on the Edge/WebView2 runtime.
*   **Native Performance**: Utilizes a custom Batch Renderer and modern OpenGL shaders for rendering, resulting in lower memory usage and faster startup.
*   **GPU Optimized**: Implements VBO/VAO streaming and vertex compression techniques.
## Features
*   **Blue Archive Style**: Recreates the iconic click sparks, shockwaves, and movement trails.
*   **Lightweight**: Minimal CPU and GPU usage when idle.
*   **Custom Color Palette**: A versatile color palette allowing you to freely customize and match the colors of all special effects.
## Requirements
*   **OS**: Windows 10 / 11
*   **Architecture**: x64
*   **Graphics**: OpenGL 3.3 support
## 小巧思
*   默认对隐藏光标的状态隐藏特效，游戏例如 Minecraft 无需（其实暂时无法）设置白名单即可完美使用。
*   0.1% 的极低 CPU 占用  85MB 的极低 RAM 占用 388KB的主程序体积
*   休眠约 0% 占用
*   无需安装，随意存放在一个角落，通过配置程序设置开机启动即可
这个项目主要用于我自己练习，并没有很多时间丰富控制面板的功能，但 C 部分已经是完整的了。
实际上体感差距和 BASpark 差距不大，特别是你的电脑比较强劲的情况下。
因此再次推荐原项目 [BASpark](https://github.com/DoomVoss/BASpark)。

附上13thGen i7-1360P/核显笔记本/3cps/60fps下的任务管理器对比
BAFlare:
![BAFlare](./images/BAFlare.PNG)
原版WebView
![BAFlareWebView](./images/WebView.PNG)

## Credits & License
*   Inspired by the original [BASpark](https://github.com/DoomVoss/BASpark).
*   Visual style based on *Blue Archive* (Nexon / Yostar).
*   Licensed under the **MIT License**.
More settings...
![Select Color](./images/1.PNG)
