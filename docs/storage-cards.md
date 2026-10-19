# Storage cards

Both slots take a CompactFlash card in ATA mode, backed by a raw disk image: the CompactFlash slot and the PC Card slot (as a CF card in an adapter would be). CE mounts the first card it finds as `\Storage Card` and a second as `\Storage Card2`; with both inserted at once, the PC Card slot's is `\Storage Card`. Make an image, optionally copying folders onto it, and insert it with `--card` (CompactFlash) or `--pc-card`, after `--load` if both are given:

```sh
tools/mkcard.sh card.img 64 ~/some/folder
./jornada720 --card=card.img
```

`make card` does the same (`CARD=card.img`, `CARD_MB=64`, `CARD_FILES=folders`). `mkcard.sh` needs mtools (`brew install mtools` or `apt install mtools`). It formats with `hdiutil` on macOS or `sfdisk` and `mkfs.fat` on Linux, copies with `mcopy`, then `tools/fatcase.py` gives every short lower-case name a long-name entry. mtools and macOS store those names with the NT case flags, which CE 3.0 ignores, so without it CE shows them in upper case. To read an image on the host, `mcopy -i card.img@@512`, or `hdiutil attach -imagekey diskimage-class=CRawDiskImage card.img` on macOS; eject it in the emulator first. Writes go straight to the image.

Cards can be inserted and ejected while CE runs (Devices, or dropping a `.img` file on the window for the CompactFlash slot); inserting a different image ejects the old one and inserts the new one a second later so CE sees the change. A saved state keeps its cards and reopens the images on load; a missing image is ejected. The Insert Card Image dialog opens in the `cards` folder of the data folder. headless takes `--card`, `--pc-card`, `--eject-card=T` and `--eject-pc-card=T`.

To install an app from a card, copy its CAB or EXE onto the image with `mkcard.sh` and open it from `\Storage Card` in My Handheld PC.
