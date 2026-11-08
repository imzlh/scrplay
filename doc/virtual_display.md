# Virtual display

## New display

To mirror a new virtual display instead of the device screen:

```bash
scrplay --new-display=1920x1080
scrplay --new-display=1920x1080/420  # force 420 dpi
scrplay --new-display         # use the main display size and density
scrplay --new-display=/240    # use the main display size and 240 dpi
```

The new virtual display is destroyed on exit.

## Start app

On some devices, a launcher is available in the virtual display.

When no launcher is available (or if is explicitly disabled by
[`--no-vd-system-decorations`](#system-decorations)), the virtual display is
empty. In that case, you must [start an Android
app](device.md#start-android-app).

For example:

```bash
scrplay --new-display=1920x1080 --start-app=org.videolan.vlc
```

The app may itself be a launcher. For example, to run the open source [Fossify
Launcher]:

```bash
scrplay --new-display=1920x1080 --no-vd-system-decorations --start-app=org.fossify.home
```

[Fossify Launcher]: https://f-droid.org/en/packages/org.fossify.home/


## System decorations

By default, virtual display system decorations are enabled. To disable them, use
`--no-vd-system-decorations`:

```
scrplay --new-display --no-vd-system-decorations
```

This is useful for some devices which might display a broken UI, or to disable
any default launcher UI available in virtual displays.

Note that if no app is started, no content will be rendered, so no video frame
will be produced at all.


## Destroy on close

By default, when the virtual display is closed, the running apps are destroyed.

To move them to the main display instead, use:

```
scrplay --new-display --no-vd-destroy-content
```


## Display IME policy

By default, the virtual display IME appears on the default display.

To make it appear on the local display, use `--display-ime-policy=local`:

```bash
scrplay --display-id=1 --display-ime-policy=local
scrplay --new-display --display-ime-policy=local
```
