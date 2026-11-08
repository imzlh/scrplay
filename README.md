# scrplay

[![CI](https://github.com/imzlh/scrplay/actions/workflows/ci.yml/badge.svg)](https://github.com/imzlh/scrplay/actions/workflows/ci.yml)

<img src="app/data/icon.svg" width="128" height="128" alt="scrplay" align="right" />

**scrplay** is a fork of [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy)
(v3.3.4) that adds a **desktop gaming control layer** on top of the mirroring
core: it maps your computer keyboard and mouse to *human-like synthetic
touches*, so Android games and cloud games can be played from the desktop —
no root, no app installed on the device.

Everything from upstream scrcpy still works the same: mirroring over USB or
[TCP/IP](doc/connection.md#tcpip-wireless) with video and audio, control with
the computer keyboard and mouse, on _Linux_, _Windows_, and _macOS_.

## 中文速览

scrplay 是 scrcpy（v3.3.4）的分支，在原有投屏/控制能力之上增加了**游戏模式**：
把键鼠操作映射为拟人化的合成触摸来玩手机游戏与云游戏（云游戏版《崩坏：星穹
铁道》已实测）。WASD 虚拟摇杆、鼠标瞄准视角、窗口内 ImGui 编辑器、profile
与预设切换、反检测的 ease-out 滑动与位置/时序抖动等特性详见
[doc/game-mode.md](doc/game-mode.md)；快速启动：构建后执行 `./run-game.sh`
（开发构建，等价于 `build/app/scrplay --game-mode`，并自动配置 fcitx5/ibus
输入法环境变量，保证 XWayland 下编辑器可输入中文）。设备端 server 为上游
未改动的预编译二进制（Java 包名保留 `com.genymobile.scrcpy`）。

## Features

### Game mode (added by scrplay)

See [doc/game-mode.md](doc/game-mode.md) for the full manual (in Chinese).

 - keyboard/mouse mapped to synthetic touches: virtual joystick, tap/hold
   button bindings, repeated swipe gestures, mouse-driven camera aim
 - in-window ImGui editor: drag elements on screen, rebind keys, tune
   parameters; changes apply immediately and autosave (debounced)
 - profiles (`~/.config/scrplay/gameprofiles/*.profile`) with up to 12 presets
   each, hotkey switching (short press = next preset, long press = plain
   scrcpy controls), and 8 built-in gameplay templates (open world, driving,
   turn-based, shooter, MOBA, platformer, rhythm, empty)
 - run-time HUD: active preset and hotkey hints, preset picker while the mouse
   is free, auto-collapse during gameplay
 - anti-detection humanized input: ease-out joystick thumb gliding, random
   position/timing jitter with configurable amplitude, randomized tap
   duration; the aim finger exists only while the mouse is captured and is
   lifted as soon as capture is released
 - client-side crop editing per profile (bindings are normalized to the full
   raw frame, so cropping never moves existing bindings)
 - Alt temporarily releases mouse capture; Alt+left-drag injects a direct
   finger touch for un-mapped UI buttons

### Inherited from scrcpy v3.3.4

 - [audio forwarding](doc/audio.md) (Android 11+)
 - [recording](doc/recording.md)
 - [virtual display](doc/virtual_display.md)
 - mirroring with [Android device screen off](doc/device.md#turn-screen-off)
 - [copy-paste](doc/control.md#copy-paste) in both directions
 - [configurable quality](doc/video.md)
 - [camera mirroring](doc/camera.md) (Android 12+)
 - [mirroring as a webcam (V4L2)](doc/v4l2.md) (Linux-only)
 - physical [keyboard][hid-keyboard] and [mouse][hid-mouse] simulation (HID)
 - [gamepad](doc/gamepad.md) support
 - [OTG mode](doc/otg.md)
 - and more…

[hid-keyboard]: doc/keyboard.md#physical-keyboard-simulation
[hid-mouse]: doc/mouse.md#physical-mouse-simulation

## Prerequisites

The Android device requires at least API 21 (Android 5.0), with
[USB debugging][enable-adb] enabled, and you must accept the "Allow USB
debugging" authorization popup on the device (scrplay drives it through `adb`).

[enable-adb]: https://developer.android.com/studio/debug/dev-options#enable

On some devices (especially Xiaomi), controlling the device may additionally
require the `USB debugging (Security Settings)` developer option.

## Quick start (Linux, from source)

```bash
# runtime + build dependencies (Debian/Ubuntu)
sudo apt install ffmpeg libsdl2-2.0-0 adb libusb-1.0-0 \
                 gcc git pkg-config meson ninja-build libsdl2-dev \
                 libavcodec-dev libavdevice-dev libavformat-dev libavutil-dev \
                 libswresample-dev libusb-1.0-0-dev

git clone https://github.com/imzlh/scrplay
cd scrplay

# the prebuilt device-side server (server/scrplay-server) is tracked in the
# repository, so building the client needs no Android SDK
meson setup build -Dcompile_server=false
ninja -C build

# start game mode (development build)
./run-game.sh            # equivalent to: build/app/scrplay --game-mode
```

`run-game.sh` sets `SCRPLAY_SERVER_PATH`/`SCRPLAY_ICON_PATH` to the in-repo
server and icon, exports `SDL_IM_MODULE`/`XMODIFIERS` for fcitx5/ibus when
they are unset (so the editor can type Chinese under X11/XWayland), and execs
`build/app/scrplay --game-mode` with any extra arguments you pass. See
[doc/game-mode.md](doc/game-mode.md) for the CLI options, the editor, and the
profile file format.

To use scrplay as a plain mirror, run `./build/app/scrplay` (or
`./run x [options]`) without `--game-mode`; all upstream options and
[docs](#user-documentation) apply.

Full build instructions (other platforms, server from source, install):
[doc/build.md](doc/build.md).

## Configuration

| Path | Content |
| --- | --- |
| `~/.config/scrplay/gameprofiles/*.profile` | game mode profiles (one file per profile) |
| `~/.config/scrplay/gameui.ini` | ImGui window layout of the editor/HUD |
| `$XDG_CONFIG_HOME/scrplay/` | used instead when `XDG_CONFIG_HOME` is set |
| Windows | `%APPDATA%\scrplay\scrplay\` (SDL pref path) |

An existing `~/.config/scrcpy/` directory is migrated automatically to
`~/.config/scrplay/` on first run.

## User documentation

 - [Game mode (游戏模式手册)](doc/game-mode.md)
 - [Connection](doc/connection.md)
 - [Video](doc/video.md)
 - [Audio](doc/audio.md)
 - [Control](doc/control.md)
 - [Keyboard](doc/keyboard.md)
 - [Mouse](doc/mouse.md)
 - [Gamepad](doc/gamepad.md)
 - [Device](doc/device.md)
 - [Window](doc/window.md)
 - [Recording](doc/recording.md)
 - [Virtual display](doc/virtual_display.md)
 - [Tunnels](doc/tunnels.md)
 - [OTG](doc/otg.md)
 - [Camera](doc/camera.md)
 - [Video4Linux](doc/v4l2.md)
 - [Shortcuts](doc/shortcuts.md)
 - [Build](doc/build.md)
 - [Developers](doc/develop.md)

For troubleshooting, read the [FAQ](FAQ.md).

## Attribution

scrplay is based on [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy)
v3.3.4, licensed under the Apache License, Version 2.0, and retains the
upstream copyright notices. The device-side server binary
(`server/scrplay-server`) is an **unmodified upstream build**: it keeps the
`com.genymobile.scrcpy` Java package and the upstream wire protocol.

All credit for the mirroring and control core belongs to the upstream scrcpy
authors; see [CHANGELOG.md](CHANGELOG.md) for what this fork changes.

## License

    Copyright (C) 2018 Genymobile
    Copyright (C) 2018-2025 Romain Vimont

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
