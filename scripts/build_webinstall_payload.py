#!/usr/bin/env python3
"""
Build the ESPGeiger web installer payload.

For each ESP32-family build (anything with a bootloader.bin in .pio/build/),
emit a folder containing:
  - bootloader.bin
  - partitions.bin
  - boot_app0.bin
  - firmware.bin
  - offsets.json       (chipFamily + part-offset map, source of truth)

Then pack everything into ESPGeiger_webinstall.<version>.zip.

The webinstaller repo reads each offsets.json to assemble its manifest.json,
so adding a new chip family is a one-line lookup change here only.
"""
from __future__ import annotations
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD_DIR = REPO / ".pio" / "build"

# arduino-esp32 include subdir -> esp-web-tools chipFamily label
INCLUDE_TO_CHIP = {
    "esp32s3": "ESP32-S3",
    "esp32s2": "ESP32-S2",
    "esp32c3": "ESP32-C3",
    "esp32c2": "ESP32-C2",
    "esp32c6": "ESP32-C6",
    "esp32h2": "ESP32-H2",
    "esp32":   "ESP32",
}
ESP8266_CHIP = "ESP8266"


def detect_chip_family(metadata: dict) -> str | None:
    """Find which esp32X include path is on the build's include list."""
    includes = metadata.get("includes", {}).get("build", [])
    hit = None
    for inc in includes:
        for key in INCLUDE_TO_CHIP:
            if f"/{key}/" in inc:
                if hit is None or len(key) > len(hit):
                    hit = key
    return INCLUDE_TO_CHIP[hit] if hit else None


def pio_metadata(env: str) -> dict:
    out = subprocess.check_output(
        ["pio", "project", "metadata", "-e", env, "--json-output"],
        cwd=str(REPO),
    )
    return json.loads(out).get(env, {})


def env_payload(env: str, env_build: Path, dest_root: Path) -> bool:
    """Materialise one env's payload folder. Return True on success."""
    meta = pio_metadata(env)
    flash_images = meta.get("extra", {}).get("flash_images", [])
    app_offset = meta.get("extra", {}).get("application_offset")
    chip = detect_chip_family(meta)
    if not flash_images or app_offset is None or chip is None:
        print(f"  skip {env}: missing PIO metadata")
        return False

    dest = dest_root / env
    dest.mkdir(parents=True, exist_ok=True)

    parts = []
    for img in flash_images:
        src = Path(img["path"])
        if not src.exists():
            print(f"  skip {env}: image missing {src.name}")
            shutil.rmtree(dest)
            return False
        shutil.copy2(src, dest / src.name)
        parts.append({"file": src.name, "offset": int(img["offset"], 16)})

    fw = env_build / "firmware.bin"
    shutil.copy2(fw, dest / "firmware.bin")
    parts.append({"file": "firmware.bin", "offset": int(app_offset, 16)})

    with open(dest / "offsets.json", "w") as f:
        json.dump({"chipFamily": chip, "parts": parts}, f, indent=2)
    print(f"  {env}: {chip}, {len(parts)} parts")
    return True


def esp8266_payload(env: str, env_build: Path, dest_root: Path) -> bool:
    fw = env_build / "firmware.bin"
    if not fw.exists():
        return False
    dest = dest_root / env
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(fw, dest / "firmware.bin")
    with open(dest / "offsets.json", "w") as f:
        json.dump({"chipFamily": ESP8266_CHIP,
                   "parts": [{"file": "firmware.bin", "offset": 0}]}, f, indent=2)
    print(f"  {env}: {ESP8266_CHIP}, 1 part")
    return True


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "dev"
    payload_dir = REPO / "webinstall_payload"
    if payload_dir.exists():
        shutil.rmtree(payload_dir)
    payload_dir.mkdir()

    count = 0
    for env_build in sorted(BUILD_DIR.glob("*/")):
        if (env_build / "bootloader.bin").exists():
            if env_payload(env_build.name, env_build, payload_dir):
                count += 1
        elif (env_build / "firmware.bin").exists():
            # ESP8266 builds: no bootloader split needed, single firmware.bin.
            if esp8266_payload(env_build.name, env_build, payload_dir):
                count += 1

    if count == 0:
        print("no ESP32 envs to package")
        return

    zip_path = REPO / f"ESPGeiger_webinstall.{version}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _, files in os.walk(payload_dir):
            for name in files:
                p = Path(root) / name
                zf.write(p, p.relative_to(payload_dir))
    shutil.rmtree(payload_dir)
    print(f"wrote {zip_path.relative_to(REPO)} ({count} envs)")


if __name__ == "__main__":
    main()
