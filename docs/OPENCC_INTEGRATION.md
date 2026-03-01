# Adding OpenCC to Mozc (Kyūjitai/Shinjitai Conversion)

This document describes **OpenCC**-based character conversion is integrated in this Mozc fork so that candidates and committed output can be converted between modern (shinjitai) and traditional (kyūjitai) kanji, similar to marinaMoji’s behaviour with Rime.

## Implementation in this fork

- **Config:** `Config::use_traditional_kanji` (proto field 69, default false).
- **Rewriter:** `OpenccRewriter` in `src/rewriter/opencc_rewriter.cc` runs when the option is on and converts candidate `value` and `content_value` via OpenCC (jp2t). Commit output uses the same segment values, so it is converted automatically.
- **Shortcut:** **Ctrl+Shift+F** toggles the option (command `ToggleTraditionalKanji`), registered in Precomposition, Composition, and Conversion. Keymap entries added in ms-ime, atok, kotoeri, chromeos, and mobile.
- **Enabling OpenCC at build time:** The rewriter compiles with a no-op stub unless `MOZC_USE_OPENCC` is defined and the OpenCC library is linked. To enable: add the OpenCC dependency to your build (e.g. link opencc or system libopencc), define `MOZC_USE_OPENCC`, and ensure `jp2t.json` (and any `.ocd2` it references) is installed (e.g. under `OPENCC_DATA_DIR` or `/usr/share/opencc/`).

## How marinaMoji does it (reference)

In **marinaMoji** (Rime-based), OpenCC is not used in the IBus C code. It is used inside **librime**:

- Rime’s **Simplifier** component (`librime/src/rime/gear/simplifier.cc`) loads an OpenCC config (e.g. `jp2t.json` for Japanese simplified ↔ traditional).
- Each schema sets `opencc_config: jp2t.json` in YAML. The Simplifier runs in the Rime conversion pipeline and rewrites candidate text (and commit) according to the current “simplified / traditional” option.
- So: one component in the conversion pipeline, configurable per schema, and all candidates and committed text are converted there.

## Mozc’s existing “character form” system

Mozc already has a **CharacterFormManager** that converts strings for:

- **Preedit**: `ConvertPreeditString()` — e.g. half/full width for romaji input.
- **Conversion/candidates**: `ConvertConversionString()` and `ConvertConversionStringWithAlternative()` — used when building candidate list and when applying the user’s character-form preferences (full/half width, etc.).

So all preedit and conversion strings (including what gets committed) already pass through this manager. It does **not** currently handle kanji variants (shinjitai vs kyūjitai); it only knows `HALF_WIDTH` and `FULL_WIDTH`.

## Is it possible to add OpenCC? Yes

Two main integration strategies:

---

### Approach 1: Integrate OpenCC into CharacterFormManager (recommended)

**Idea:** Keep a single place for “output character form”. After the existing full/half conversion, optionally run the string through OpenCC (e.g. `jp2t.json`) when a config option is set (e.g. “use traditional kanji” or “opencc_config: jp2t”).

**Where:**

- **Config:** Add a new option, e.g. in `protocol/config.proto` and the config UI:
  - e.g. `optional bool use_traditional_kanji = …` or `optional string opencc_config = …` (e.g. `"jp2t"` / `"jp2t.json"`).
- **CharacterFormManager:** In the implementation that backs `ConvertPreeditString` and `ConvertConversionString` (e.g. `CharacterFormManagerImpl::ConvertString` in `config/character_form_manager.cc`), after applying the current rules, if the new option is enabled, call OpenCC (e.g. `opencc::Converter::Convert()`) and replace the output (and optional alternative) with the OpenCC result.

**Pros:**

- One integration point.
- Every caller that already uses `ConvertConversionString` / `ConvertPreeditString` (preedit, conversion candidates, prediction, commit path) automatically gets OpenCC conversion when the option is on.
- Same behaviour as marinaMoji: one toggle, all output (candidates + commit) in the chosen form.

**Cons:**

- The `config` (or a small helper) module must depend on **libopencc** and ship or find OpenCC config files (e.g. `jp2t.json`).
- You need to wire the new config option into `CharacterFormManager::ReloadConfig()` so the OpenCC step is enabled/disabled and the config path is correct.

