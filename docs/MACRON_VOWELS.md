# Macron vowels (Ctrl+Alt+vowel)

You can type vowels with a macron (long-vowel mark) using the keyboard in **ASCII mode** (half-width or full-width alphanumeric) and in **Direct input**:

| Keys | Output |
|------|--------|
| Ctrl+Alt+a | ā |
| Ctrl+Alt+e | ē |
| Ctrl+Alt+i | ī |
| Ctrl+Alt+o | ō |
| Ctrl+Alt+u | ū |
| Ctrl+Alt+Shift+A | Ā |
| Ctrl+Alt+Shift+E | Ē |
| Ctrl+Alt+Shift+I | Ī |
| Ctrl+Alt+Shift+O | Ō |
| Ctrl+Alt+Shift+U | Ū |

- **Active in ASCII mode and in Direct input**; in Hiragana/Katakana mode the key is passed through as normal input.
- Available in all keymap layouts (MS-IME, ATOK, Kotoeri, Chrome OS, mobile).

## Alternatives when Ctrl+Alt is grabbed by the desktop

Many desktop environments (Cinnamon, GNOME, KDE, etc.) use **Ctrl+LeftAlt** for global shortcuts, so Ctrl+Alt+vowel may not reach the IME. The same macron output is also bound to:

- **Ctrl+RightAlt+vowel** — e.g. Ctrl+RightAlt+a → ā. Right Alt is often left alone by the DE.
- **Ctrl+AltGr+vowel** — same as Ctrl+RightAlt (AltGr is an alias for Right Alt in the keymap).

Use these when Ctrl+Alt+vowel does nothing or triggers a system shortcut.

## Macron dead key (AltGr+umlaut or AltGr+circumflex)

On layouts where **AltGr+umlaut** (¨, U+00A8) is easy to type (e.g. French AZERTY: key next to P), you can use it as a **dead key** for macrons:

1. Press **AltGr+¨** (nothing is inserted).
2. Press **a**, **e**, **i**, **o**, or **u** (with or without Shift) → the corresponding macron vowel is inserted (ā ē ī ō ū or Ā Ē Ī Ō Ū).

On some layouts (e.g. Dvorak or others where the macron dead key is on **^**), use **AltGr+^** (circumflex) the same way: then type a vowel to get the macron form.

This works in Composition, Conversion, Precomposition, and Direct input. If the next key is not a vowel, the dead state is cancelled and the key is handled normally.

- **Dvorak with Right Alt:** If Right Alt sends only “Alt” (not AltGr), **Ctrl+Left Alt+vowel** still works for macrons; **Right Alt+vowel** is also supported (Right Alt is detected from the physical key).
- **AZERTY / Dvorak:** If **Ctrl+LeftAlt+vowel** inserted ā but also switched to Hiragana, a follow-up “ghost” Hiragana key from the layout is now suppressed so only the macron is inserted and the mode stays unchanged.
- **Right Ctrl+Alt+a:** Some desktops or apps bind **Right Ctrl+Alt+a** to “Select all” and consume it before the IME. Use **Left Ctrl+Left Alt+a** (or **Ctrl+RightAlt+a** / **Ctrl+AltGr+a** where available) for macrons if Right Ctrl+Alt does nothing or selects all.
- **AltGr+deadkey still not working:** If **AltGr+¨** or **AltGr+^** does nothing, your layout may send a different keysym for the dead key or AltGr may not set the modifiers we use. Check the debug log (see development docs) for the keyval and modifiers received; we support diaeresis keyval 0xA8 and 0xFE20, and circumflex 0x5E and 0xFE22.
