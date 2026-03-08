<img src="src/unix/ibus/toolbar_icons/logo_long_light.svg" title="" alt="" width="303">

A Japanese IME to turn back time

[Marina Pandolfino](https://www.crcao.fr/membre/marina-pandolfino/) (EPHE) | [Daniel Patrick Morgan](https://www.crcao.fr/membre/daniel-patrick-morgan/) (CNRS)

marinaMoji is a fork of [Mozc](https://github.com/google/mozc), Copyright 2010-2026 Google LLC, fine tuned for scholars of ancient and pre-modern Japanese.

===================================

Build Status
------------

(coming)

## Features

marinaMoji provides the following features for scholarly Japanese text input:

- **Kyūjitai/Shinjitai conversion:** Automatic conversion between modern and traditional characters via OpenCC (! currently improving conversion tables !).
- **Historical kana input:** Direct input of historical kana forms (ゐ, ゑ, and historical distinctions)
- **Full katakana mode:** convert katakana into kanji, as in hiragana mode; quickly switch between the two with `shift_R`.
- **Historical marks palette:** Set default repetition mark (々, 〻, 〱, ゝ, ヽ, ヾ, ゞ, ヶ, etc.) via input palette (`ctrl+shift+2`) and insert via `ctrl+shift+1`. 
- **Unicode kaeriten shortcuts:** directly type ㆑㆒㆓, etc., via `;r`, `;1`, `;2`, etc.
- **Floating toolbar** - Visual mode indicator showing current input mode, shin/kyu,  with quick access to historical marks
- **Macron vowels** - Input of macron vowels (ā, ē, ī, ō, ū) for scholarly transliteration in ASCII mode
- **Quick dictionary injection:** type `ctrl+shift+0` in compose mode to immediately save kanji phrase and pronunciation to user dictionary.

Build Instructions
------------------

* [How to build Mozc for Android](docs/build_mozc_for_android.md): for Android library (`libmozc.so`)
* [How to build Mozc for Linux](docs/build_mozc_for_linux.md): for Linux desktop
* [How to build Mozc for macOS](docs/build_mozc_in_osx.md): for macOS build
* [How to build Mozc for Windows](docs/build_mozc_in_windows.md): for Windows

License
-------

All Mozc code written by Google is released under
[The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause).
For third party code under [src/third_party](src/third_party) directory,
see each sub directory to find the copyright notice.  Note also that
outside [src/third_party](src/third_party) following directories contain
third party code.

### [src/data/dictionary_oss/](src/data/dictionary_oss)

Mixed.
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/dictionary/](src/data/test/dictionary)

The same as [src/data/dictionary_oss/](src/data/dictionary_oss).
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/stress_test/](src/data/test/stress_test)

Public Domain.  See the comment in
[src/data/test/stress_test/sentences.txt](src/data/test/stress_test/sentences.txt)

## Install in CachyOS

Install dependencies

```
sudo pacman -S --needed \
  ibus glib2 base-devel \
  qt6-base \
  opencc \
  gtk3 \
  zip unzip jdk-openjdk
```

install bazelisk

```
# AUR (yay/paru)
yay -S bazelisk
# or
paru -S bazelisk
```

Build

```
git clone https://github.com/YOUR_USERNAME/YOUR_MOZC_FORK.git marinaMozc --recursive
cd marinaMozc/src

bazelisk build package --config oss_linux --config release_build
```

Install

```
sudo unzip -o bazel-bin/unix/mozc.zip -d /
```

- **JAVA_HOME:** If you see "Could not find system javabase" or "must point to a JDK, not a JRE", install a JDK (`jdk-openjdk`), then e.g. `export JAVA_HOME=/usr/lib/jvm/java-25-openjdk` (or `default`) before building.
- **rules_swift aspect error:** If you see "required_aspect_providers, got element of type NoneType", ensure you use **bazelisk** (not system `bazel`) from the `src/` directory so the correct Bazel and dependency versions are used. This repo pins `rules_swift` to 2.5.0 and dependency overrides to avoid that failure on Linux.

Reload ibus and add marinaMozc

```
ibus write-cache
ibus restart
```