# Shin/Kyu (Traditional Kanji) Toggle and Toolbar Assets

This document describes the **Traditional kanji (Kyūjitai)** toggle added to this Mozc fork in the least invasive way: an IBus property and protocol support. marinaMoji-style toolbar icons are copied for future use.

## Behaviour

- **IBus property:** A toggle property **"Traditional kanji (Kyūjitai)"** appears in the engine’s property menu (same place as Input Mode and Tools). Clicking it toggles between modern (shinjitai) and traditional (kyūjitai) kanji conversion, same as **Ctrl+Shift+F**.
- **State:** The property shows checked when traditional kanji is on, unchecked when off. State is updated from server config after each toggle and when the engine receives output with config (e.g. after key events).
- **Schema / mode indicator:** The current input mode (Hiragana, Katakana, etc.) is already shown by the existing **Input Mode** property (the checked item in the menu). No extra schema indicator was added.

## Implementation (least invasive)

- **Protocol:** `commands.proto` – `SessionCommand::TOGGLE_TRADITIONAL_KANJI = 28`. No new keymap; the existing keybinding (e.g. Ctrl+Shift+F) still works via keymap.
- **Session:** `session.cc` – one new `SendCommand` case that calls `ToggleTraditionalKanji(command)`. `ToggleTraditionalKanji` now also fills `command->mutable_output()->mutable_config()` so clients get the updated config.
- **IBus:** `property_handler.{h,cc}` – one new toggle property `Option.TraditionalKanji`, appended to the root. On activate, the handler sends `SessionCommand::TOGGLE_TRADITIONAL_KANJI` and updates the property state from `output.config().use_traditional_kanji()`. `Update()` also syncs the property from `output.config()` when present.
- **Wrapper:** `ibus_wrapper.h` – default constructor for `IbusPropertyWrapper` (for the new property member). No floating window or GTK dependency.

## Toolbar icons (for future use)

marinaMoji toolbar assets for shin/kyu were copied so a future floating toolbar (if desired) can reuse them without adding code to this change:

- **Location:** `src/unix/ibus/toolbar_icons/`
- **Files:** `toolbar_shin_light.svg`, `toolbar_shin_dark.svg`, `toolbar_kyu_light.svg`, `toolbar_kyu_dark.svg` (from marinaMoji’s `icons/scalable/`).

A separate floating toolbar (like marinaMoji’s) would require a GTK dependency and extra UI code; the current implementation uses only the existing IBus property menu.

## Files

- **`protocol/commands.proto`:** `TOGGLE_TRADITIONAL_KANJI = 28`.
- **`session/session.cc`:** Case for `TOGGLE_TRADITIONAL_KANJI`; `ToggleTraditionalKanji` fills output config.
- **`unix/ibus/property_handler.{h,cc}`:** `prop_traditional_kanji_`, `AppendTraditionalKanjiPropertyToPanel()`, handling in `ProcessPropertyActivate` and `Update()`.
- **`unix/ibus/ibus_wrapper.h`:** Default constructor for `IbusPropertyWrapper`.
- **`unix/ibus/toolbar_icons/`:** Copied marinaMoji shin/kyu SVGs and README.
