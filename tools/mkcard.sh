#!/bin/sh
set -e
if [ $# -lt 2 ]; then
    echo "usage: mkcard.sh IMAGE SIZE_MB [DIR...]" >&2
    exit 2
fi
image=$1
size=$2
shift 2
if [ "$(uname -s)" != Darwin ]; then
    rm -f "$image"
    truncate -s "${size}M" "$image"
    echo 'start=1, type=06' | sfdisk --quiet --no-reread --no-tell-kernel "$image" >/dev/null
    sectors=$((size * 2048 - 1))
    mkfs.fat -F 16 -n JORNADA -s 2 -r 512 -R 1 -a -g 16/32 -h 1 --offset 1 "$image" $((sectors / 2)) >/dev/null
    for dir in "$@"; do mcopy -i "$image@@512" -s -Q "$dir" ::/; done
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
hdiutil create -quiet -size "${size}m" -layout MBRSPUD -fs "MS-DOS FAT16" -volname JORNADA -type UDIF "$work/card.dmg"
hdiutil convert -quiet "$work/card.dmg" -format UDTO -o "$work/card"
mv "$work/card.cdr" "$image"
if [ $# -gt 0 ]; then
    mount=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage "$image" | awk '/JORNADA/ {sub(/^.*\t/, ""); print}')
    touch "$mount/.metadata_never_index"
    for dir in "$@"; do cp -RX "$dir" "$mount/"; done
    rm -f "$mount/.metadata_never_index"
    find "$mount" -name '._*' -delete
    rm -rf "$mount/.fseventsd" "$mount/.Spotlight-V100" "$mount/.Trashes"
    hdiutil detach -quiet "$mount"
fi
