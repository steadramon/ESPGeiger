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

echo "renaming bin files with the environment name"
rm -f .pio/build/*/bootloader.bin
rm -f .pio/build/*/partitions.bin
rm -f .pio/build/*/firmware_merged.bin
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
