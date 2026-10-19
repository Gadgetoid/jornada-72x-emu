# Hardware notes

## Emulated

An ARMv4 interpreter with the Intel SA-1110 and SA-1111, the Epson SED1356 display controller, HP's keyboard and touch microcontroller, the configuration EEPROM, CompactFlash cards, an NE2000 Ethernet card, and the internal modem's data pump.

## Not emulated yet

- Recording, IrDA and USB
- Dialling on the internal modem
- Cards other than CompactFlash storage and the NE2000, such as modem and wireless cards
- Battery charging state. Batteries read fixed healthy values.
- Socket power from the SA-1111 GPIO A lines. A card answers whenever it's inserted.

CE resets its clock on a cold boot. Machines made with the clock setting get the host's time instead.

## Internal modem

The J720's internal V.90 modem is a Rockwell (Conexant) RP56-family modem data pump on the SA-1110's static bank 1 (0x08000000), with its 32 interface memory registers on address lines A2-A6. The modem controller itself, `mctl.dll` (Rockwell's "Galileo" HCF controller), runs on the StrongARM.

At boot, `mctl.dll`:

1. Powers the pump up and checks its ID.
2. Downloads 6502 code for the pump's controller and DSP code: eleven blocks, each confirmed with a rotate-and-add check byte.
3. Configures it through DSP RAM.

`src/core/rockwell_mdp.c` models enough of the pump for that: the ID, the download protocol, X and Y DSP RAM access, and the new-configuration handshake. So CE boots without the 14 s of detection retries it spent before, and `COM6:` answers AT commands. `ATI3` reports "Hewlett Packard Internal V.90 Fax Modem, Ver Gal.2.2e".

There's no telephone line. Dialling returns `OK`, then the controller stops answering, because the pump never reports dial tone or call progress.

## TLBs

CE's ARM page-fault fast path keeps one loaded page per 1 MB section in the page table, and relies on the TLB to hold the previous one. So the SA-1110's TLBs are modelled: two 32-entry TLBs, round robin, flushed only by cp15 c8.

## References

Hardware behaviour comes from:

- the ARM ARM (DDI 0100E)
- Intel's SA-1110 (278240-004) and SA-1111 (278242-003) manuals
- Epson's S1D13506/SED1356 specification and programming notes
- HP's "Jornada 720 Hardware Configuration" porting note
- the Linux drivers
- the ROM's own code

`reference/` (not in git) holds local copies, and `reference/INDEX.md` lists sources and hashes.

### Where sources disagree

- **Touch sample order:** samples arrive as X1 X2 X3 Y1 Y2 Y3 Xhigh Yhigh. HP's note lists Xhigh after the X samples; Linux and HP's own CE driver use this order.
- **EEPROM:** the configuration EEPROM is on the SA-1111 SSP, with chip select on SA-1111 GPIO B0, active low.
