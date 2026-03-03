# Resegmentation and Right Shift: debugging and fixes

This document describes the issues we ran into (partial commit / resegmentation, Right Shift toggle) and the approaches we tried before arriving at the current fixes. It is intended as a record for future maintenance and similar debugging.

---

## 1. Problems described

### 1.1 Resegmentation (partial commit)

- **Scenario:** User types e.g. `だいげんごう`, converts to segments (e.g. 大 | げんごう). For the second segment they want to choose 元 (one kanji for “gen”) and have the remainder ごう stay for further conversion.
- **Observed:** Choosing 元 and pressing Enter committed everything; “ぐう” disappeared. Alternatively, the wrong character (e.g. 刈) was committed, or after choosing 元 the next segment was ん (candidates for ん) instead of ごう, with no third segment to navigate to.

### 1.2 Right Shift not toggling Hiragana ↔ Katakana (Manyoshu)

- **Desired:** Right Shift alone (no other keys) should toggle between Hiragana and Katakana (Manyoshu) on key release.
- **Observed:** Right Shift did nothing; mode did not toggle.

---

## 2. Approaches and fixes

### 2.1 Enter: Commit vs CommitOnlyFirstSegment (keymap)

- **Idea:** Map Enter in Conversion to `CommitOnlyFirstSegment` so that Enter runs the “commit through focused segment” path (which includes partial-commit logic) instead of full commit.
- **What we did:** User tried changing the keymap TSV so that Conversion + Enter (or VirtualEnter) triggered `CommitOnlyFirstSegment` instead of `Commit`.
- **Result:** Either no change in behaviour or the keymap was reverted. The underlying issue was that when Enter stayed mapped to `Commit`, the session called `Session::Commit()` → `converter->Commit()`, so `CommitSegmentsInternal` (where partial commit is implemented) was never run for Enter.
- **Conclusion:** Keymap alone was not enough; we needed the converter to honour partial candidates when the user presses Enter with the default Commit binding.

### 2.2 Reordering logic in CommitSegmentsInternal (partial before full)

- **Idea:** In `CommitSegmentsInternal`, the “commit all” path was taken before the partial-commit path, so partial commit was never used.
- **What we did:** Reordered the logic so that the partial-commit branch (check `PARTIALLY_KEY_CONSUMED` and `consumed_key_size` for the focused segment’s candidate) runs *before* the “if commit all segments, call Commit()” branch.
- **Result:** This fixed the control flow when the *segment* path was used (e.g. Ctrl+Down or VirtualEnter → CommitSegment). It did not fix the case where the user presses Enter, because Enter was still bound to `Commit`, which never calls `CommitSegmentsInternal`.

### 2.3 Making Enter run the partial path when applicable (Commit → CommitSegmentsInternal)

- **Idea:** Keep Enter mapped to `Commit`, but when we are in conversion and the *last* segment has a partial candidate (e.g. 元 for げんごう), run the same segment path so that partial commit can apply.
- **What we did:** At the start of `EngineConverter::Commit()`, if we are in CONVERSION and the last segment’s focused candidate has `PARTIALLY_KEY_CONSUMED` and a valid `consumed_key_size`, we call `CommitSegmentsInternal(..., segments_.conversion_segments_size(), ...)` and return instead of doing the usual full commit.
- **Result:** Enter now goes through the partial-commit logic when the last segment has such a candidate, so choosing 元 and pressing Enter can leave the remainder (e.g. ごう) as the next segment.
- **Files:** `src/engine/engine_converter.cc` (start of `Commit()`).

### 2.4 Right Shift: keymap normalization (AddRule vs GetCommand)

- **Idea:** Right Shift key-up was being sent to the session (confirmed by logging), but the keymap lookup never matched “RightShift”, so `ToggleManyoshuHiragana` was never invoked.
- **Cause:** In keymap lookup, `GetCommand` normalises the key with `KeyEventUtil::NormalizeModifiers`, which strips LEFT_SHIFT and RIGHT_SHIFT. So the *lookup* key became “no modifiers”. When *adding* rules from the TSV, `AddRule` did *not* normalise; “RightShift” was stored with `KeyInformation` that included RIGHT_SHIFT. Lookup used normalised key (0); storage used unnormalised key (non-zero). They did not match.
- **What we did:** In `KeyMap<T>::AddRule` we now normalise the key with `KeyEventUtil::NormalizeModifiers` before calling `GetKeyInformation`, so that the stored key matches the normalised key used in `GetCommand`. Then “RightShift” (which normalises to no modifiers) is stored and looked up with the same representation.
- **Result:** Right Shift alone on key release now matches the RightShift rule and toggles Hiragana ↔ Katakana (Manyoshu).
- **Files:** `src/session/keymap.h` (`AddRule` template).

### 2.5 Wrong candidate committed (刈 instead of 元)

