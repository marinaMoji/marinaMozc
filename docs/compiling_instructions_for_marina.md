# Compiling marinaMozc on Ubuntu

This guide is for building **marinaMozc** (this Mozc fork) from source on **Ubuntu**. It covers the packages you need, how to install Bazelisk, how to compile, and how to install the result.

---

## 1. Install required packages

Install the following **before** compiling. These provide the libraries and tools the build needs.

### 1.1 One-time setup (copy-paste)

Open a terminal and run:

```bash
sudo apt update
sudo apt install -y \
  git \
  unzip zip \
  openjdk-17-jdk \
  libibus-1.0-dev \
  qt6-base-dev \
  libgtk-3-dev \
  librsvg2-dev \
  libopencc-dev
```

### 1.2 What each package is for

| Package | Purpose |
|--------|--------|
| `git` | Clone the repository (including submodules). |
| `unzip` `zip` | Unpack the built `mozc.zip` when installing. |
| `openjdk-17-jdk` | Java JDK required by Bazel. If the build complains about “javabase”, set `JAVA_HOME` (see Troubleshooting). |
| `libibus-1.0-dev` | IBus input method framework (glib, gobject, ibus headers/libs). |
| `qt6-base-dev` | Qt6 (core, gui, widgets) for the config dialog and candidate window. |
| `libgtk-3-dev` | GTK3 for the marinaMozc floating toolbar. |
| `librsvg2-dev` | Renders SVG icons in the toolbar. |
| `libopencc-dev` | **Optional but recommended.** Enables the Shin/Kyū (traditional vs modern kanji) conversion. Without it, the build still succeeds but that feature has no effect. |

You can omit `libopencc-dev` if you do not need traditional kanji support; the rest are required for a full build including the toolbar.

---

## 2. Install Bazelisk

The project uses **Bazelisk** to run the correct Bazel version automatically. Do **not** use the system `bazel` package; use Bazelisk.

### 2.1 Download and install the binary

