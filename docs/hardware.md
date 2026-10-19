# Hardware notes

## Internal modem

The J720's internal V.90 modem is a Rockwell (Conexant) RP56-family modem data pump on the SA-1110's static bank 1 (0x08000000), with its 32 interface memory registers on address lines A2-A6. The modem controller itself, `mctl.dll` (Rockwell's "Galileo" HCF controller), runs on the StrongARM. At boot it powers the pump up, checks its ID, downloads 6502 code for the pump's controller and DSP code (eleven blocks, each confirmed with a rotate-and-add check byte), then configures it through DSP RAM. `src/core/rockwell_mdp.c` models enough of the pump for that: the ID, the download protocol, X and Y DSP RAM access and the new-configuration handshake. So CE boots without the 14 s of detection retries it spent before, and `COM6:` answers AT commands (`ATI3` reports "Hewlett Packard Internal V.90 Fax Modem, Ver Gal.2.2e"). There's no telephone line: dialling returns `OK` and then the controller stops answering, since the pump never reports dial tone or call progress.

## References

Hardware behaviour comes from the ARM ARM (DDI 0100E), Intel's SA-1110 (278240-004) and SA-1111 (278242-003) manuals, Epson's S1D13506/SED1356 specification and programming notes, HP's "Jornada 720 Hardware Configuration" porting note, the Linux drivers, and the ROM's own code. `reference/` (not in git) holds local copies; `reference/INDEX.md` lists sources and hashes.

Where sources disagree:

- Touch samples arrive as X1 X2 X3 Y1 Y2 Y3 Xhigh Yhigh. HP's note lists Xhigh after the X samples; Linux and HP's own CE driver use this order.
- The configuration EEPROM is on the SA-1111 SSP with chip select on SA-1111 GPIO B0, active low.

CE's ARM page-fault fast path keeps one loaded page per 1 MB section in the page table and relies on the TLB to hold the previous one, so the SA-1110's TLBs (two 32-entry, round robin, flushed only by cp15 c8) are modelled.

## Not done

Recording, IrDA, USB, dialling on the internal modem, cards other than CompactFlash storage and the NE2000 (modems, wireless cards) and battery charging state. Socket power from the SA-1111 GPIO A lines isn't modelled; a card answers whenever it's inserted. Batteries read fixed healthy values. CE resets the RTC on a cold boot; machines made with the clock setting on get the host's time instead.