**Files to touch (summary):**

- `protocol/config.proto` — add option (e.g. `use_traditional_kanji` or `opencc_config`).
- Config GUI (e.g. `gui/config_dialog/`) — add checkbox or dropdown for the option.
- `config/character_form_manager.cc` (and possibly a small `config/opencc_util.cc` if you isolate OpenCC) — load OpenCC config, apply conversion after existing rules; reload when config changes.
- Build system — link `config` (or the new helper) with `libopencc`, and install or embed OpenCC data (e.g. `jp2t.json` and any `.ocd2` it references) so the engine can find them at runtime (e.g. under `shared_data_dir` or a known path).

---

### Approach 2: New OpenCC rewriter

**Idea:** Add a **Rewriter** that, when “traditional kanji” (or OpenCC) is enabled, walks segments and converts each candidate’s `value` / `content_value` through OpenCC.

**Where:**

- New `rewriter/opencc_rewriter.cc` (and `.h`) implementing `RewriterInterface::Rewrite()`: iterate segments and candidates, replace text with OpenCC conversion.
- Register it in `rewriter/rewriter.cc` (e.g. after `VariantsRewriter`).
- Config: same as above (new option in proto and GUI).
- **Commit path:** The string that is finally committed might still need to go through OpenCC. If the committed string is taken from the same segment/candidate values that the rewriter already converted, you are done. If there is a separate code path that builds the commit string, you must apply OpenCC there too (or route it through `CharacterFormManager::ConvertConversionString()` and do OpenCC inside the manager as in Approach 1).

**Pros:**

- OpenCC is confined to the rewriter (and optionally one more place for commit).
- Fits the existing “rewriter” pattern (like VariantsRewriter).

**Cons:**

- You must ensure **every** place that produces text shown to the user or committed (including prediction, preedit, commit) either goes through this rewriter or through a single OpenCC path; otherwise behaviour will be inconsistent. The commit path is easy to miss.

---

## Recommendation

**Approach 1 (CharacterFormManager)** is the most straightforward way to get “candidates and output converted like marinaMoji”: one toggle, one code path (after existing full/half rules), and all preedit, conversion, prediction, and commit strings automatically converted when the option is on.

## OpenCC dependency and data

- **Library:** Link against **libopencc** (e.g. `opencc` on Arch, `libopencc-dev` on Debian). Use the C++ API: `opencc::Config`, `opencc::Converter`, `Converter::Convert(std::string)`.
- **Config:** Use the same OpenCC config as marinaMoji if you want the same behaviour, e.g. **jp2t** (Japanese simplified → traditional). Config file is JSON (e.g. `jp2t.json`); it can reference `.ocd2` dictionaries. You can ship `jp2t.json` (and any required `.ocd2`) in the engine’s data directory or rely on the system’s OpenCC data if you install it there.
- **Path:** Resolve the config path the same way as marinaMoji/Rime if you want user overrides: e.g. look under a user config directory first, then fall back to shared/data dir.

## Toggle (e.g. Ctrl+Shift+F)

marinaMoji uses a menu option and shortcut (e.g. Ctrl+Shift+F) to toggle “Traditional kanji (kyūjitai)”. In Mozc you would:

- Store the state in the config (e.g. `use_traditional_kanji`).
- On toggle, update config and call `CharacterFormManager::GetCharacterFormManager()->ReloadConfig(config)` (if you integrated OpenCC there) so the next conversion uses the new setting.
- Optionally send a command to the session/server so the conversion result is refreshed (e.g. re-run conversion with the new form).

## Summary

| Aspect | marinaMoji (Rime) | Mozc + OpenCC (proposed) |
|--------|-------------------|---------------------------|
| Where conversion runs | Rime Simplifier (in pipeline) | CharacterFormManager (after full/half) or new Rewriter |
| Config | Schema `opencc_config: jp2t.json` | New config option + jp2t (or custom) |
| Candidates | Converted by Simplifier | Converted by same path as other character form |
| Commit | Same pipeline | Same ConvertConversionString path |
| Toggle | Option + shortcut | Config option + shortcut + ReloadConfig |

Adding OpenCC into Mozc is **possible** and fits best by extending the existing character-form conversion path (Approach 1) so that candidates and committed output are converted in one place, similar to marinaMoji.
