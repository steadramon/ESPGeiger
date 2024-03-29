#!/bin/bash
set -e
echo "renaming bin files with the environment name"
rm .pio/build/*/bootloader.bin
rm .pio/build/*/partitions.bin
rename -v 's/.bin/.'"$1"'.bin/' .pio/build/*/*.bin
rename -v 's:/:-:g' .pio/build/*/*.bin
mkdir toDeploy
rename 's/.pio-build-//' .*.bin
# remove binaries for *-all*, *-test* env and only zip containing *-test*
echo "zipping code and licence"
zip -r ESPGeiger_sources.zip ESPGeiger LICENSE
mv *.zip toDeploy
mv *.bin toDeploy

ls -lA toDeploy