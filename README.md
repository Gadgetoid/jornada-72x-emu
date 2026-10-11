# jornada-72x-emu

HP Jornada 720 emulator: an ARMv4 interpreter with the SA-1110, SA-1111, Epson SED1356 and HP's keyboard/touch microcontroller. Boots HP's Handheld PC 2000 ROM to the setup wizard.

## Install

- **macOS (Apple silicon):** `Jornada720.app`, from a release or `make app`. It isn't notarised: allow it in System Settings > Privacy & Security > Open Anyway, or run `xattr -dr com.apple.quarantine Jornada720.app`.
- **Debian 12 or later and Ubuntu 24.04 or later:** the `.deb`, from a release or `make deb`. It installs `jornada720`, `jornada720-headless` and `jornada-rapi`, with a desktop entry.
- **Android (arm64, Android 9 or later):** the `.apk`, from a release or `make apk`. See Android.

## Build

Needs SDL3 and zlib. libslirp adds the PC Link network and libcurl the web proxy; without them the build still works.

macOS: `brew install sdl3 libslirp`, then `make`. Debian or Ubuntu: `sudo apt install build-essential pkg-config libsdl3-dev libslirp-dev libcurl4-openssl-dev zlib1g-dev`, then `make`. Without `libsdl3-dev` (Debian 12, Ubuntu 24.04), install `cmake curl libglib2.0-dev` and the X11 and Wayland development packages listed in `.github/workflows/build.yml`, then `sh tools/sdl3-static.sh` and `PKG_CONFIG_PATH=build/sdl3/lib/pkgconfig make SDL_STATIC=1`. Linux gets a menu bar drawn in the window (`MENU=bar`, the default there); macOS uses the system menu bar.

Android, from macOS: `brew install --cask android-ndk android-commandlinetools`, `brew install openjdk@21 meson ninja` and `sdkmanager "platforms;android-35" "build-tools;35.0.0"`, then `make apk` for `dist/jornada720.apk`. `tools/android-deps.sh` cross-builds GLib, libslirp, mbedTLS and curl into `build/android` the first time. Without a release key (`APK_KEYSTORE`, `APK_KEYSTORE_PASSWORD`, `APK_KEY_ALIAS`), the APK is a debug build signed with `~/.android/debug.keystore`. `make apk-install` installs it over adb; `make apk-push` replaces a debug install's native code without reinstalling.

## Run

No ROMs are included. Put the ROM and, if you have it, its configuration EEPROM (256 bytes) in the `roms` folder of the data folder, then make a machine with Machine > New Machine. Without an EEPROM the machine starts with a blank one, which CE fills in. Run from this folder, `./jornada720` also finds `rom/jornada720.bin` and `rom/jorn720_eeprom.bin`.

```sh
./jornada720 [OPTIONS] [ROM [EEPROM]]
```

With no ROM it opens the last machine used. The first run makes a machine from the ROM it finds, keeping an existing `state.bin` from earlier versions as that machine's state. With a ROM it runs that ROM with its own state, outside the machine list. `./jornada720 --help` lists the options.

The data folder is `$XDG_DATA_HOME/jornada-72x-emu` (otherwise `~/Library/Application Support/jornada-72x-emu` on macOS, `~/.local/share/jornada-72x-emu` elsewhere). It holds the ROMs, machines, states and snapshots. Settings are in `jornada720.ini` in `$XDG_CONFIG_HOME/jornada-72x-emu` (otherwise the data folder on macOS, `~/.config/jornada-72x-emu` elsewhere).

Mouse is the stylus, and the scroll wheel sends arrow keys. Keys map to the Jornada keyboard; right Alt is Fn, F12 is On/Off. A cold boot reaches the Welcome screen after about a minute.

The menus replace the old Cmd keys:

