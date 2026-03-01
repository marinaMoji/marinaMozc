# Odoriji (Iteration Marks) Palette in Mozc

This document describes the **odoriji palette** feature added to this Mozc fork. It provides a candidate-window–based way to choose and insert iteration marks (繰り返し記号), similar to marinaMoji’s odoriji selection.

To keep **upstream merges** easy, odoriji logic is isolated in a single module; `Session` only has thin delegation.

## Behaviour

- **Ctrl+Shift+1** (command `InsertOdorijiDefault`): Inserts the **session default** odoriji (e.g. 々) without opening the palette. Available in Precomposition, Composition, and Conversion. The default is index 0 (々) until the user selects from the palette.
- **Ctrl+Shift+2** (command `ShowOdorijiPalette`): Opens the odoriji palette. Available in Precomposition, Composition, and Conversion.
- **Odoriji (iteration marks) property:** In the engine’s property menu (IBus language bar / toolbar), an **“Odoriji (iteration marks)”** button opens the same palette as Ctrl+Shift+2.
- **UI:** The normal candidate window is replaced by a fixed list of 8 odoriji:
  - 々 (ideographic iteration)
  - ゝ ゞ (hiragana iteration, voiced)
  - ヽ ヾ (katakana iteration, voiced)
  - 〻 〱 〲 (vertical iteration marks)
- **Selection:** When the user selects an odoriji (by **1–8**, **Enter**, or **click**), that choice is saved as the **session default** for Ctrl+Shift+1.
  - **1–8:** Insert the corresponding odoriji, set it as session default, and close the palette.
  - **Enter:** Insert the focused odoriji, set it as session default, and close the palette.
  - **Up/Down (or Virtual Up/Down):** Change the focused candidate.
  - **Escape:** Close the palette without inserting (default for Ctrl+Shift+1 is unchanged).
- **Click:** The client can send `SUBMIT_CANDIDATE` with the chosen candidate `id` (0–7); the session commits that odoriji, sets it as session default, and closes the palette.

## Implementation (less invasive)

- **Module:** All odoriji logic lives in `session/odoriji_palette.{h,cc}`. `OdorijiPalette` is a static-API helper: `GetCharacter(index)`, `Show()`, `HandleKey(..., session_default_index)`, `OverlayOutput()`, `TryCommitCandidate(..., session_default_index)`. When the user commits from the palette, the optional `session_default_index` is set so Session can store it.
- **Session touch points:** `Session` keeps three members (`odoriji_palette_visible_`, `odoriji_focused_index_`, `odoriji_default_index_`) and delegates in five places:
  - **SendKey:** if palette visible and `OdorijiPalette::HandleKey(..., &odoriji_default_index_)` returns true → `Output(command)`; return true.
  - **Output:** if palette visible → `OdorijiPalette::OverlayOutput(output, focused_index)`.
  - **CommitCandidate:** if `OdorijiPalette::TryCommitCandidate(command, &visible, &odoriji_default_index_)` returns true → `Output(command)`; return true.
  - **ShowOdorijiPalette:** `OdorijiPalette::Show(&visible, &focused_index)`; `Output(command)`.
  - **InsertOdorijiDefault:** set `output.result` to `OdorijiPalette::GetCharacter(odoriji_default_index_)`; `OutputFromState(command)`; return true.
- **Keymap/TSV:** Two commands: `ShowOdorijiPalette` (Ctrl+Shift+2) and `InsertOdorijiDefault` (Ctrl+Shift+1). Same states as before.
- **IBus property:** The property **“Odoriji (iteration marks)”** (`Option.OdorijiPalette`) sends `SessionCommand::SHOW_ODORIJI_PALETTE`; the engine applies the output so the palette appears (same as pressing Ctrl+Shift+2).

## Keymap

- **Ctrl+Shift+1** → `InsertOdorijiDefault` (insert session default odoriji), **Ctrl+Shift+2** → `ShowOdorijiPalette` (open palette), and **Ctrl+Shift+3** → `ToggleFullHalfWidth` (toggle half/full width) are bound in the same states in:
  - `ms-ime.tsv`, `chromeos.tsv`, `mobile.tsv`: Precomposition, Composition, Conversion
  - `atok.tsv`, `kotoeri.tsv`: Conversion only (Ctrl+Shift+3 where Ctrl+Shift+2 exists)

## Files

- **`session/odoriji_palette.{h,cc}`:** All odoriji data and logic (constants, fill candidate window, key handling, overlay, commit). Session never includes odoriji implementation details.
- **`session/session.h`, `session/session.cc`:** Two member variables and four short delegation sites (see above). No odoriji strings or control flow beyond “call module and return”.
- **`session/keymap.h`, `session/keymap.cc`:** Command `SHOW_ODORIJI_PALETTE` and registration.
- **`data/keymap/*.tsv`:** Keybinding Ctrl+Shift+2 → ShowOdorijiPalette.
- **`session/BUILD.bazel`:** New target `odoriji_palette`; `session` depends on it.

## Relation to marinaMoji

In marinaMoji, odoriji selection is done via:

- **Ctrl+Shift+2:** show an IBus lookup table with the same 8 marks; choose with 1–8, Enter, arrows, Space; selection sets the session default and commits.
- **Toolbar:** a floating toolbar has an “odoriji” button that opens a popup of buttons; clicking one inserts that mark and sets the session default.

This Mozc implementation does not add a separate toolbar; it reuses the existing candidate window and keybindings so that the same workflow (shortcut → candidate list → key or click to choose) works with the standard Mozc/IBus candidate UI.
