# jornada-72x-emu

HP Jornada 720 emulator: an ARMv4 interpreter with the SA-1110, SA-1111, Epson SED1356 and HP's keyboard/touch microcontroller. Boots HP's Handheld PC 2000 ROM to the setup wizard.

## Build

Needs SDL3 and zlib. libslirp adds the PC Link network and libcurl the web proxy; without them the build still works.

```sh
make
```

Put the ROM and configuration EEPROM in `rom/` as `jornada720.bin` and `jorn720_eeprom.bin`.

## Run

```sh
./jornada720 [ROM] [EEPROM]
```

Mouse is the stylus. Keys map to the Jornada keyboard; right Alt is Fn, F12 is On/Off. A cold boot reaches the Welcome screen after about a minute.

State is saved on quit and every minute, and restored on launch with the clock advanced by the time away. `--fresh` cold boots instead and `--state=PATH` uses another state file. A state from an incompatible build is moved aside to `state.bin.old`.

- Cmd-S / Cmd-L: save / load state
- Cmd-R: soft reset
- Cmd-M: sound on/off
- Cmd-D: print CPU and interrupt status to the terminal
- Cmd-Q: quit

The state lives in `~/Library/Application Support/jornada-72x-emu/` on macOS.

## PC Link and debugmgr

`--net` plugs a serial cable into the PPP gateway. Start PC Link on the Jornada (Start > Programs > Communication > PC Link) and it connects at 57600. `./jornada-rapi` then talks to it over RAPI: `info`, `ls`, `put`, `get`, `run`, `reg` and more (`./jornada-rapi --help`).

`--agent=SOCKET` opens the host mailbox for jornada-72x-tools' debugmgr (`swi 0x51CE`). With both, the app installs debugmgr when PC Link connects: it copies `\Windows\debugmgr.exe`, sets `HKLM\Software\debugmgr` `Transport` to `mailbox`, adds an `HKLM\Init` launch entry and starts it. `jornada-debug --socket=SOCKET` then pings, copies files and runs programs. `make debugmgr` rebuilds the embedded copy from `../jornada-72x-tools`.

`--tcp[=PORT]` offers the serial port as raw bytes on a TCP port instead (default 9991).

`./headless --help` lists the scripted runner's options (screenshots, key presses, taps, traces, profiles). This calibrates the touch screen from a cold boot:

```sh
./headless --seconds=100 --key=62:0x4c --key=70:0x4c \
  --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 \
  --key=95:0x4c --screenshot=shot.png
```

## Tests

`make test` also runs `build/native-check`, which compares 10,000 randomized native memmove cases against libc, 256 calls against CE's ROM, 32 cache-sweep calls against CE, and verifies that a data-TLB miss is filled by native mapping. The boot suite cold boots, calibrates, completes the setup wizard, checks save/resume determinism and battery warnings, and compares an optimized 8-second cold boot with the baseline while requiring cache-sweep hits. It then launches Pocket Word, My Handheld PC, and Pocket Excel; each app must change the screen, match with optimisations on, exercise the fingerprinted memmove hook, and repeat deterministically. It starts PC Link, installs debugmgr over RAPI and pings it through the mailbox, then again after a soft reset. Finally it runs `jornada720 --net --agent` under SDL's dummy drivers, waits for it to install debugmgr by itself, quits it with SIGTERM and checks that the state was saved.

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

Recording, serial cable detection (PC Link has to be started by hand), IrDA, PC Card/CF, USB, suspend and battery charging state. Batteries read fixed healthy values. CE resets the RTC on a cold boot, so set the time once in CE; saved states keep it.

## Licence

MIT, see `LICENSE`.
