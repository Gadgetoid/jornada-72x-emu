# jornada-72x-emu

HP Jornada 720 emulator: an ARMv4 interpreter with the SA-1110, SA-1111, Epson SED1356 and HP's keyboard/touch microcontroller. Boots HP's Handheld PC 2000 ROM to the setup wizard.

## Build

Needs SDL3 and zlib.

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
- Cmd-R: reset
- Cmd-M: sound on/off
- Cmd-D: print CPU and interrupt status to the terminal
- Cmd-Q: quit

The state lives in `~/Library/Application Support/jornada-72x-emu/` on macOS.

`./headless --help` lists the scripted runner's options (screenshots, key presses, taps, traces, profiles). This calibrates the touch screen from a cold boot:

```sh
./headless --seconds=100 --key=62:0x4c --key=70:0x4c \
  --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 \
  --key=95:0x4c --screenshot=shot.png
```

## Tests

`make test` also runs `build/native-check`, which compares 10,000 randomized native memmove cases against libc, including overlap and page crossings, and checks atomic fallback for an unmapped destination. The boot suite cold boots, calibrates, completes the setup wizard, and checks save/resume determinism and battery warnings. It then launches Pocket Word, My Handheld PC, and Pocket Excel from the configured desktop; each app must change the screen, match with optimisations on, exercise the fingerprinted memmove hook, and produce the same screen on a repeated optimized run. Finally it runs `jornada720` on the desktop state under SDL's dummy drivers, quits it with SIGTERM and checks that the state was saved.

`make format` and `make format-check` run the pinned uncrustify from `requirements-style.txt`.

`make difftest` compares random ARM instructions against Unicorn's SA-1110 model (`pip install unicorn`). Unaligned accesses are skipped because QEMU doesn't follow the ARMv4 rules for them.

## Optimisations

Native optimisations are off by default. Pass `--optimisations` to `headless` or `jornada720` to enable them.

The `nk.exe` delay routine at `0x0004F06C` is recognized by its 12-word ARM instruction fingerprint. In a cold-boot `--profile=user` run, its compare/branch PC (`0x0004F088`) accounted for 20.6% of sampled PCs. It advances through the OSCR busy wait to the first counter value that makes the routine's own comparison succeed, without crossing the machine scheduler's next event. At return, registers, flags, PC and guest time match the interpreted path.

The exported coredll `memmove` at `0x01FD8A90` is also fingerprinted. It copies through translated guest pages, uses a direct mapped-page path when both ranges fit, and falls back before writing if either range cannot be mapped. `build/native-check` compares it with CE's ROM implementation on randomized calls as well as libc, and the app tests require actual hits in all three launches. In seven My Handheld PC timing samples, median host time was 0.953s off and 0.942s on; the ranges overlap, so this remains a preliminary, workload-specific result.

Profiling a Pocket Word launch also identified the DDI software `EmulatedBlt` routine at `0x00FF8C8C` (hot loop around `0x00FF97xx`). The observed operations used masks, and the prior-art native implementation safely declined them, so no DDI blit hook is enabled yet.

On this host, one 65-second cold boot took 32.262 seconds without optimisations and 30.937 seconds with them (4.1% lower host time); the screenshots were byte-identical. This is an initial single-run measurement, not a controlled average. The DDI profile's hot PCs were in `ddi.dll` (`0x00FF97xx`, about 0.6% per PC); its masked blits had no verified native hits, so no saving is claimed for that lead.

## References

Hardware behaviour comes from the ARM ARM (DDI 0100E), Intel's SA-1110 (278240-004) and SA-1111 (278242-003) manuals, Epson's S1D13506/SED1356 specification and programming notes, HP's "Jornada 720 Hardware Configuration" porting note, the Linux drivers, and the ROM's own code. `reference/` (not in git) holds local copies; `reference/INDEX.md` lists sources and hashes.

Where sources disagree:

- Touch samples arrive as X1 X2 X3 Y1 Y2 Y3 Xhigh Yhigh. HP's note lists Xhigh after the X samples; Linux and HP's own CE driver use this order.
- The configuration EEPROM is on the SA-1111 SSP with chip select on SA-1111 GPIO B0, active low.

CE's ARM page-fault fast path keeps one loaded page per 1 MB section in the page table and relies on the TLB to hold the previous one, so the SA-1110's TLBs (two 32-entry, round robin, flushed only by cp15 c8) are modelled.

## Not done

Recording, serial, IrDA, PC Card/CF, USB, suspend and battery charging state. Batteries read fixed healthy values. CE resets the RTC on a cold boot, so set the time once in CE; saved states keep it.

## Licence

MIT, see `LICENSE`.
