# Storage cards

The Jornada 720 has a CompactFlash slot and a PC Card slot. The emulator puts a CompactFlash card in ATA mode in either one (in the PC Card slot, as a CF card in an adapter would be). Each card is backed by a raw disk image file on the host.

CE mounts the first card it finds as `\Storage Card` and a second as `\Storage Card2`. With both inserted at once, the PC Card slot's card is `\Storage Card`.

## Requirements

`tools/mkcard.sh` makes card images. It needs:

| Platform | Install |
| --- | --- |
| macOS | `brew install mtools` (it also uses `hdiutil`, which is built in) |
| Debian or Ubuntu | `sudo apt install mtools dosfstools fdisk` |

Both also need `python3`.

## Making a card

```sh
tools/mkcard.sh card.img 64 ~/some/folder
```

That makes a 64 MB image and copies `~/some/folder` onto it. Folders are optional, and you can pass more than one.

`make card` does the same with `CARD=card.img`, `CARD_MB=64` and `CARD_FILES="folders"`.

## Inserting and ejecting

| Where | CompactFlash slot | PC Card slot |
| --- | --- | --- |
| Command line | `--card=IMAGE` | `--pc-card=IMAGE` |
| Devices menu | Insert Card Image (Cmd-O), Eject Card (Cmd-E) | The same with Shift |
| Drag and drop | Drop a `.img` file on the window | |
| headless | `--card=IMAGE`, `--eject-card=SECONDS` | `--pc-card=IMAGE`, `--eject-pc-card=SECONDS` |

- Cards can be inserted and ejected while CE runs. Inserting a different image ejects the old one and inserts the new one a second later, so CE sees the change.
- The Insert Card Image dialog opens in the `cards` folder of the data folder.
- A saved state keeps its cards and reopens the images when it loads. A missing image is ejected.
- In headless, give `--card` and `--pc-card` after `--load` if you use both.

## Installing software

Copy the app's CAB or EXE onto an image with `mkcard.sh`, insert it, and open it from `\Storage Card` in My Handheld PC.

## Reading an image on the host

Eject the card in the emulator first, then:

- `mcopy -i card.img@@512 ::/file.txt .` (any platform with mtools), or
- `hdiutil attach -imagekey diskimage-class=CRawDiskImage card.img` (macOS).

Writes go straight to the image file.

## How images are made

- `mkcard.sh` formats a FAT16 partition with `hdiutil` on macOS, or `sfdisk` and `mkfs.fat` on Linux, then copies files in with `mcopy`.
- `tools/fatcase.py` then gives every short lower-case name, such as `readme.txt`, a long-name entry. mtools and macOS store those names using NT case flags, which CE 3.0 ignores, so without this step CE shows them in upper case.
