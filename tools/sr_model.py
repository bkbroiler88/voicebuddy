"""PlatformIO post-script: flash Espressif's speech models (srmodels.bin) into the
"model" partition at 0xC10000 whenever the firmware is uploaded over USB.

The models ship with the Arduino core package; the partition offset must match
partitions_sr.csv. The signed web update only replaces the app, which is fine
because the models never change between our versions.
"""
import os

Import("env")

libs = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")
model = os.path.join(libs or "", "esp32s3", "esp_sr", "srmodels.bin")
if os.path.exists(model):
    env.Append(FLASH_EXTRA_IMAGES=[("0xC10000", model)])
    print(f"sr_model: will flash {os.path.basename(model)} ({os.path.getsize(model) // 1024} KB) at 0xC10000")
else:
    print("sr_model: srmodels.bin not found; speech models will not be flashed")
