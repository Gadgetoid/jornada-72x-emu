# Tests

## Requirements

- The Jornada 720 ROM and EEPROM in `rom/` (`rom/jornada720.bin`, `rom/jorn720_eeprom.bin`).
- Optional: jornada-72x-tools at `../jornada-72x-tools`, or set `JORNADA_TOOLS`. Without it, the tests that build CE programs are skipped.
- Optional, for `make difftest`: `pip install unicorn`.

## Commands

| Command | What it does |
| --- | --- |
| `make test` | Runs `build/native-check` and the boot suite |
| `make format`, `make format-check` | Formats or checks the C code with the pinned uncrustify from `requirements-style.txt` |
| `make difftest` | Compares random ARM instructions against Unicorn's SA-1110 model |

## What `make test` covers

**native-check** compares the native memmove against libc (10,000 random cases) and against CE's ROM (256 calls), compares 32 cache-sweep calls with CE's, and checks that a data TLB miss is filled by native mapping.

**The boot suite:**

- **Boot:** cold boots, calibrates the touch screen and completes the setup wizard. Checks that resuming a saved state matches a continuous run, that there are no battery warnings, and that an optimised 8-second cold boot matches the normal one while using the cache-sweep hook.
- **Apps:** launches Pocket Word, My Handheld PC and Pocket Excel. Each must change the screen, match with optimisations on, use the memmove hook, and repeat exactly.
- **PC Link:** plugs in the cable, waits for PC Link, installs debugmgr over RAPI and pings it through the mailbox, then does it again after a soft reset.
- **Cards:** makes CompactFlash and PC Card images, checks CE mounts both, that files written on the host read back, that a file put over RAPI lands in the image, that lower-case names stay lower case, that saved states keep the cards, and that ejecting removes them.
- **With jornada-72x-tools:**
  - `tests/comecho` checks that bytes sent to `--tcp-com4` come back through CE's COM4.
  - `tests/atprobe` checks the internal modem answers `ATI3`.
  - `tests/tcpprobe` checks the Ethernet card gets a DHCP lease, then, after a soft reset with only the card, that CE exchanges a line with the host over TCP.
- **Power and input:** suspends and wakes with the On/Off key, types into Pocket Word, checks `--speed=2` runs more instructions and `--host-clock` sets the date.
- **App:** runs `jornada720 --net --agent --gdb` under SDL's dummy drivers, waits for it to install debugmgr itself, quits it and checks the state was saved. Then runs it with an empty data folder and checks it makes a first machine, saves its state and that `--tcp` listens.

## difftest

Unaligned accesses are skipped, because QEMU (behind Unicorn) doesn't follow the ARMv4 rules for them.