1. Open: [Bazelisk releases on GitHub](https://github.com/bazelbuild/bazelisk/releases).
2. Download the **Linux amd64** binary (e.g. `bazelisk-linux-amd64` from the latest release).
3. Make it executable and put it in your `PATH`. For example, to install for your user only:

```bash
mkdir -p ~/bin
mv ~/Downloads/bazelisk-linux-amd64 ~/bin/bazelisk
chmod +x ~/bin/bazelisk
```

4. Ensure `~/bin` is in your `PATH`. Add this to `~/.bashrc` or `~/.profile` if needed:

```bash
export PATH="$HOME/bin:$PATH"
```

5. Reload your shell or run `source ~/.bashrc`, then check:

```bash
bazelisk version
```

You should see Bazelisk run and then download/use the Bazel version required by the repo (defined in `src/.bazeliskrc`).

### 2.2 Alternative: install via Go

If you have Go installed:

```bash
go install github.com/bazelbuild/bazelisk@latest
```

Ensure `$GOPATH/bin` or `$HOME/go/bin` is in your `PATH`, then use `bazelisk` as in the steps below.

---

## 3. Get the source and compile

### 3.1 Clone the repository (with submodules)

Use the **marinaMozc** fork URL (replace with the actual repo URL if different):

```bash
git clone https://github.com/YOUR_ORG_OR_USER/mozc.git marinaMozc --recursive
cd marinaMozc/src
```

The `--recursive` option is required; the build depends on submodules.

### 3.2 Build the package

From the **`marinaMozc/src`** directory (not the repo root), run:

```bash
bazelisk build package --config oss_linux --config release_build
```

The first run can take a long time (tens of minutes) while dependencies are downloaded and compiled. When it finishes successfully, the installable archive is:

- **`bazel-bin/unix/mozc.zip`**

---

## 4. Install and use marinaMozc

### 4.1 Install the built files

From the **`marinaMozc/src`** directory:

```bash
sudo unzip -o bazel-bin/unix/mozc.zip -d /
```

This unpacks the engine, server, icons, and IBus component into system directories (e.g. `/usr/lib/marinamozc/`, `/usr/lib/ibus-marinamozc/`, `/usr/share/ibus/component/marinamozc.xml`).

### 4.2 Verify installation

```bash
test -f /usr/share/ibus/component/marinamozc.xml && echo "Component OK"
test -x /usr/lib/ibus-marinamozc/ibus-engine-marinamozc && echo "Engine OK"
```

Both lines should print “OK”.

### 4.3 Reload IBus

So that IBus picks up the new component:

```bash
ibus write-cache
ibus restart
```

If you use a full desktop session, logging out and back in is an alternative.

### 4.4 Add marinaMozc as an input method

- **GNOME:** **Settings → Keyboard → Input Sources → Add (+)** → choose **Japanese** → select **marinaMozc** (or “marinaMozc (Japanese Input Method)”).
- **Other (IBus):** In your input method settings, add the engine named **marinaMozc** / **Japanese (marinaMozc)**.

If it does not appear, make sure `ibus-daemon` is running and check for errors when opening input settings or running `ibus engine`.

---

## 5. Candidate window (IBus vs Mozc)

By default, marinaMozc uses the **IBus** candidate window (equivalent to `MOZC_IBUS_CANDIDATE_WINDOW=ibus`). The Mozc candidate window offers more detail but can have positioning issues on some setups.

You can change this in **Properties → Misc → Candidate window** (dropdown: IBus / Mozc). The choice is stored in `~/.config/mozc/ibus_config.textproto` and applies after switching input method away and back, or restarting IBus.

To force IBus from the command line when starting the daemon:

```bash
ibus exit
MOZC_IBUS_CANDIDATE_WINDOW=ibus ibus-daemon -d
```

---

## 6. Optional: Wayland and gtk-layer-shell

On **Wayland**, the toolbar is positioned like marinaMoji (bottom-right) without extra packages. If you want to use **gtk-layer-shell** (e.g. on Sway/Hyprland) for layer-shell positioning:

1. Install:  
   `sudo apt install -y libgtk-layer-shell-dev`
2. Build with:  
   `bazelisk build --define=gtk_layer_shell=1 package --config oss_linux --config release_build`

If you do not add the define, the toolbar still works on X11 and on Wayland with the built-in positioning.

---

## 7. Troubleshooting

### “Could not find system javabase” / “must point to a JDK, not a JRE”

Install a JDK (e.g. `openjdk-17-jdk` as above), then point Bazel to it, for example:

```bash
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
```

Adjust the path to match your system (`/usr/lib/jvm/` and `ls` there to see the exact name). Then run the build again from `marinaMozc/src`.

### “required_aspect_providers, got element of type NoneType” (rules_swift)

Use **bazelisk** (not the system `bazel`) and run it from the **`src`** directory. The repo pins Bazel and dependency versions via `src/.bazeliskrc`; using system Bazel can cause this error.

### Build or dependency errors after system updates

Try cleaning Bazel’s cache and rebuilding:

```bash
cd marinaMozc/src
bazelisk clean --expunge
bazelisk build package --config oss_linux --config release_build
```

### marinaMozc does not appear in the input method list

- Run `ibus write-cache` and `ibus restart` again.
- Confirm the component file exists:  
  `ls -l /usr/share/ibus/component/marinamozc.xml`
- Check that the engine is executable:  
  `ls -l /usr/lib/ibus-marinamozc/ibus-engine-marinamozc`

---

## 8. Summary checklist

1. **Packages:** `git`, `unzip`, `zip`, `openjdk-17-jdk`, `libibus-1.0-dev`, `qt6-base-dev`, `libgtk-3-dev`, `librsvg2-dev`, and (optional) `libopencc-dev`.
2. **Bazelisk:** Download the Linux binary from GitHub, put it in `PATH` as `bazelisk`, and run `bazelisk version`.
3. **Clone:** `git clone ... marinaMozc --recursive` and `cd marinaMozc/src`.
4. **Build:** `bazelisk build package --config oss_linux --config release_build`.
5. **Install:** `sudo unzip -o bazel-bin/unix/mozc.zip -d /`.
6. **Reload:** `ibus write-cache` and `ibus restart`.
7. **Add input method:** In Settings, add **Japanese → marinaMozc**.

Config is stored under `~/.config/marinamozc/` and is separate from stock Mozc, so you can install marinaMozc alongside it for testing.
