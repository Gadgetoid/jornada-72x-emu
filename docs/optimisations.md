# Optimisations

## Usage

Native optimisations are off by default, except on Android. Turn them on with:

- `--optimisations` for `jornada720` or `headless` (remembered by the app), or
- Machine > CPU Speed > Optimisations.

## Always on

**Window animations.** gwes animates window minimise and restore by spinning on `GetTickCount` for 26 ms per step, which needs the full 206 MHz. The emulator recognises those two loops and skips to the next timer interrupt while the tick count hasn't reached its target, so animations keep their real speed on a slower host. Only CPU time is skipped, so this is on whatever the setting.

## With optimisations on

Each routine is recognised by a fingerprint of its code, and runs natively only when the fingerprint matches.

| Routine | Address | What it does natively |
| --- | --- | --- |
| `nk.exe` delay | `0x0004F06C` | Skips the OSCR busy wait to the first counter value that ends it, without passing the next scheduled event |
| `nk.exe` cache maintenance | `0x80075354` | Skips the D-cache and minicache sweep (870 instructions per call), since the emulator has no data cache to clean |
| coredll `memmove` | `0x01FD8A90` | Copies through translated guest pages, falling back to CE's code if either range can't be mapped |

headless reports each routine's hits and the work avoided.

### Details

- **Delay:** registers, flags, PC and guest time at return match the interpreted path.
- **Cache maintenance:** checks all nine ranges are mapped and keeps the final register and flag state. A `--count-pc=8007535c` run counted 11,654,400 executions in a 65-second boot, 0.294% of 3,965,578,378 instructions. At 45,296 calls, it skips about 39.4 million interpreted instructions, roughly 1% of boot work. (An earlier 1.7% figure was inflated by a profiler bug since fixed.)
- **memmove:** `build/native-check` compares it with CE's ROM implementation and with libc on random calls, and the app tests require it to be used in all three launches. The guest cycles headless reports for it are a timing estimate, not a measured host saving.

### Leads

Internet Explorer's hottest code is the DDI software `EmulatedBlt` loop around `0x00FF97xx`. A captured call uses masked `ROP4=0xCCAA` with a 1-bpp mask and a 16-bpp destination. Source-surface translation isn't worked out yet, so no DDI hook is enabled.

### Measurements

After the TLB-safe native mapper change, three 65-second cold-boot runs had median host times of 31.641 s without optimisations and 31.769 s with them, within run-to-run noise. No overall cold-boot speedup from these routines is demonstrated yet.
