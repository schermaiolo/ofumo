# OFumo

Tiny Touhou Fumo desktop pet for Linux, inspired by the classic `oneko`.

OFumo keeps the oneko-style movement and idle state machine, replaces the cat
with Koishi animations, and adds a KDE Plasma Wayland backend while retaining
native X11 support.

## Features

- Koishi follows the pointer with oneko-style movement
- X11 support through Xlib/XShape
- KDE Plasma Wayland support through XWayland + a temporary KWin D-Bus bridge
- `-tofocus`, `-towindow`, `-toname`, and other oneko-compatible controls
- idle wink, sleep, disappear, click, spawn, and spin animations
- PNG sprites converted to 1-bit XBM + XShape masks at build time
- manual page installed as `man ofumo`

## Build

Arch Linux dependencies:

```bash
sudo pacman -S --needed \
    base-devel cmake libx11 libxext python-pillow systemd-libs
```

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Run

```bash
./build/ofumo
```

Follow the active window:

```bash
./build/ofumo -tofocus
```

List all options:

```bash
./build/ofumo -help
```

OFumo selects its backend automatically at startup:

- X11 session -> X11 backend
- KDE Plasma Wayland -> KWin bridge + XWayland rendering

Other Wayland compositors are not supported in `v0.0.1`.

## Install

```bash
sudo cmake --install build --prefix /usr
man ofumo
```

## Project Structure

```text
assets/        Koishi source PNG sprites
docs/          manual page
src/           OFumo runtime and KDE Wayland bridge
tools/         PNG-to-XBM build-time converter
CMakeLists.txt build and install configuration
```

## Credits

OFumo movement/state behaviour is derived from the classic public-domain
`oneko`. See [NOTICE.md](NOTICE.md) for provenance and artwork notes.

---

## Author

Daniel Fridman (Schermaiolo)
