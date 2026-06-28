"""
Spark Cursor Effect - Settings Controller
- Saves RGB values directly to registry
- Auto-start toggle for BAFlare.exe
- Process kill button for BAFlare.exe
- Window automatically centered on startup (ensured fully visible)
All UI strings and comments are in English.
"""

import customtkinter as ctk
import winreg
import subprocess
import os, sys
from pathlib import Path
from tkinter import colorchooser

# ========== Color presets (matches config.h) ==========
COLOR_PRESETS = [
    {"name": "Blue", "color": "#2DAFFF", "rgb": (45, 175, 255), "desc": "Deep Blue"},
    {"name": "Red", "color": "#FF4545", "rgb": (255, 69, 69), "desc": "Passionate Red"},
    {"name": "Green", "color": "#45FF89", "rgb": (69, 255, 137), "desc": "Vibrant Green"},
    {"name": "Yellow", "color": "#FFDF45", "rgb": (255, 223, 69), "desc": "Bright Yellow"},
    {"name": "Purple", "color": "#C345FF", "rgb": (195, 69, 255), "desc": "Mystic Purple"},
    {"name": "Cyan", "color": "#00FFFF", "rgb": (0, 255, 255), "desc": "Crystal Cyan"},
    {"name": "Orange", "color": "#FF8C00", "rgb": (255, 140, 0), "desc": "Energetic Orange"},
    {"name": "Pink", "color": "#FF69B4", "rgb": (255, 105, 180), "desc": "Romantic Pink"},
]

CUSTOM_INDEX = 8  # custom color card index (0~7 are presets)

REG_PATH = r"Software\SparkCursorEffect"
RUN_REG_PATH = r"Software\Microsoft\Windows\CurrentVersion\Run"
AUTOSTART_KEY = "BAFlare"
EXE_NAME = "BAFlare.exe"


class ModernSlider(ctk.CTkFrame):
    """Custom modern slider component"""
    def __init__(self, master, title, min_val, max_val, default, unit="", resolution=1, **kwargs):
        super().__init__(master, fg_color="transparent", **kwargs)

        self.min_val = min_val
        self.max_val = max_val
        self.unit = unit
        self.resolution = resolution
        self._value = default

        self.title_frame = ctk.CTkFrame(self, fg_color="transparent")
        self.title_frame.pack(fill="x", pady=(5, 0))

        self.title_label = ctk.CTkLabel(
            self.title_frame,
            text=title,
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            text_color=("#1f6aa5", "#4a9eff")
        )
        self.title_label.pack(side="left")

        self.value_label = ctk.CTkLabel(
            self.title_frame,
            text=f"{default:.1f}{unit}",
            font=ctk.CTkFont(family="Consolas", size=13, weight="bold"),
            text_color=("#2a8a4a", "#3fd170")
        )
        self.value_label.pack(side="right")

        self.slider = ctk.CTkSlider(
            self,
            from_=min_val,
            to=max_val,
            number_of_steps=int((max_val - min_val) / resolution),
            command=self._on_slider_change,
            progress_color=("#3b8ed0", "#1f6aa5"),
            button_color=("#1f6aa5", "#3b8ed0"),
            button_hover_color=("#144870", "#2a7db8")
        )
        self.slider.set(default)
        self.slider.pack(fill="x", pady=(5, 5))

        self.range_frame = ctk.CTkFrame(self, fg_color="transparent")
        self.range_frame.pack(fill="x")
        ctk.CTkLabel(
            self.range_frame,
            text=f"{min_val:.1f}",
            font=ctk.CTkFont(family="Segoe UI", size=10),
            text_color=("gray50", "gray60")
        ).pack(side="left")
        ctk.CTkLabel(
            self.range_frame,
            text=f"{max_val:.1f}",
            font=ctk.CTkFont(family="Segoe UI", size=10),
            text_color=("gray50", "gray60")
        ).pack(side="right")

    def _on_slider_change(self, value):
        self._value = round(value / self.resolution) * self.resolution
        self.value_label.configure(text=f"{self._value:.1f}{self.unit}")

    def get(self):
        return self._value

    def set(self, value):
        self._value = value
        self.slider.set(value)
        self.value_label.configure(text=f"{value:.1f}{self.unit}")


