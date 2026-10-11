"""Merge the PlatformIO build into one image that flashes at 0x0.

Run `pio run` first, then:  python tools/make_bin.py
Writes bin/voicebuddy-es3c28p.bin (bootloader + partitions + boot_app0 + app).
"""
import glob, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "es3c28p")
PIO = os.path.join(os.path.expanduser("~"), ".platformio", "packages")


def find(pattern):
    hits = sorted(glob.glob(os.path.join(PIO, pattern)))
    if not hits:
        sys.exit(f"not found: {pattern}")
    return hits[-1]


esptool = find("tool-esptoolpy*/esptool.py")
boot_app0 = find("framework-arduinoespressif32/tools/partitions/boot_app0.bin")
out_dir = os.path.join(ROOT, "bin")
os.makedirs(out_dir, exist_ok=True)
out = os.path.join(out_dir, "voicebuddy-es3c28p.bin")

cmd = [sys.executable, esptool, "--chip", "esp32s3", "merge_bin", "-o", out,
       "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
       "0x0", os.path.join(BUILD, "bootloader.bin"),
       "0x8000", os.path.join(BUILD, "partitions.bin"),
       "0xe000", boot_app0,
       "0x10000", os.path.join(BUILD, "firmware.bin")]
subprocess.run(cmd, check=True)
print(f"wrote {out} ({os.path.getsize(out)} bytes), flash at 0x0")
