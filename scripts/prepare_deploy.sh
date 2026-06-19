#!/bin/bash
set -e

VERSION="$1"
# Strip a leading v so the webinstall zip filename matches the version that
# the webinstaller's sync script expects (URL uses /v0.12.0/ but the asset
# name is ESPGeiger_webinstall.0.12.0.zip).
VERSION_CLEAN="${VERSION#v}"

# ESP32 envs ship the bootloader, partition table, boot_app0 and app
# separately so the web installer can target only those offsets, leaving
# the NVS partition (saved WiFi creds) at 0x9000 untouched on upgrades.
# Per-env offsets.json is the source of truth; webinstaller assembles its
# manifest.json from it.
echo "building ESP32 webinstall payload"
python3 scripts/build_webinstall_payload.py "$VERSION_CLEAN"

echo "extracting advanced test firmware bins to bundled zip"
# Plain *_test envs (esp32_test, esp8266_test etc.) stay as standalone release
# bins. The dev-oriented variants (testpulse / testpulseint / testserial) get
# bundled into one zip to keep the release page tidy.
mkdir -p test_advanced
for env_dir in .pio/build/*_testpulse .pio/build/*_testpulseint .pio/build/*_testserial; do
  [ -d "$env_dir" ] || continue
  env_name="$(basename "$env_dir")"
  for src in "$env_dir/firmware.bin" "$env_dir/firmware_merged.bin"; do
    [ -f "$src" ] || continue
    cp "$src" "test_advanced/${env_name}-$(basename "$src" .bin).${VERSION}.bin"
    rm -f "$src"
  done
done
if [ -n "$(ls -A test_advanced 2>/dev/null)" ]; then
  zip -j "ESPGeiger_test_advanced.${VERSION}.zip" test_advanced/*.bin
fi
rm -rf test_advanced

echo "renaming bin files with the environment name"
rm -f .pio/build/*/bootloader.bin
rm -f .pio/build/*/partitions.bin
rename -v 's/.bin/.'"$VERSION"'.bin/' .pio/build/*/*.bin
rename -v 's/.elf/.'"$VERSION"'.elf/' .pio/build/*/*.elf
rename -v 's:/:-:g' .pio/build/*/*.bin .pio/build/*/*.elf
mkdir toDeploy
rename 's/.pio-build-//' .*.bin .*.elf
echo "zipping code and licence"
zip -r ESPGeiger_sources.zip ESPGeiger LICENSE
echo "zipping elf files for stack decoding"
zip -j ESPGeiger_elfs."$VERSION".zip *.elf
rm -f *.elf
mv *.zip toDeploy
mv *.bin toDeploy

ls -lA toDeploy