class ColorCard(ctk.CTkFrame):
    """Color card for preset colors"""
    def __init__(self, master, preset, index, selected=False, on_click=None, **kwargs):
        super().__init__(master, fg_color="transparent", **kwargs)

        self.preset = preset
        self.index = index
        self.on_click = on_click
        self._selected = selected

        self.card = ctk.CTkFrame(
            self,
            height=90,
            corner_radius=12,
            fg_color=("#f0f0f0", "#2b2b2b") if not selected else ("#d0e8ff", "#1a4a6e"),
            border_width=3 if selected else 1,
            border_color=(preset["color"], preset["color"]) if selected else ("gray70", "gray40")
        )
        self.card.pack(fill="both", expand=True, padx=3, pady=3)
        self.card.pack_propagate(False)

        self.color_frame = ctk.CTkFrame(
            self.card,
            width=35,
            height=35,
            corner_radius=18,
            fg_color=preset["color"]
        )
        self.color_frame.pack(pady=(10, 3))

        self.name_label = ctk.CTkLabel(
            self.card,
            text=preset["name"],
            font=ctk.CTkFont(family="Segoe UI", size=11, weight="bold")
        )
        self.name_label.pack()

        for widget in [self, self.card, self.color_frame, self.name_label]:
            widget.bind("<Button-1>", self._handle_click)
            widget.bind("<Enter>", self._on_enter)
            widget.bind("<Leave>", self._on_leave)

    def _handle_click(self, event):
        if self.on_click:
            self.on_click(self.index)

    def _on_enter(self, event):
        if not self._selected:
            self.card.configure(fg_color=("#e0e8f0", "#3a3a3a"))

    def _on_leave(self, event):
        if not self._selected:
            self.card.configure(fg_color=("#f0f0f0", "#2b2b2b"))

    def select(self):
        self._selected = True
        self.card.configure(
            fg_color=("#d0e8ff", "#1a4a6e"),
            border_width=3,
            border_color=(self.preset["color"], self.preset["color"])
        )

    def deselect(self):
        self._selected = False
        self.card.configure(
            fg_color=("#f0f0f0", "#2b2b2b"),
            border_width=1,
            border_color=("gray70", "gray40")
        )


class CustomColorCard(ctk.CTkFrame):
    """Custom color card for user-defined RGB"""
    def __init__(self, master, rgb, on_click=None, **kwargs):
        super().__init__(master, fg_color="transparent", **kwargs)
        self.rgb = rgb
        self.on_click = on_click
        self._selected = False

        self.card = ctk.CTkFrame(
            self,
            height=90,
            corner_radius=12,
            fg_color=("#f0f0f0", "#2b2b2b"),
            border_width=1,
            border_color=("gray70", "gray40")
        )
        self.card.pack(fill="both", expand=True, padx=3, pady=3)
        self.card.pack_propagate(False)

        self.color_frame = ctk.CTkFrame(
            self.card,
            width=35,
            height=35,
            corner_radius=18,
            fg_color=self._rgb_to_hex(rgb)
        )
        self.color_frame.pack(pady=(10, 3))

        self.name_label = ctk.CTkLabel(
            self.card,
            text="Custom",
            font=ctk.CTkFont(family="Segoe UI", size=11, weight="bold")
        )
        self.name_label.pack()

        for widget in [self, self.card, self.color_frame, self.name_label]:
            widget.bind("<Button-1>", self._handle_click)
            widget.bind("<Enter>", self._on_enter)
            widget.bind("<Leave>", self._on_leave)

    def _rgb_to_hex(self, rgb):
        return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"

    def _handle_click(self, event):
        if self.on_click:
            self.on_click()

    def _on_enter(self, event):
        if not self._selected:
            self.card.configure(fg_color=("#e0e8f0", "#3a3a3a"))

    def _on_leave(self, event):
        if not self._selected:
            self.card.configure(fg_color=("#f0f0f0", "#2b2b2b"))

    def select(self):
        self._selected = True
        self.card.configure(
            fg_color=("#d0e8ff", "#1a4a6e"),
            border_width=3,
            border_color=(self._rgb_to_hex(self.rgb), self._rgb_to_hex(self.rgb))
        )

    def deselect(self):
        self._selected = False
        self.card.configure(
            fg_color=("#f0f0f0", "#2b2b2b"),
            border_width=1,
            border_color=("gray70", "gray40")
        )

    def update_color(self, rgb):
        self.rgb = rgb
        self.color_frame.configure(fg_color=self._rgb_to_hex(rgb))
        if self._selected:
            self.card.configure(border_color=(self._rgb_to_hex(rgb), self._rgb_to_hex(rgb)))


