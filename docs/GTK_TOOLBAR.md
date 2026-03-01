# marinaMozc GTK Toolbar

Floating GTK toolbar that reflects engine state and provides quick toggles:

- **Schema (input mode)** – Label showing current mode: あ (Hiragana), ア (Katakana), _A / Ａ (half/full ASCII), _ｱ (half Katakana), A (Direct).
- **Shin/Kyū** – Toggle for traditional (kyūjitai) / modern (shinjitai) kanji (same as Ctrl+Shift+F). Checked = traditional.
- **Odoriji** – Opens the odoriji palette (same as Ctrl+Shift+2).
- **Half/Full** – Toggles half-width / full-width (same as Ctrl+Shift+3). Label shows "Half", "Full", or "Half/Full" depending on mode.

The toolbar is shown when the engine has focus and hidden on focus loss. State (schema, shin/kyū, half/full) is updated from engine output after each command, so it stays in sync with the IBus property panel.

## Build

On Linux the toolbar is **enabled by default**: `ibus_mozc_lib` is built with `MOZC_HAVE_GTK_TOOLBAR` and links GTK3. You need GTK3 development packages (e.g. `libgtk-3-dev` on Debian/Ubuntu).

If the build fails for missing GTK or include paths, install the packages or adjust `copts`/`linkopts` in `src/unix/ibus/BUILD.bazel` (e.g. add `-I/usr/lib/$(arch)-linux-gnu/glib-2.0/include` for glib on some systems).

## Implementation

- **`mozc_toolbar.{h,cc}`** – GTK window with schema label, Shin/Kyū toggle, Odoriji and Half/Full buttons. `MozcToolbarUpdate(output)` updates the UI from `output.status().mode()` and `output.config().use_traditional_kanji()`.
- **`mozc_engine.{h,cc}`** – `current_engine_` stores the last focused engine; `SendToolbarSessionCommand(type)` sends the session command and calls `UpdateAll`; `UpdateAll` calls `MozcToolbarUpdate(output)` so the toolbar reflects current state.
- **BUILD.bazel** – For Linux/oss_linux: `MOZC_HAVE_GTK_TOOLBAR` define, GTK include `copts`, and GTK `linkopts`.
