#!/bin/bash
set -e
echo "renaming bin files with the environment name"
# These intermediate ESP32 outputs are not uploaded as build artifacts in the
# new release flow, so use -f to be safe in either flow.
rm -f .pio/build/*/bootloader.bin
rm -f .pio/build/*/partitions.bin
rename -v 's/.bin/.'"$1"'.bin/' .pio/build/*/*.bin
rename -v 's/.elf/.'"$1"'.elf/' .pio/build/*/*.elf
rename -v 's:/:-:g' .pio/build/*/*.bin .pio/build/*/*.elf
mkdir toDeploy
rename 's/.pio-build-//' .*.bin .*.elf
# remove binaries for *-all*, *-test* env and only zip containing *-test*
echo "zipping code and licence"
zip -r ESPGeiger_sources.zip ESPGeiger LICENSE
echo "zipping elf files for stack decoding"
zip -j ESPGeiger_elfs."$1".zip *.elf
rm -f *.elf
mv *.zip toDeploy
mv *.bin toDeploy

ls -lA toDeploy