[Mozc - a Japanese Input Method Editor designed for multi-platform](https://github.com/google/mozc)
===================================

Copyright 2010-2026 Google LLC

Mozc is a Japanese Input Method Editor (IME) designed for multi-platform such as
Android OS, Apple macOS, Chromium OS, GNU/Linux and Microsoft Windows.  This
OpenSource project originates from
[Google Japanese Input](http://www.google.com/intl/ja/ime/).

Mozc is not an officially supported Google product.

Build Status
------------

| Linux | Windows | macOS | Android lib |
|:-----:|:-------:|:-----:|:-----------:|
| [![Linux](https://github.com/google/mozc/actions/workflows/linux.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/linux.yaml) | [![Windows](https://github.com/google/mozc/actions/workflows/windows.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/windows.yaml) | [![macOS](https://github.com/google/mozc/actions/workflows/macos.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/macos.yaml) | [![Android lib](https://github.com/google/mozc/actions/workflows/android.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/android.yaml) |


What's Mozc?
------------
For historical reasons, the project name *Mozc* has two different meanings:

1. Internal code name of Google Japanese Input that is still commonly used
   inside Google.
2. Project name to release a subset of Google Japanese Input in the form of
   source code under OSS license without any warranty nor user support.

In this repository, *Mozc* means the second definition unless otherwise noted.

Detailed differences between Google Japanese Input and Mozc are described in [About Branding](docs/about_branding.md).

Build Instructions
------------------

* [How to build Mozc for Android](docs/build_mozc_for_android.md): for Android library (`libmozc.so`)
* [How to build Mozc for Linux](docs/build_mozc_for_linux.md): for Linux desktop
* [How to build Mozc for macOS](docs/build_mozc_in_osx.md): for macOS build
* [How to build Mozc for Windows](docs/build_mozc_in_windows.md): for Windows

Release Plan
------------

tl;dr. **There is no stable version.**

As described in [About Branding](docs/about_branding.md) page, Google does
not promise any official QA for OSS Mozc project.  Because of this,
Mozc does not have a concept of *Stable Release*.  Instead we change version
number every time when we introduce non-trivial change.  If you are
interested in packaging Mozc source code, or developing your own products
based on Mozc, feel free to pick up any version.  They should be equally
stable (or equally unstable) in terms of no official QA process.

[Release History](docs/release_history.md) page may have additional
information and useful links about recent changes.

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

Install dependancies

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