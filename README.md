# jornada-72x-emu

An emulator for the HP Jornada 720 Handheld PC. It boots HP's Handheld PC 2000 ROM, with the keyboard, touch screen, sound, CompactFlash and PC Card storage, an Ethernet card, PC Link over PPP and the internal modem's AT commands.

## Install

- **macOS (Apple silicon):** `Jornada720.app` from a [release](https://github.com/Gadgetoid/jornada-72x-emu/releases). It isn't notarised: allow it in System Settings > Privacy & Security > Open Anyway, or run `xattr -dr com.apple.quarantine Jornada720.app`.
- **Debian 12 or later, Ubuntu 24.04 or later:** the `.deb` from a release. It installs `jornada720`, `jornada720-headless` and `jornada-rapi`.
- **Android (arm64, Android 9 or later):** the `.apk` from a release. See [docs/android.md](docs/android.md).

To build it yourself, see [docs/building.md](docs/building.md).

## Getting started

No ROMs are included. You need the Jornada 720's ROM and, if you have it, its 256-byte configuration EEPROM.

1. Put the ROM (and EEPROM) in the `roms` folder of the data folder, making the folders if they don't exist: `~/Library/Application Support/jornada-72x-emu/roms` on macOS, `~/.local/share/jornada-72x-emu/roms` on Linux. On Android the app asks for them on its first run.
2. Start `jornada720`. The first run makes a machine from the ROM it finds. Machine > New Machine makes more later; without an EEPROM a machine starts with a blank one, which CE fills in.

A cold boot reaches the Welcome screen in a few seconds. The mouse is the stylus and the scroll wheel sends arrow keys. Keys map to the Jornada keyboard, right Alt is Fn and F12 is the On/Off key. The machine's state is saved when you quit and every minute.

## Command-line options

`jornada720 [OPTIONS] [ROM [EEPROM]]`. With no ROM it opens the last machine used. `--help` lists every option.

| Option | What it does |
| --- | --- |
| `--machine=NAME` | Open a machine by name |
| `--fresh` | Ignore the saved state and cold boot |
| `--state=FILE` | Use FILE for the saved state instead of the machine's own |
| `--card=IMAGE` | Insert a CompactFlash card image ([storage cards](docs/storage-cards.md)) |
| `--pc-card=IMAGE` | Insert a card image in the PC Card slot |
| `--ethernet` | Insert an Ethernet card on its own network ([networking](docs/networking.md)) |
| `--net` | Plug the serial port into the PPP network for PC Link and RAPI |
| `--tcp[=PORT]` | Offer the serial port as raw bytes on a TCP port (default 9991) |
| `--speed=1\|2` | CPU speed against the timers |
| `--optimisations[=on\|off]` | Run some of CE's routines natively ([optimisations](docs/optimisations.md)) |
| `--gdb=PORT` | Listen for GDB ([debugging](docs/debugging.md)) |

## Documentation

- [Using the emulator](docs/using.md): menus, the data folder, states and the clock
- [Storage cards](docs/storage-cards.md): making card images and installing software
- [Networking and PC Link](docs/networking.md): PPP, RAPI, the Ethernet card and serial ports over TCP
- [Debugging](docs/debugging.md): debugmgr, GDB and the headless runner
- [Android](docs/android.md)
- [Building](docs/building.md)
- [Hardware notes](docs/hardware.md): the internal modem, references and what isn't emulated yet
- [Optimisations](docs/optimisations.md)
- [Tests](docs/testing.md)

## Licence

MIT, see `LICENSE`.
