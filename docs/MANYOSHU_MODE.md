# 万葉集 (Manyōshū) composition mode

**万葉集** is an optional composition mode that behaves like Hiragana input but is aimed at working with old Japanese text (e.g. Man’yōgana):

- **Input:** Same as Hiragana (romaji → kana conversion).
- **Preedit:** All preedit is shown in **full-width katakana** (converted from the internal hiragana).
- **Candidates:** All candidate text is shown in **katakana or kanji** (no hiragana). Any hiragana in a candidate is converted to katakana. Duplicates are then removed by normalized value: only the first occurrence of each resulting string is kept (e.g. "このもの" and "コノモノ" both become "コノモノ", and only one entry is shown).

## How to use

1. **From the IME menu:** Choose **万葉集** (or “万”) in the composition mode menu (same place as Hiragana, Katakana, etc.).
2. **Right Shift:** Press **Right Shift** alone to toggle between 万葉集 and Hiragana in Precomposition, Composition, and Conversion.

## Keybinding

- **Right Shift** → `ToggleManyoshuHiragana` (Precomposition, Composition, Conversion)

Defined in all keymap TSV files (e.g. `ms-ime.tsv`). If Right Shift alone is not delivered by your environment, you can change the binding in the keymap data.

## Technical notes

- Internally the composer stays in Hiragana; when the mode is MANYOSHU, the displayed preedit is converted to katakana, candidate values are converted to katakana (hiragana → katakana), and candidates are deduplicated by that normalized value.
- The mode is stored in session state and reported in `Output::status::mode()` so the UI (toolbar, IBus) can show 万 or “万葉集”.
