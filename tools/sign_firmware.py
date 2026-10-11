"""Sign a firmware build so devices accept it as a web update.

  pio run
  python tools/sign_firmware.py            # signs .pio/build/es3c28p/firmware.bin

Writes bin/voicebuddy-<version>.signed.bin = firmware.bin + a 128-byte trailer:
  "VBSIG001" | uint16 little-endian signature length | DER ECDSA-P256/SHA-256 signature | zero pad
The device hashes everything before the trailer and checks the signature with the
public key in src/signing_pubkey.h before it installs anything.
"""
import os, re, struct, sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRAILER = 128
MAGIC = b"VBSIG001"

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".pio", "build", "es3c28p", "firmware.bin")
key_path = os.path.join(ROOT, "keys", "signing_key.pem")
version = re.search(r'FW_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read()).group(1)

with open(key_path, "rb") as f:
    key = serialization.load_pem_private_key(f.read(), password=None)
image = open(src, "rb").read()
sig = key.sign(image, ec.ECDSA(hashes.SHA256()))
trailer = MAGIC + struct.pack("<H", len(sig)) + sig
trailer += b"\0" * (TRAILER - len(trailer))

os.makedirs(os.path.join(ROOT, "bin"), exist_ok=True)
out = os.path.join(ROOT, "bin", f"voicebuddy-{version}.signed.bin")
with open(out, "wb") as f:
    f.write(image + trailer)

# self-check with the public key that is compiled into the firmware
key.public_key().verify(sig, image, ec.ECDSA(hashes.SHA256()))
print(f"signed {os.path.basename(src)} ({len(image)} bytes) -> {out}")