- Machine: Power Button (the On/Off key, which suspends and wakes the machine), Soft Reset (Cmd-R), the machines, New Machine and Manage Machines (reset or delete), Pause (Cmd-P), CPU Speed (1x or 2x the CPU clock against the timers, and Optimisations), GDB Server.
- State: Save State (Cmd-S), Load State (Cmd-L), snapshots to and from any file, Show State Folder. The state is also saved on quit and every minute, a backup goes in `snapshots/Backups` every 10 minutes and before a load or reset, and the clock is advanced by the time away. An unreadable state is moved aside to `.old`.
- Edit: Copy Screen, Paste as Typing, Save Screenshot.
- View: 50% to 200%, Zoom In and Out, Full Screen.
- Devices: the serial port (not connected, PPP network, pseudo-terminal, TCP port or a host serial port), Sound.
- PC Link: send files, copy My Documents back, a shared folder synced with My Documents, Pocket IE proxy, RAPI over the network, connection speed.

New Machine's clock setting sets CE's clock from the host's local time at the first boot, as CE clears it then. `--fresh` cold boots and `--state=PATH` uses another state file. `--speed` and `--optimisations` set the CPU speed and optimisations and are remembered.

## PC Link and debugmgr

`--net`, or Devices > Network (PPP), plugs a serial cable into the PPP gateway, and is remembered. CE sees the cable (GPIO 15 goes low) and starts PC Link, which connects at 57600. `./jornada-rapi` then talks to it over RAPI: `info`, `ls`, `put`, `get`, `run`, `reg` and more (`./jornada-rapi --help`).

`--agent=SOCKET` opens the host mailbox for jornada-72x-tools' debugmgr (`swi 0x51CE`). With either `--agent` or `--gdb`, the app installs debugmgr when PC Link connects: it copies `\Windows\debugmgr.exe`, sets `HKLM\Software\debugmgr` `Transport` to `mailbox`, adds an `HKLM\Init` launch entry and starts it. It prints `debugmgr: ready` once debugmgr polls the mailbox. `jornada-debug --socket=SOCKET` then pings, copies files and runs programs. `make debugmgr` rebuilds the embedded copy from `../jornada-72x-tools`.

`--gdb=PORT` listens for GDB on 127.0.0.1 (`target extended-remote :PORT`, `set architecture armv4`); it can attach and detach while the emulator runs, and `--gdb-process=NAME` limits user-space breakpoints to one process. Registers, memory per process, breakpoints, stepping, Ctrl-C, the CE module list as shared libraries, and crashes CE reports (as SIGSEGV with the faulting registers) work. With debugmgr running, `remote put`/`get`/`delete` and `run`/`kill` go through it. Hardware watchpoints aren't supported. headless takes the same options, waits for GDB before running and runs until GDB detaches or kills it.

Machine > GDB Server listens on all interfaces on port 1234 (`gdb_port` in `jornada720.ini`) instead.

`--tcp[=PORT]`, or Devices > TCP Port, offers the serial port as raw bytes on a TCP port instead (default 9991).

`./headless --help` lists the scripted runner's options (screenshots, key presses, taps, traces, profiles). This calibrates the touch screen from a cold boot:

```sh
./headless --seconds=100 --key=62:0x4c --key=70:0x4c \
  --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 \
  --key=95:0x4c --screenshot=shot.png
```

## Android

The first run asks for the ROM and EEPROM, from the phone's storage or Downloads, and copies them into the app; Machine > Import ROMs adds more later. A finger is the stylus. In landscape the controls run down both sides of the screen, and in portrait across the top: Hide, Menu (or Back) for the menus, Esc, Tab, Ctrl, Alt and Shift (which stay down until tapped again), Kbd for the phone's keyboard, Power for the On/Off key, the arrows and Enter. Optimisations are on by default. Files live under `Android/data/org.jornada_emu.jornada/files/jornada-72x-emu`. Leaving the app saves the machine.

## Tests

`make test` also runs `build/native-check`, which compares 10,000 randomized native memmove cases against libc, 256 calls against CE's ROM, 32 cache-sweep calls against CE, and verifies that a data-TLB miss is filled by native mapping. The boot suite cold boots, calibrates, completes the setup wizard, checks save/resume determinism and battery warnings, and compares an optimized 8-second cold boot with the baseline while requiring cache-sweep hits. It then launches Pocket Word, My Handheld PC, and Pocket Excel; each app must change the screen, match with optimisations on, exercise the fingerprinted memmove hook, and repeat deterministically. It plugs in the cable, waits for PC Link to start, installs debugmgr over RAPI and pings it through the mailbox, then again after a soft reset. It suspends and wakes the machine with the On/Off key, types into Pocket Word, checks that `--speed=2` runs more instructions and that `--host-clock` sets the date. Finally it runs `jornada720 --net --agent --gdb` under SDL's dummy drivers, waits for it to install debugmgr by itself, quits it with SIGTERM and checks that the state was saved, then runs it with an empty data folder holding an old `state.bin` and checks that the first machine keeps it and that `--tcp` listens.

