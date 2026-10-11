#!/bin/sh
set -e
app=${1:-Jornada720.app}
version=${VERSION:-$(git describe --always --dirty 2>/dev/null || echo unknown)}
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/Resources"
cp jornada720 jornada-rapi "$app/Contents/MacOS/"

icons=${ICONS:-build/icons}
iconset=$(mktemp -d)/Jornada720.iconset
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    cp "$icons/icon-$size.png" "$iconset/icon_${size}x${size}.png"
    cp "$icons/icon-$((size * 2)).png" "$iconset/icon_${size}x${size}@2x.png"
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/Jornada720.icns"
rm -rf "$(dirname "$iconset")"

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Jornada 720</string>
    <key>CFBundleDisplayName</key><string>Jornada 720</string>
    <key>CFBundleIdentifier</key><string>org.jornada-emu.jornada</string>
    <key>CFBundleExecutable</key><string>jornada720</string>
    <key>CFBundleIconFile</key><string>Jornada720</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>Emulates the HP Jornada 720 Handheld PC. ROMs not included.</string>
</dict>
</plist>
EOF

external_libraries() {
    otool -L "$1" | awk 'NR > 1 { print $1 }' | grep -E '^(/opt/homebrew|/usr/local)/' || true
}

bundle() {
    for library in $(external_libraries "$1"); do
        name=$(basename "$library")
        install_name_tool -change "$library" "@rpath/$name" "$1" 2>/dev/null
        if [ ! -f "$app/Contents/Frameworks/$name" ]; then
            cp "$library" "$app/Contents/Frameworks/$name"
            chmod u+w "$app/Contents/Frameworks/$name"
            install_name_tool -id "@rpath/$name" "$app/Contents/Frameworks/$name" 2>/dev/null
            bundle "$app/Contents/Frameworks/$name"
        fi
    done
}

bundle "$app/Contents/MacOS/jornada720"
for binary in "$app/Contents/MacOS/jornada720" "$app/Contents/Frameworks/"*.dylib; do
    [ -f "$binary" ] || continue
    for path in $(otool -l "$binary" | awk '/LC_RPATH/ { found = 1 } found && $1 == "path" { print $2; found = 0 }'); do
        install_name_tool -delete_rpath "$path" "$binary" 2>/dev/null || true
    done
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$app/Contents/MacOS/jornada720" 2>/dev/null || true

for library in "$app/Contents/Frameworks/"*.dylib; do
    [ -f "$library" ] && codesign --force --sign - "$library"
done
codesign --force --sign - "$app/Contents/MacOS/jornada720"
codesign --force --sign - "$app"
echo "built $app ($version)"
