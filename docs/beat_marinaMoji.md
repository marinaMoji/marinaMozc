# How to beat marinaMoji

## Positives and negatives

**Rime**

- [+] The engine wants Chinese, so for multiple syllable kanji phrases it offers also the first kanji; using this, a somewhat slow character selection instantly records a nominal phrase in user history.

- [+] Simpler to code in the moment

- [+] Sunk costs

- [+] Ideological purity, sinological supremacy

- [+] Easier to justify branding via toolbar, because default doesn't have input schema in the taskbar (stupid, fixable)

- [-] Much worse prediction, not usable for everyday IME

- [-] Presumably harder to code for multiple OSes

**Mozc**

- [+] Much better prediction

- [+] Theoretically instant roll out across all OSes

- [+] Fully functional IME to rival adult ones for all purposes.

- [-] smart auto-segmenter breaks up nominal phrases, must use multiple commits, thus cannot store 大元宮 in user history, because it requires multiple commits.

---

## Options

We need to seriously weigh and reflect upon the overall balance of positives and negatives. To do so, however, we should make sure that we are truly up against a wall before making a compromise.

For the moment, the question is how to get Mozc to rival marinaMoji in terms of remembering manually-entered nominal phrases.

### User history

*Option 1*: rework the segment-candidate-commit process to propose partial candidates and resegment what's left iteratively. But, this requires arrow keys, and it breaks everything.

_Option 2_: introduce a break key to force segmentation: type `だいげん|ぐう`, break segmentation, select candidates, delete the break, send commit to user history. **Implemented:** boundary is toggled with **Ctrl+Shift+4**. On US QWERTY, Shift+4 sends `$`, so the keymap binds both "Ctrl Shift 4" and "Ctrl Shift $". On AZERTY, the same physical key sends `'` (apostrophe) when shifted, so "Ctrl Shift '" is also bound; the session recognizes key code 0x27 so the break key works regardless of keymap. Rebind in `src/data/keymap/ms-ime.tsv` or custom keymap if needed.

### User dictionary

_Option 3_: ultra-fast add word option

- type a nominal phrase or whatever somehow

- `shift+arrow` to select

- `ctrl+0` to open candidate palette with all possible pronunciations

- select and commit, like for a character

- POS candidate window ranked via intelligent linguistic analysis; lazy user skips choosing POS, hits `enter`

*Option 4*: ultra-fast add word option:

- `ctrl+0` to open add word window

- type word character by character using pronunciations you want; Mozc records predit text behind each commit and adds to the pronunciation cell. Now we have 'expression' and 'reading'; 

- lazy user skips choosing POS, hits `enter`

---

## Clarifications

**User history vs user dictionary (Rime and Mozc)**  
In both engines, *user history* is implicit learning from commits (used for ranking/suggestions); *user dictionary* is an explicit, editable word list. History is persistent on disk but can evict or down-rank rarely used entries over time, so it “forgets.” For important nominal phrases, adding to the **user dictionary** is the way to make them permanent; history alone is not reliable long-term.

**Why “last commit” doesn’t work for add-word**  
Mozc’s segmenter forces 2–3 syllable chunks, so building 大元宮 typically requires multiple commits (e.g. だい→大, げん→元, ぐう→宮). There is no single commit with key だいげんぐう and value 大元宮. So an “add last committed to user dictionary” shortcut would only add the last segment (e.g. ぐう→宮). Any add-word flow that relies on commit data must either accumulate (key, value) across multiple commits in a dedicated “add word” mode, or use selection/clipboard + reverse conversion instead of commit data.

---

## Refinements for the options

**Segment-break key (Option 2)**  
When the user commits after using the break (e.g. だいげん_ぐう → 大元宮), the **key sent to user history must have the break character stripped** (だいげんぐう). Then the next time they type `daigenguu` with no break, the key matches and history suggests 大元宮. The converter must treat the break as a hard boundary (no lattice node may span across it).

**Multiple pronunciation candidates (Options 3 & 4)**  
When the word is built from selection (or from reverse conversion), a single “reverse convert” often returns one reading (e.g. だいもとみや for 大元宮). The converter’s reverse conversion actually produces **multiple candidates per segment**; we can enumerate combinations, rank them, and show a short candidate list (e.g. だいげんぐう, だいもとみや, …) so the user picks the intended reading instead of re-typing it.

**POS ranking (Options 3 & 4)**  
For kanji-only (or mainly kanji) selections, all are effectively nouns; the open choice is *which* noun type (名詞, 固有名詞, 地名, 人名, 組織, …). We can **autodetect** by looking up the word in the system dictionary, reading each token’s `lid`/`rid`, classifying with PosMatcher, and aggregating (by count or cost). Then show the POS dropdown **ranked by that probability** so the lazy user can accept the top suggestion and hit Enter.
