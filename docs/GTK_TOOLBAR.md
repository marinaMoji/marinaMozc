# marinaMozc GTK Toolbar

Floating GTK toolbar in **marinaMoji style**: white (or dark) rounded panel, **bottom-right** of the primary monitor where possible, **draggable** by clicking on the background (not on buttons). It avoids grabbing focus to reduce flashing/vibration.

## Behaviour

- **Logo** – marinaMoji-style long logo (`logo_long_light.svg`) on the left; loaded from the same icon dir as the toolbar icons.
- **Position** – Window is **TOPLEVEL** with **UTILITY** type hint so the compositor does not show it in the dock/taskbar; **keep_above** keeps it on top without focus grab. Position:
  - **X11**: Bottom-right of the primary monitor’s work area (margin from edges). Applied in `configure-event` once the window has a real size.
  - **Wayland + gtk-layer-shell** (optional): Anchored bottom-right via layer-shell; `KEYBOARD_MODE_NONE`. If you build with layer-shell, it is used; otherwise the next case applies.
  - **Wayland without layer-shell** (e.g. GNOME): Bottom-right via `MoveToBottomRight()` in `configure-event` or `map-event`, same as marinaMoji. User can drag; position is restored on next show (window is reused).
- **Schema (input mode)** – Label: あ (Hiragana), ア (Katakana), 万 (Manyōshū), _A / Ａ (half/full ASCII), _ｱ (half Katakana), A (Direct).
- **Shin/Kyū** – Toggle traditional (kyūjitai) / modern (shinjitai) kanji (same as Ctrl+Shift+F). Checked = traditional.
- **Odoriji** – Opens the odoriji palette (same as Ctrl+Shift+2).
- **Half/Full** – Toggles half-width / full-width (same as Ctrl+Shift+3). Only active in ASCII modes; label shows "Half", "Full", or "Half/Full".

The toolbar is shown when the engine has focus and hidden on focus loss. State is updated from engine output after each command.

## Styling

- Transparent window background with RGBA visual for rounded corners.
- Content area: `border-radius: 10px`, white background `rgba(255,255,255,0.97)`, navy text `#203758`, light border.
- Buttons use transparent background (no theme shadows).

## Build

On Linux the toolbar is **enabled by default**: `ibus_mozc_lib` is built with `MOZC_HAVE_GTK_TOOLBAR` and links GTK3 and librsvg-2. Install GTK3 and librsvg development packages (e.g. `libgtk-3-dev`, `librsvg-2-dev`). Toolbar SVG icons in `src/unix/ibus/toolbar_icons/*.svg` are included and installed to the IBus icon dir (e.g. `/usr/share/ibus-marinamozc/`). Icons fall back to text labels if loading fails.

### Optional: Wayland layer-shell

For **Wayland** (Hyprland, Sway, etc.), pinned bottom-right and no focus issues require **gtk-layer-shell**. To enable:

1. Install the development package so the header and library are available:
   - **Arch / CachyOS:** `sudo pacman -S gtk-layer-shell`
   - **Debian / Ubuntu:** `sudo apt install libgtk-layer-shell-dev`
   - Or use pkg-config name `gtk-layer-shell-0` to find the package on your distro.
2. From the mozc **src** directory, build with (use `--config=linux` so the build uses C++20 and the Linux toolchain):  
   `bazel build --config=linux --define=gtk_layer_shell=1 //unix/ibus:ibus_mozc`  
   (If your workspace root is different, use the path that matches your `//unix/ibus` package.)

If you **do not** install gtk-layer-shell, build **without** the define (toolbar still works on X11 and on Wayland with compositor-defined position):
   `bazel build --config=linux //unix/ibus:ibus_mozc`

Without `gtk_layer_shell`, the toolbar uses the same strategy as marinaMoji on Wayland: TOPLEVEL + DOCK + explicit bottom-right positioning, so it works the same with or without layer-shell.

## Implementation

- **`mozc_toolbar.{h,cc}`** – GTK **TOPLEVEL** window, type hint **UTILITY** (not shown in dock/taskbar), accept_focus/focus_on_map/can_focus false. Runtime detection: **IsWayland()** / **IsX11()**. **X11 and Wayland (no layer-shell)**: `configure-event` or `map-event` → `MoveToBottomRight()` once size is known; margin from workarea uses `kToolbarMargin`. **Wayland with layer-shell**: `SetupLayerShellBottomRight()` anchors bottom-right and sets `KEYBOARD_MODE_NONE`. Drag: `button-press` on non-button widgets calls `gtk_window_begin_move_drag`. Logo and icons from `GetIconPath()`; `MozcToolbarUpdate(output)` syncs schema, traditional-kanji, half/full.
- **`mozc_engine.{h,cc}`** – `MozcToolbarShow(engine)`, `MozcToolbarHide()`, `MozcToolbarUpdate(output)`, `SendToolbarSessionCommand(type)`.
