# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Initial scrplay release]

### Added

- **Game mode** (`--game-mode`): a desktop gaming control layer on top of the
  scrcpy mirroring core. The computer keyboard and mouse are mapped to
  human-like synthetic touches, so Android games and cloud games can be played
  from the desktop (tested with the cloud version of Honkai: Star Rail).
  - Virtual joystick, button bindings (tap/hold), repeated swipe gestures and
    a mouse-aim start point, all placed on screen through an ImGui editor.
  - Profiles (`~/.config/scrplay/gameprofiles/*.profile`) with up to 12
    presets each, plus 8 built-in gameplay templates (open world, driving,
    turn-based, shooter, MOBA, platformer, rhythm, empty).
  - Run-time HUD: preset/native mode badge, edit and preset hotkey hints,
    preset switcher while the mouse is free.
  - Anti-detection humanized input: ease-out joystick thumb gliding, random
    position and timing jitter (configurable amplitude), taps with randomized
    duration; the aim finger exists only while the mouse is captured and is
    lifted as soon as capture is released.
  - Client-side crop editing and per-profile hotkeys.
- In-window profile picker when several profiles exist, profile/preset
  creation, renaming and deletion, debounced autosave.
- Self-test mode (`SCRPLAY_GAME_SELFTEST`) scripting a fixed touch sequence
  for automated verification.

### Changed

- The product is renamed **scrplay** (it was scrcpy; this fork is based on
  [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy) v3.3.4):
  - the client binary is `build/app/scrplay`,
  - environment variables use the `SCRPLAY_*` prefix (e.g. `SCRPLAY_SERVER_PATH`),
  - the configuration directory is `~/.config/scrplay/` on Linux; an existing
    `~/.config/scrcpy/` directory is migrated automatically on first run,
  - the prebuilt server tracked in the repository is `server/scrplay-server`.

### Unchanged

- The device-side server binary is an **unmodified upstream build**: it keeps
  the `com.genymobile.scrcpy` Java package and the upstream wire protocol, so
  it behaves exactly like the scrcpy server.

[Initial scrplay release]: https://github.com/imzlh/scrplay
