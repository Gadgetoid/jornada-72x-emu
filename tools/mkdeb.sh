#!/bin/sh
set -e
out=${1:-dist}
version=$(printf "%s" "${VERSION:-0.1+git$(date +%Y%m%d)}" | tr - "~")
arch=$(dpkg --print-architecture)
package=jornada-72x-emu
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

make -s all icons
icons=${BUILD:-build}/icons

install -D -m 755 jornada720 "$root/usr/bin/jornada720"
install -D -m 755 headless "$root/usr/bin/jornada720-headless"
install -D -m 755 jornada-rapi "$root/usr/bin/jornada-rapi"
install -D -m 755 tools/mkcard.sh "$root/usr/share/$package/mkcard.sh"
install -D -m 755 tools/fatcase.py "$root/usr/share/$package/fatcase.py"
install -D -m 644 README.md "$root/usr/share/doc/$package/README.md"
for doc in docs/*.md; do install -D -m 644 "$doc" "$root/usr/share/doc/$package/$doc"; done
install -D -m 644 LICENSE "$root/usr/share/doc/$package/LICENSE"
install -D -m 644 assets/icon.svg "$root/usr/share/icons/hicolor/scalable/apps/$package.svg"
for size in 16 32 64 128 256 512; do install -D -m 644 "$icons/icon-$size.png" "$root/usr/share/icons/hicolor/${size}x${size}/apps/$package.png"; done
for licence in licences/*; do install -D -m 644 "$licence" "$root/usr/share/doc/$package/licences/$(basename "$licence")"; done

cat > "$root/usr/share/doc/$package/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: jornada-72x-emu
Comment: No ROMs are included.

Files: *
Copyright: 2026 Phil Howard
License: MIT
 See /usr/share/doc/$package/LICENSE

Files: src/vendor/stb_truetype.h src/vendor/stb_image.h
License: public-domain
 stb_truetype and stb_image by Sean Barrett, public domain.

Files: src/vendor/nanosvg.h src/vendor/nanosvgrast.h
License: Zlib
 See /usr/share/doc/$package/licences/Zlib-nanosvg.txt
EOF

mkdir -p "$root/usr/share/applications"
cat > "$root/usr/share/applications/$package.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Jornada 720
Comment=HP Jornada 720 emulator
Exec=jornada720
Icon=$package
StartupWMClass=jornada-72x-emu
Terminal=false
Categories=Emulator;Game;
EOF

work=$(mktemp -d)
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$package" "$package" > "$work/debian/control"
depends=$(cd "$work" && dpkg-shlibdeps -O "$root/usr/bin/jornada720" "$root/usr/bin/jornada720-headless" "$root/usr/bin/jornada-rapi" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
rm -rf "$work"

size=$(du -sk "$root/usr" | cut -f1)
mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: $package
Version: $version
Architecture: $arch
Maintainer: Phil Howard <phil@pimoroni.com>
Installed-Size: $size
Depends: $depends
Recommends: mtools, dosfstools, fdisk, python3, fonts-dejavu-core | fonts-noto-core
Section: otherosfs
Priority: optional
Description: HP Jornada 720 emulator
 Emulates the HP Jornada 720 Handheld PC: the StrongARM SA-1110 and
 SA-1111, the Epson SED1356 display controller and HP's keyboard and
 touch microcontroller, with a serial port and a PPP network.
 .
 jornada720 is the emulator, jornada720-headless runs it without a window
 for tests and scripts, and jornada-rapi talks to a running device over
 RAPI. ROMs are not included.
EOF

mkdir -p "$out"
dpkg-deb --root-owner-group --build "$root" "$out/${package}_${version}_${arch}.deb" >/dev/null
echo "$out/${package}_${version}_${arch}.deb"