`make format` and `make format-check` run the pinned uncrustify from `requirements-style.txt`.

`make difftest` compares random ARM instructions against Unicorn's SA-1110 model (`pip install unicorn`). Unaligned accesses are skipped because QEMU doesn't follow the ARMv4 rules for them.

## Optimisations

Native optimisations are off by default. Pass `--optimisations` to `headless` or `jornada720` to enable them.

The `nk.exe` delay routine at `0x0004F06C` is recognized by its 12-word ARM instruction fingerprint. It advances through the OSCR busy wait to the first counter value that makes the routine's own comparison succeed, without crossing the machine scheduler's next event. At return, registers, flags, PC and guest time match the interpreted path. Headless reports poll skips and guest cycles advanced.

The `nk.exe` cache-maintenance helper at `0x80075354` sweeps the 8 KiB main D-cache and 512-byte minicache with 32-byte-stride loads before separate CP15 invalidations. The emulator has no SA-1110 D-cache to clean, so this fingerprinted native hook verifies all nine ranges are mapped, preserves the final register/flag state, and skips exactly 870 interpreted instructions per hit. A `--count-pc=8007535c` run recorded 11,654,400 executions over a 65-second boot, 0.294% of 3,965,578,378 instructions. At 45,296 calls, it skips about 39.4M interpreted instructions (roughly 1% of boot work). Headless reports that expected work avoided. The earlier 1.7% sample was inflated because the full profile table omitted new PCs from its denominator after it filled; the denominator now includes every sample attempt.

The exported coredll `memmove` at `0x01FD8A90` is also fingerprinted. It copies through TLB-filling translated guest pages, uses a direct mapped-page path when both ranges fit, and falls back before writing if either range cannot be mapped. `build/native-check` compares it with CE's ROM implementation on randomized calls as well as libc, and the app tests require actual hits in all three launches. Headless reports its estimated guest cycles charged; this is a timing estimate, not a demonstrated host-time saving.

The corrected Internet Explorer profile concentrates its hottest sampled PCs in the DDI software `EmulatedBlt` loop around `0x00FF97xx`. A captured call uses masked `ROP4=0xCCAA` with a 1-bpp mask and 16-bpp destination; source-surface translation is not yet established, so no DDI hook is enabled. This is the current higher-upside lead.

After the TLB-safe native mapper change, three 65-second cold-boot runs had median host times of 31.641 seconds without optimisations and 31.769 seconds with them. The difference is within run-to-run noise; no overall cold-boot speedup is demonstrated yet.

## References

Hardware behaviour comes from the ARM ARM (DDI 0100E), Intel's SA-1110 (278240-004) and SA-1111 (278242-003) manuals, Epson's S1D13506/SED1356 specification and programming notes, HP's "Jornada 720 Hardware Configuration" porting note, the Linux drivers, and the ROM's own code. `reference/` (not in git) holds local copies; `reference/INDEX.md` lists sources and hashes.

Where sources disagree:

- Touch samples arrive as X1 X2 X3 Y1 Y2 Y3 Xhigh Yhigh. HP's note lists Xhigh after the X samples; Linux and HP's own CE driver use this order.
- The configuration EEPROM is on the SA-1111 SSP with chip select on SA-1111 GPIO B0, active low.

CE's ARM page-fault fast path keeps one loaded page per 1 MB section in the page table and relies on the TLB to hold the previous one, so the SA-1110's TLBs (two 32-entry, round robin, flushed only by cp15 c8) are modelled.

## Not done

Recording, IrDA, PC Card/CF, USB and battery charging state. Batteries read fixed healthy values. CE resets the RTC on a cold boot; machines made with the clock setting on get the host's time instead.

## Licence

MIT, see `LICENSE`.
