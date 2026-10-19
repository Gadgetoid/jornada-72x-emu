# Optimisations

gwes animates window minimise and restore by spinning on `GetTickCount` for 26 ms per step, which needs the full 206 MHz. Those two loops are fingerprinted and skip to the next timer interrupt while the tick count hasn't reached its target, so the animation keeps its real speed on a slower host. Only CPU time is skipped, so this is on whether or not optimisations are.

Native optimisations are off by default. Pass `--optimisations` to `headless` or `jornada720` to enable them.

The `nk.exe` delay routine at `0x0004F06C` is recognized by its 12-word ARM instruction fingerprint. It advances through the OSCR busy wait to the first counter value that makes the routine's own comparison succeed, without crossing the machine scheduler's next event. At return, registers, flags, PC and guest time match the interpreted path. Headless reports poll skips and guest cycles advanced.

The `nk.exe` cache-maintenance helper at `0x80075354` sweeps the 8 KiB main D-cache and 512-byte minicache with 32-byte-stride loads before separate CP15 invalidations. The emulator has no SA-1110 D-cache to clean, so this fingerprinted native hook verifies all nine ranges are mapped, preserves the final register/flag state, and skips exactly 870 interpreted instructions per hit. A `--count-pc=8007535c` run recorded 11,654,400 executions over a 65-second boot, 0.294% of 3,965,578,378 instructions. At 45,296 calls, it skips about 39.4M interpreted instructions (roughly 1% of boot work). Headless reports that expected work avoided. The earlier 1.7% sample was inflated because the full profile table omitted new PCs from its denominator after it filled; the denominator now includes every sample attempt.

The exported coredll `memmove` at `0x01FD8A90` is also fingerprinted. It copies through TLB-filling translated guest pages, uses a direct mapped-page path when both ranges fit, and falls back before writing if either range cannot be mapped. `build/native-check` compares it with CE's ROM implementation on randomized calls as well as libc, and the app tests require actual hits in all three launches. Headless reports its estimated guest cycles charged; this is a timing estimate, not a demonstrated host-time saving.

The corrected Internet Explorer profile concentrates its hottest sampled PCs in the DDI software `EmulatedBlt` loop around `0x00FF97xx`. A captured call uses masked `ROP4=0xCCAA` with a 1-bpp mask and 16-bpp destination; source-surface translation is not yet established, so no DDI hook is enabled. This is the current higher-upside lead.

After the TLB-safe native mapper change, three 65-second cold-boot runs had median host times of 31.641 seconds without optimisations and 31.769 seconds with them. The difference is within run-to-run noise; no overall cold-boot speedup is demonstrated yet.