- **Idea:** When the user selected 元 and pressed Enter, the engine sometimes committed 刈. Logs showed `candidate_id: 70` and the segment’s 70th candidate was 刈. So the engine was using a different candidate than the one the user selected.
- **Cause:** On Enter, the session only receives a key event (COMMIT); it does not receive a “submit this candidate” command with an id. The engine therefore used whatever was “focused” in its internal state (`candidate_list_.focused_id()`). If the UI and server had diverged (e.g. user clicked 元 but the server never got that selection, or focus was still on 刈), the wrong candidate was used.
- **What we did:** In `CommitSegmentsInternal`, when resolving which candidate to use for the focused segment, we first check `selected_candidate_indices_[focus_seg_idx]`. If that index is valid and points to a candidate with `PARTIALLY_KEY_CONSUMED`, we use that index instead of `GetCandidateIndexForConverter` (which returns `focused_id()`). So when the user had previously selected a candidate (e.g. by click), we prefer that stored selection when it is a partial candidate.
- **Result:** If the client had sent the selection (e.g. click on 元), we now commit 元 instead of whatever was in `focused_id()`. If the client never sends the selection, the fallback remains `focused_id()`.
- **Files:** `src/engine/engine_converter.cc` (`CommitSegmentsInternal`, before the partial-commit block).

### 2.6 元 consuming only first kana (next segment ん instead of ごう)

- **Idea:** After choosing 元, the next segment was ん (candidates for ん) and there was no third segment for ぐう. So the engine was committing only the first kana (consumed_key_size = 1, i.e. “げ”) and the remainder was “んごう” instead of “ごう”.
- **Cause:** We inject prefix candidates for several prefix lengths (e.g. 1, 2, 3, 4). So for segment key “げんごう” we get:
  - prefix “げ” (len 1) → 元 with `consumed_key_size = 1` (remainder んごう),
  - prefix “げん” (len 2) → 元 with `consumed_key_size = 2` (remainder ごう).
  So 元 appears twice. The UI can show or focus the 元 that came from “げ”. When the user selects 元, we were using whichever 元 was at the selected/focused index; if that was the “げ” variant, we committed only one kana and the next segment became ん.
- **What we did:** In `CommitSegmentsInternal`, after we have chosen a candidate (by focus or stored selection) that has `PARTIALLY_KEY_CONSUMED`, we scan the same segment for any other candidate with the *same* `.value` (e.g. 元) and `PARTIALLY_KEY_CONSUMED` but *larger* `consumed_key_size` (still &lt; segment key length). If we find one, we use that candidate instead. So we always use the “longest” reading for that surface form (e.g. げん→元) and leave the minimal remainder (ごう).
- **Result:** Selecting 元 now commits “げん” → 元 and leaves “ごう” as the next segment, so the user gets a third segment (ごう) and can navigate right to convert ぐう.
- **Files:** `src/engine/engine_converter.cc` (`CommitSegmentsInternal`, “prefer longest consumed for same value” block).

---

## 3. Debugging instrumentation (optional to remove)

The following were added for runtime debugging and can be removed once behaviour is confirmed and no longer needed:

- **Session (`session.cc`):** Logging in `SendKeyConversionState` (key_command, special_key, modifier_keys_size, first_modifier) and at the start of `ToggleManyoshuHiragana` (manyoshu_mode_).
- **Key event handler (`key_event_handler.cc`):** Logging on modifier key up (keyval, is_shift_r, current_pressed_empty, to_be_sent_empty, non_modifier_pressed, will_send).
- **Engine converter (`engine_converter.cc`):** Logging at the start of `CommitSegmentsInternal` (segments_to_commit, focus_seg_idx, candidate_id, has_partial_attr, consumed_key_size, seg_key_len, in_conversion) and when taking the full-commit path (“Taking full commit path”).

Logs were written as NDJSON to a debug log file (e.g. `.cursor/debug-1b2dce.log` in the marinaMoji repo). Removing these blocks does not change the fixes above.

---

## 4. Summary of current behaviour

| Area | Fix | Where |
|------|-----|--------|
| Enter and partial commit | When Enter triggers Commit in conversion, we first check the last segment for a partial candidate; if present, we call CommitSegmentsInternal so partial commit runs. | `engine_converter.cc` → `Commit()` |
| Right Shift toggle | Keymap AddRule now normalises the key like GetCommand, so “RightShift” matches when the normalised key is used on lookup. | `keymap.h` → `AddRule` |
| Wrong candidate on Enter | We prefer `selected_candidate_indices_[focus_seg_idx]` over `focused_id()` when it points to a partial candidate. | `engine_converter.cc` → `CommitSegmentsInternal` |
| 元 → next segment ん | For the chosen partial candidate we prefer, among same `.value` and PARTIALLY_KEY_CONSUMED, the one with the largest `consumed_key_size`, so we use げん→元 and remainder ごう. | `engine_converter.cc` → `CommitSegmentsInternal` |

---

## 5. References

- Keymap: `src/data/keymap/ms-ime.tsv` (Conversion Enter, RightShift, VirtualEnter, CommitOnlyFirstSegment).
- Session command dispatch: `session.cc` → `SendKeyConversionState`, `Commit`, `CommitSegment`, `ToggleManyoshuHiragana`.
- Converter: `engine_converter.cc` → `Commit`, `CommitSegmentsInternal`, `InjectPrefixCandidatesForConversionSegments`.
- Key normalisation: `composer/key_event_util.cc` → `NormalizeModifiers` (strips LEFT/RIGHT modifiers); keymap lookup and add in `session/keymap.h`.
