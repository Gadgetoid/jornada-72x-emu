# Building

Needs SDL3 and zlib. libslirp adds the PC Link network and libcurl the web proxy; without them the build still works.

macOS: `brew install sdl3 libslirp`, then `make`. Debian or Ubuntu: `sudo apt install build-essential pkg-config libsdl3-dev libslirp-dev libcurl4-openssl-dev zlib1g-dev`, then `make`. Without `libsdl3-dev` (Debian 12, Ubuntu 24.04), install `cmake curl libglib2.0-dev` and the X11 and Wayland development packages listed in `.github/workflows/build.yml`, then `sh tools/sdl3-static.sh` and `PKG_CONFIG_PATH=build/sdl3/lib/pkgconfig make SDL_STATIC=1`. Linux gets a menu bar drawn in the window (`MENU=bar`, the default there); macOS uses the system menu bar.

Android, from macOS: `brew install --cask android-ndk android-commandlinetools`, `brew install openjdk@21 meson ninja` and `sdkmanager "platforms;android-35" "build-tools;35.0.0"`, then `make apk` for `dist/jornada720.apk`. `tools/android-deps.sh` cross-builds GLib, libslirp, mbedTLS and curl into `build/android` the first time. Without a release key (`APK_KEYSTORE`, `APK_KEYSTORE_PASSWORD`, `APK_KEY_ALIAS`), the APK is a debug build signed with `~/.android/debug.keystore`. `make apk-install` installs it over adb; `make apk-push` replaces a debug install's native code without reinstalling.

## Packages

`make app` builds `Jornada720.app` on macOS, `make deb` the Debian package (with `mkcard.sh` in `/usr/share/jornada-72x-emu`), and `make apk` the Android APK as above.

## Running from the source folder

Run from this folder, `./jornada720` also finds `rom/jornada720.bin` and `rom/jorn720_eeprom.bin`, so a checkout with those files needs no data folder set-up.
