"""Merge the PlatformIO build into one image that flashes at 0x0.

Run `pio run` first, then:
  python tools/make_bin.py          # bin/voicebuddy-es3c28p.bin: bootloader + partitions + app (~1.5 MB)
  python tools/make_bin.py --full   # bin/voicebuddy-es3c28p-full.bin: the same plus the speech models
                                    # (padded to ~15 MB because the models live at 0xC10000)
The speech models (srmodels.bin from the Arduino core) are needed for the reading games;
`pio run -t upload` flashes them automatically, see tools/sr_model.py.
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


# esptool: prefer the one PlatformIO installed into its own Python (esptool 5.x);
# fall back to the old package directory.
PENV_PY = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "python.exe")
if os.path.exists(PENV_PY) and subprocess.run([PENV_PY, "-c", "import esptool"], capture_output=True).returncode == 0:
    ESPTOOL = [PENV_PY, "-m", "esptool"]
else:
    ESPTOOL = [sys.executable, find("tool-esptoolpy*/esptool.py")]
boot_app0 = find("framework-arduinoespressif32*/tools/partitions/boot_app0.bin")
srmodels = find("framework-arduinoespressif32-libs*/esp32s3/esp_sr/srmodels.bin")   # speech models -> "model" partition
out_dir = os.path.join(ROOT, "bin")
os.makedirs(out_dir, exist_ok=True)
full = "--full" in sys.argv
out = os.path.join(out_dir, "voicebuddy-es3c28p-full.bin" if full else "voicebuddy-es3c28p.bin")

cmd = ESPTOOL + ["--chip", "esp32s3", "merge_bin", "-o", out,
       "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
       "0x0", os.path.join(BUILD, "bootloader.bin"),
       "0x8000", os.path.join(BUILD, "partitions.bin"),
       "0xe000", boot_app0,
       "0x10000", os.path.join(BUILD, "firmware.bin")]
if full:
    cmd += ["0xC10000", srmodels]
subprocess.run(cmd, check=True)
print(f"wrote {out} ({os.path.getsize(out)} bytes), flash at 0x0")
