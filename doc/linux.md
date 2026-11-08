# On Linux

## Install

### From the official release

Download a static build of the [latest release]:

 - [`scrcpy-linux-x86_64-v3.3.4.tar.gz`][direct-linux-x86_64] (x86_64)  
   <sub>SHA-256: `0305d98c06178c67e12427bbf340c436d0d58c9e2a39bf9ffbbf8f54d7ef95a5`</sub>

[latest release]: https://github.com/Genymobile/scrcpy/releases/latest
[direct-linux-x86_64]: https://github.com/Genymobile/scrcpy/releases/download/v3.3.4/scrcpy-linux-x86_64-v3.3.4.tar.gz

and extract it.

_Static builds of scrplay for Linux are still experimental._


### From your package manager

Scrplay is a fork and is **not packaged in distributions yet**: build it from
[source](build.md) instead.

For reference, the upstream [scrcpy](https://github.com/Genymobile/scrcpy)
project (from which scrplay is derived, but which it does not replace) is
packaged in several distributions:

 - Debian/Ubuntu: ~~`apt install scrcpy`~~ _(obsolete version)_
 - Arch Linux: `pacman -S scrcpy`
 - Fedora: `dnf copr enable zeno/scrcpy && dnf install scrcpy`
 - Gentoo: `emerge scrcpy`
 - Snap: ~~`snap install scrcpy`~~ _(obsolete version)_
 - … (see [repology](https://repology.org/project/scrcpy/versions))

Note that these packages install the upstream `scrcpy` binary, not `scrplay`.


### From an install script

To install the latest release from `master`, follow this simplified process.

First, you need to install the required packages:

```bash
# for Debian/Ubuntu
sudo apt install ffmpeg libsdl2-2.0-0 adb wget \
                 gcc git pkg-config meson ninja-build libsdl2-dev \
                 libavcodec-dev libavdevice-dev libavformat-dev libavutil-dev \
                 libswresample-dev libusb-1.0-0 libusb-1.0-0-dev
```

Then clone the repo and execute the installation script
([source](/install_release.sh)):

```bash
git clone https://github.com/imzlh/scrplay
cd scrplay
./install_release.sh
```

When a new release is out, update the repo and reinstall:

```bash
git pull
./install_release.sh
```

To uninstall:

```bash
sudo ninja -Cbuild-auto uninstall
```

_Note that this simplified process only works for released versions (it
downloads a prebuilt server binary), so for example you can't use it for testing
the development branch (`dev`)._

_See [build.md](build.md) to build and install the app manually._


## Run

_Make sure that your device meets the [prerequisites](/README.md#prerequisites)._

Once installed, run from a terminal:

```bash
scrplay
```

or with arguments (here to disable audio and record to `file.mkv`):

```bash
scrplay --no-audio --record=file.mkv
```

Documentation for command line arguments is available:
 - `man scrplay`
 - `scrplay --help`
 - on [github](/README.md)
