#!/usr/bin/env bash
# Builds the USB installer site (web/ + the flash images + manifest.json) into a directory.
#   scripts/build-site.sh <version> <output dir> [pio build dir] [boot_app0.bin]
# The release workflow publishes the result on GitHub Pages (branch gh-pages).
set -euo pipefail

version="${1:?version, e.g. 1.2.0}"
out="${2:?output directory}"
build="${3:-.pio/build/seeed_xiao_esp32c3}"
boot_app0="${4:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin}"

for f in "$build/bootloader.bin" "$build/partitions.bin" "$build/firmware.bin" "$boot_app0"; do
  [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

rm -rf "$out"
mkdir -p "$out"
cp web/index.html web/icon.png "$out/"
cp "$build/bootloader.bin" "$build/partitions.bin" "$build/firmware.bin" "$out/"
cp "$boot_app0" "$out/boot_app0.bin"
touch "$out/.nojekyll"
python3 scripts/changelog.py json > "$out/changelog.json"

# Flash layout of the ESP32-C3 with the min_spiffs.csv partition table (platformio.ini).
cat > "$out/manifest.json" <<JSON
{
  "name": "Airbox",
  "version": "$version",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-C3",
      "parts": [
        { "path": "bootloader.bin", "offset": 0 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "boot_app0.bin", "offset": 57344 },
        { "path": "firmware.bin", "offset": 65536 }
      ]
    }
  ]
}
JSON
ls -l "$out"
