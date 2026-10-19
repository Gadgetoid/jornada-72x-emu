# Building

## Requirements

| Library | Needed for |
| --- | --- |
| SDL3, zlib | Required |
| libslirp | PC Link networking and the Ethernet card (optional) |
| libcurl | The web proxy for Pocket IE (optional) |

Without the optional libraries, the build still works without those features.

## macOS

```sh
brew install sdl3 libslirp
make
```

The app uses the system menu bar.

## Debian or Ubuntu

```sh
sudo apt install build-essential pkg-config libsdl3-dev libslirp-dev libcurl4-openssl-dev zlib1g-dev
make
```

On Debian 12 and Ubuntu 24.04, which lack `libsdl3-dev`, build SDL3 statically instead:

1. Install `cmake curl libglib2.0-dev` and the X11 and Wayland development packages listed in `.github/workflows/build.yml`.
2. `sh tools/sdl3-static.sh`
3. `PKG_CONFIG_PATH=build/sdl3/lib/pkgconfig make SDL_STATIC=1`

On Linux the menu bar is drawn in the window (`MENU=bar`, the default).

## Android

Build from macOS.

1. Install the tools:

   ```sh
   brew install --cask android-ndk android-commandlinetools
   brew install openjdk@21 meson ninja
   sdkmanager "platforms;android-35" "build-tools;35.0.0"
   ```

2. `make apk` builds `dist/jornada720.apk`. The first time, `tools/android-deps.sh` cross-builds GLib, libslirp, mbedTLS and curl into `build/android`.

| Command | What it does |
| --- | --- |
| `make apk-install` | Installs the APK over adb |
| `make apk-push` | Replaces a debug install's native code without reinstalling |

Without a release key (`APK_KEYSTORE`, `APK_KEYSTORE_PASSWORD`, `APK_KEY_ALIAS`), the APK is a debug build signed with `~/.android/debug.keystore`.

## Packages

| Command | Builds |
| --- | --- |
| `make app` | `Jornada720.app` (macOS) |
| `make deb` | The Debian package, with `mkcard.sh` in `/usr/share/jornada-72x-emu` |
| `make apk` | The Android APK |

## Running from the source folder

Run from the source folder, `./jornada720` also finds `rom/jornada720.bin` and `rom/jorn720_eeprom.bin`, so a checkout with those files needs no data folder set-up.
