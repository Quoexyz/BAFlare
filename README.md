# BAFlare
A lightweight, native Windows mouse effect tool that reconstructs the Blue Archive UI style using C, SDL2, and Modern OpenGL.WITHOUT WebView; Inspired by BASpark
## Overview
**BAFlare** is a high-performance rewrite of the original [BASpark](https://github.com/DoomVoss/BASpark).
The original version used a "WPF + WebView2" architecture. While effective, it relied on Webview. This project replaces that stack entirely with a pure **C / SDL2 / OpenGL 3.3 Core** implementation.
### Key Changes
*   **No WebView**: Removes the dependency on the Edge/WebView2 runtime.
*   **Native Performance**: Utilizes a custom Batch Renderer and modern OpenGL shaders for rendering, resulting in lower memory usage and faster startup.
*   **GPU Optimized**: Implements VBO/VAO streaming and vertex compression techniques.
## Features
*   **Blue Archive Style**: Recreates the iconic click sparks, shockwaves, and movement trails.
*   **Modern OpenGL**: Uses GLSL shaders and a custom batch rendering system (no legacy fixed-function pipeline).
*   **Frame Rate Independent**: Animation speed remains consistent regardless of frame rate.
*   **Lightweight**: Minimal CPU and GPU usage when idle.
## Requirements
*   **OS**: Windows 10 / 11
*   **Architecture**: x64
*   **Graphics**: OpenGL 3.3 support

## Credits & License
*   Inspired by the original [BASpark](https://github.com/DoomVoss/BASpark).
*   Visual style based on *Blue Archive* (Nexon / Yostar).
*   Licensed under the **MIT License**.
