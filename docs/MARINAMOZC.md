# marinaMozc branding

This fork is branded **marinaMozc** so it can be installed **next to** stock Mozc without overwriting it.

## Identity

- **Product name:** marinaMozc (shown in the IBus/language panel).
- **IBus component:** `com.marinamozc.IBus.Mozc` (separate from `com.google.IBus.Mozc`).
- **Component file:** `marinamozc.xml` (installed under `/usr/share/ibus/component/`).
- **Engine executable:** `ibus-engine-marinamozc` (under `/usr/lib/ibus-marinamozc/`).
- **Server directory:** `/usr/lib/marinamozc/` (mozc_server, mozc_tool, mozc_renderer).
- **Icons / data:** `/usr/share/ibus-marinamozc/`, `/usr/share/icons/marinamozc/`.

## Configuration

- **`src/config.bzl`** – `BRANDING = "marinaMozc"` and the paths above. Change these to use a different prefix (e.g. `/usr/local`) if needed.
- **`src/unix/ibus/gen_mozc_xml.py`** – When `--branding=marinaMozc`, generates the marinaMozc component name and textdomain.

After building and installing, add **marinaMozc** as an input method in IBus; it will appear alongside Mozc in the list.
