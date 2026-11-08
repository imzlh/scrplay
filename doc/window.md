# Window

## Disable window

To disable window (may be useful for recording or for playing audio only):

```bash
scrplay --no-window --record=file.mp4
# Ctrl+C to interrupt
```

## Title

By default, the window title is the device model. It can be changed:

```bash
scrplay --window-title='My device'
```

## Position and size

The initial window position and size may be specified:

```bash
scrplay --window-x=100 --window-y=100 --window-width=800 --window-height=600
```

## Borderless

To disable window decorations:

```bash
scrplay --window-borderless
```

## Always on top

To keep the window always on top:

```bash
scrplay --always-on-top
```

## Fullscreen

The app may be started directly in fullscreen:

```bash
scrplay --fullscreen
scrplay -f   # short version
```

Fullscreen mode can then be toggled dynamically with <kbd>MOD</kbd>+<kbd>f</kbd>
(see [shortcuts](shortcuts.md)).


## Disable screensaver

By default, _scrplay_ does not prevent the screensaver from running on the
computer. To disable it:

```bash
scrplay --disable-screensaver
```