class SparkSettingsApp(ctk.CTk):
    """Main application window"""
    def __init__(self):
        super().__init__()

        self.title("BA Flare Settings")
        self.geometry("620x680")  # Compact size
        self.resizable(False, False)

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self._detect_system_theme()
        self.custom_rgb = (255, 255, 255)
        self.selected_color = 0
        self._create_ui()
        self._load_config()

        # Center window after UI is fully drawn and geometry is stable
        self.after(50, self._center_window)

    def _center_window(self):
        """Manually center the window on screen, ensuring it's fully visible."""
        self.update_idletasks()  # Ensure window dimensions are finalized

        # Get screen dimensions
        screen_width = self.winfo_screenwidth()
        screen_height = self.winfo_screenheight()

        # Get window dimensions
        win_width = self.winfo_width()
        win_height = self.winfo_height()

        # Calculate position (top-left corner)
        x = (screen_width - win_width) // 2
        y = (screen_height - win_height) // 2

        # Ensure window is not pushed off-screen (especially top/left)
        # If window is larger than screen, set to 0,0
        x = max(0, x)
        y = max(0, y)

        # If bottom edge goes beyond screen, adjust y upwards
        if y + win_height > screen_height:
            y = max(0, screen_height - win_height)
        if x + win_width > screen_width:
            x = max(0, screen_width - win_width)

        self.geometry(f"+{x}+{y}")

    def _detect_system_theme(self):
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize")
            value, _ = winreg.QueryValueEx(key, "AppsUseLightTheme")
            winreg.CloseKey(key)
            if value == 0:
                ctk.set_appearance_mode("dark")
            else:
                ctk.set_appearance_mode("light")
        except:
            pass

    def _create_ui(self):
        """Build the user interface"""
        self.main_frame = ctk.CTkScrollableFrame(
            self,
            fg_color="transparent",
            scrollbar_button_color=("gray70", "gray30"),
            scrollbar_button_hover_color=("gray60", "gray40")
        )
        self.main_frame.pack(fill="both", expand=True, padx=20, pady=20)

        # ===== Title =====
        self.title_frame = ctk.CTkFrame(self.main_frame, fg_color="transparent")
        self.title_frame.pack(fill="x", pady=(0, 15))

        ctk.CTkLabel(
            self.title_frame,
            text="🎨 Mouse Spark Settings",
            font=ctk.CTkFont(family="Segoe UI", size=26, weight="bold"),
            text_color=("#1f6aa5", "#4a9eff")
        ).pack()
        ctk.CTkLabel(
            self.title_frame,
            text="Settings are saved to registry. Launch effect to apply.",
            font=ctk.CTkFont(family="Segoe UI", size=12),
            text_color=("gray50", "gray60")
        ).pack(pady=(5, 0))

        # ===== Color selection =====
        self.color_section = ctk.CTkFrame(
            self.main_frame,
            corner_radius=15,
            fg_color=("#f5f5f5", "#1f1f1f")
        )
        self.color_section.pack(fill="x", pady=(0, 15), padx=5)

        ctk.CTkLabel(
            self.color_section,
            text="🌈 Select Color Scheme",
            font=ctk.CTkFont(family="Segoe UI", size=16, weight="bold"),
            text_color=("#1f6aa5", "#4a9eff")
        ).pack(pady=(15, 5), padx=15, anchor="w")

        self.colors_frame = ctk.CTkFrame(self.color_section, fg_color="transparent")
        self.colors_frame.pack(fill="x", padx=10, pady=10)
        for col in range(3):
            self.colors_frame.grid_columnconfigure(col, weight=1, uniform="col")

        self.color_cards = []
        row, col = 0, 0
        for i, preset in enumerate(COLOR_PRESETS):
            card = ColorCard(
                self.colors_frame,
                preset,
                i,
                selected=(i == 0),
                on_click=self._on_color_selected
            )
            card.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)
            self.color_cards.append(card)
            col += 1
            if col == 3:
                col = 0
                row += 1

        self.custom_card = CustomColorCard(
            self.colors_frame,
            rgb=self.custom_rgb,
            on_click=self._open_color_picker
        )
        self.custom_card.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)

        for r in range(row + 1):
            self.colors_frame.grid_rowconfigure(r, weight=1)

        # ===== Parameters =====
        self.params_section = ctk.CTkFrame(
            self.main_frame,
            corner_radius=15,
            fg_color=("#f5f5f5", "#1f1f1f")
        )
        self.params_section.pack(fill="x", pady=(0, 15), padx=5)

        ctk.CTkLabel(
            self.params_section,
            text="⚙️ Adjust Parameters",
            font=ctk.CTkFont(family="Segoe UI", size=16, weight="bold"),
            text_color=("#1f6aa5", "#4a9eff")
        ).pack(pady=(15, 5), padx=15, anchor="w")

        self.scale_slider = ModernSlider(
            self.params_section,
            title="📏 Particle Size",
            min_val=0.5,
            max_val=5.0,
            default=1.5,
            unit="x",
            resolution=0.1
        )
        self.scale_slider.pack(fill="x", padx=20, pady=(10, 5))

        self.opacity_slider = ModernSlider(
            self.params_section,
            title="💫 Opacity",
            min_val=0.1,
            max_val=1.0,
            default=1.0,
            unit="",
            resolution=0.05
        )
        self.opacity_slider.pack(fill="x", padx=20, pady=5)

        self.speed_slider = ModernSlider(
            self.params_section,
            title="⚡ Animation Speed",
            min_val=0.2,
            max_val=2.0,
            default=1.0,
            unit="x",
            resolution=0.1
        )
        self.speed_slider.pack(fill="x", padx=20, pady=(5, 15))

        # ===== Quick Actions (compact 2-row layout) =====
        self.quick_section = ctk.CTkFrame(
            self.main_frame,
            corner_radius=15,
            fg_color=("#f5f5f5", "#1f1f1f")
        )
        self.quick_section.pack(fill="x", pady=(0, 5), padx=5)

        ctk.CTkLabel(
            self.quick_section,
            text="🚀 Quick Actions",
            font=ctk.CTkFont(family="Segoe UI", size=16, weight="bold"),
            text_color=("#1f6aa5", "#4a9eff")
        ).pack(pady=(15, 10), padx=15, anchor="w")

        # ---- Row 1: Auto-start switch (left) and Stop button (right) ----
        self.row1_frame = ctk.CTkFrame(self.quick_section, fg_color="transparent")
        self.row1_frame.pack(fill="x", padx=20, pady=(0, 10))
        self.row1_frame.grid_columnconfigure(0, weight=1)
        self.row1_frame.grid_columnconfigure(1, weight=1)

        self.autostart_switch = ctk.CTkSwitch(
            self.row1_frame,
            text="Auto-start BAFlare",
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            command=self._toggle_autostart
        )
        self.autostart_switch.grid(row=0, column=0, sticky="w", padx=(0, 10))

        self.autostart_status = ctk.CTkLabel(
            self.row1_frame,
            text="",
            font=ctk.CTkFont(family="Segoe UI", size=11),
            text_color=("gray50", "gray60")
        )
        self.autostart_status.grid(row=0, column=0, sticky="e", padx=(0, 10))

        self.stop_button = ctk.CTkButton(
            self.row1_frame,
            text="⏹ Stop Effect",
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            height=35,
            corner_radius=10,
            fg_color=("#b84a4a", "#b84a4a"),
            hover_color=("#8a3a3a", "#8a3a3a"),
            command=self._stop_effect
        )
        self.stop_button.grid(row=0, column=1, sticky="e", padx=(10, 0))

        # ---- Row 2: Reset, Launch, Save (3 buttons evenly spaced) ----
        self.row2_frame = ctk.CTkFrame(self.quick_section, fg_color="transparent")
        self.row2_frame.pack(fill="x", padx=20, pady=(0, 15))
        for col in range(3):
            self.row2_frame.grid_columnconfigure(col, weight=1, uniform="btn")

        ctk.CTkButton(
            self.row2_frame,
            text="🔄 Reset Defaults",
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            height=38,
            corner_radius=10,
            fg_color=("gray60", "gray40"),
            hover_color=("gray50", "gray50"),
            command=self._reset_defaults
        ).grid(row=0, column=0, sticky="ew", padx=3)

        ctk.CTkButton(
            self.row2_frame,
            text="▶️ Launch Effect",
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            height=38,
            corner_radius=10,
            fg_color=("#2a8a4a", "#2a8a4a"),
            hover_color=("#1f6a3a", "#1f6a3a"),
            command=self._launch_effect
        ).grid(row=0, column=1, sticky="ew", padx=3)

        ctk.CTkButton(
            self.row2_frame,
            text="💾 Save Settings",
            font=ctk.CTkFont(family="Segoe UI", size=13, weight="bold"),
            height=38,
            corner_radius=10,
            fg_color=("#3b8ed0", "#1f6aa5"),
            hover_color=("#2a7db8", "#144870"),
            command=self._save_and_exit
        ).grid(row=0, column=2, sticky="ew", padx=3)

        # ===== Status bar =====
        self.status_frame = ctk.CTkFrame(self.main_frame, fg_color="transparent")
        self.status_frame.pack(fill="x", pady=(10, 0))
        self.status_label = ctk.CTkLabel(
            self.status_frame,
            text="💡 Tip: Save settings then launch effect to apply.",
            font=ctk.CTkFont(family="Segoe UI", size=11),
            text_color=("gray50", "gray60")
        )
        self.status_label.pack()

    # ---------- Color selection ----------
    def _select_color(self, index):
        self.selected_color = index
        for card in self.color_cards:
            card.deselect()
        self.custom_card.deselect()
        if index == CUSTOM_INDEX:
            self.custom_card.select()
        else:
            self.color_cards[index].select()

    def _on_color_selected(self, index):
        self._select_color(index)

    # ---------- Custom color picker ----------
    def _open_color_picker(self):
        initial_color = self._rgb_to_hex(self.custom_rgb)
        result = colorchooser.askcolor(
            color=initial_color,
            title="Select Custom Color"
        )
        if result is not None:
            rgb_tuple, hex_str = result
            r, g, b = [int(c) for c in rgb_tuple]
            self.custom_rgb = (r, g, b)
            self.custom_card.update_color(self.custom_rgb)
            self._select_color(CUSTOM_INDEX)
            self._update_status(f"🎨 Custom color set to {hex_str}")

    def _rgb_to_hex(self, rgb):
        return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"

    # ---------- Auto-start management ----------
    def _get_exe_path(self):
        # 修正打包后的路径获取
        import sys
        if getattr(sys, "frozen", False):
            base_dir = Path(sys.executable).parent
        else:
            base_dir = Path(__file__).parent
        return base_dir / EXE_NAME

    def _set_autostart(self, enabled):
        exe_path = self._get_exe_path()
        if enabled and not exe_path.exists():
            self._update_status("⚠️ BAFlare.exe not found, cannot enable auto-start")
            return False

        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_REG_PATH, 0, winreg.KEY_ALL_ACCESS)
            if enabled:
                winreg.SetValueEx(key, AUTOSTART_KEY, 0, winreg.REG_SZ, str(exe_path))
                self.autostart_status.configure(text="(enabled)", text_color="#2a8a4a")
                self._update_status("✅ Auto-start enabled")
            else:
                try:
                    winreg.DeleteValue(key, AUTOSTART_KEY)
                except FileNotFoundError:
                    pass
                self.autostart_status.configure(text="(disabled)", text_color="gray60")
                self._update_status("Auto-start disabled")
            winreg.CloseKey(key)
            self._save_autostart_flag(enabled)
            return True
        except Exception as e:
            self._update_status(f"❌ Failed to modify auto-start: {e}")
            return False

    def _save_autostart_flag(self, enabled):
        try:
            key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_PATH)
            winreg.SetValueEx(key, "AutoStart", 0, winreg.REG_DWORD, 1 if enabled else 0)
            winreg.CloseKey(key)
        except:
            pass

    def _load_autostart_flag(self):
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_PATH, 0, winreg.KEY_READ)
            value, _ = winreg.QueryValueEx(key, "AutoStart")
            winreg.CloseKey(key)
            return value == 1
        except FileNotFoundError:
            return False
        except:
            return False

    def _toggle_autostart(self):
        enabled = self.autostart_switch.get() == 1
        if enabled:
            exe_path = self._get_exe_path()
            if not exe_path.exists():
                self.autostart_switch.deselect()
                self._update_status("⚠️ BAFlare.exe not found, cannot enable")
                return
        self._set_autostart(enabled)

    # ---------- Process control ----------
    def _stop_effect(self):
        """Terminate all BAFlare.exe processes using taskkill"""
        try:
            result = subprocess.run(
                ["taskkill", "/f", "/im", EXE_NAME],
                capture_output=True,
                text=True,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            )
            if result.returncode == 0:
                self._update_status("⏹ BAFlare.exe terminated successfully")
            else:
                if "not found" in result.stderr.lower() or "no process" in result.stderr.lower():
                    self._update_status("ℹ️ BAFlare.exe is not running")
                else:
                    self._update_status(f"⚠️ Failed to terminate: {result.stderr.strip()}")
        except Exception as e:
            self._update_status(f"❌ Error stopping effect: {e}")

    # ---------- Config load/save ----------
    def _load_config(self):
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_PATH, 0, winreg.KEY_READ)

            # --- Load Color ---
            try:
                r, _ = winreg.QueryValueEx(key, "ColorR")
                g, _ = winreg.QueryValueEx(key, "ColorG")
                b, _ = winreg.QueryValueEx(key, "ColorB")
                loaded_rgb = (r, g, b)
                found = False
                for i, preset in enumerate(COLOR_PRESETS):
                    if preset["rgb"] == loaded_rgb:
                        self._select_color(i)
                        found = True
                        break
                if not found:
                    self.custom_rgb = loaded_rgb
                    self.custom_card.update_color(self.custom_rgb)
                    self._select_color(CUSTOM_INDEX)
            except FileNotFoundError:
                self._select_color(0)

            # --- Load Scale ---
            try:
                scale, _ = winreg.QueryValueEx(key, "Scale")
                self.scale_slider.set(scale / 10.0)
            except:
                pass

            # --- Load Opacity ---
            try:
                opacity, _ = winreg.QueryValueEx(key, "Opacity")
                self.opacity_slider.set(opacity / 100.0)
            except:
                pass

            # --- Load Speed ---
            try:
                speed, _ = winreg.QueryValueEx(key, "Speed")
                self.speed_slider.set(speed / 10.0)
            except:
                pass

            # --- Load Auto-start flag ---
            try:
                autostart = self._load_autostart_flag()
                if autostart:
                    self.autostart_switch.select()
                    self.autostart_status.configure(text="(enabled)", text_color="#2a8a4a")
                else:
                    self.autostart_switch.deselect()
                    self.autostart_status.configure(text="(disabled)", text_color="gray60")
            except:
                pass

            winreg.CloseKey(key)
            self._update_status("✅ Loaded saved config")
        except:
            self._update_status("ℹ️ First run, using defaults")

    def _save_config(self):
        try:
            key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_PATH)

            if self.selected_color == CUSTOM_INDEX:
                r, g, b = self.custom_rgb
            else:
                preset = COLOR_PRESETS[self.selected_color]
                r, g, b = preset["rgb"]

            winreg.SetValueEx(key, "ColorR", 0, winreg.REG_DWORD, r)
            winreg.SetValueEx(key, "ColorG", 0, winreg.REG_DWORD, g)
            winreg.SetValueEx(key, "ColorB", 0, winreg.REG_DWORD, b)

            winreg.SetValueEx(key, "Scale", 0, winreg.REG_DWORD, int(self.scale_slider.get() * 10))
            winreg.SetValueEx(key, "Opacity", 0, winreg.REG_DWORD, int(self.opacity_slider.get() * 100))
            winreg.SetValueEx(key, "Speed", 0, winreg.REG_DWORD, int(self.speed_slider.get() * 10))

            self._save_autostart_flag(self.autostart_switch.get() == 1)

            winreg.CloseKey(key)
            return True
        except Exception as e:
            print(f"Error saving config: {e}")
            return False

    def _reset_defaults(self):
        self.custom_rgb = (255, 255, 255)
        self.custom_card.update_color(self.custom_rgb)
        self._select_color(0)
        self.scale_slider.set(1.5)
        self.opacity_slider.set(1.0)
        self.speed_slider.set(1.0)

        self.autostart_switch.deselect()
        self._set_autostart(False)
        self.autostart_status.configure(text="(disabled)", text_color="gray60")

        self._update_status("🔄 Reset to defaults")

    def _launch_effect(self):
        if self._save_config():
            exe_path = self._get_exe_path()
            if exe_path.exists():
                subprocess.Popen([str(exe_path)], shell=True)
                self._update_status("✅ Effect launched!")
            else:
                self._update_status("⚠️ BAFlare.exe not found in current directory!")

    def _save_and_exit(self):
        if self._save_config():
            self._update_status("✅ Settings saved!")

    def _update_status(self, text):
        self.status_label.configure(text=text)


if __name__ == "__main__":
    app = SparkSettingsApp()
    app.mainloop()
