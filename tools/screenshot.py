"""Capture real screens from the device over USB serial.

  python tools/screenshot.py COM36            # all README shots into docs/screens/
  python tools/screenshot.py COM36 03 pup.png # one shot: view 0, mood 3
  python tools/screenshot.py COM36 - now.png  # whatever is on screen right now

The firmware's serial commands: S<view><mood> forces a view + mood, P dumps the panel
as raw RGB565, X returns to normal. Views: 0 pup, 1 dial, 2 Saturn, 3 solar system.
Moods: 0 calm, 1 happy, 2 alert, 3 too loud, 4 sleepy, 5 star, 6 celebrate.
"""
import os, struct, sys, time, zlib
import serial

SHOTS = [("01", "pup-happy.png"), ("03", "pup-too-loud.png"), ("12", "dial-alert.png"),
         ("25", "saturn-star.png"), ("21", "saturn-happy.png"), ("33", "solar-too-loud.png")]


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def grab(s, code, path):
    s.reset_input_buffer()
    if code != "-":                       # "-" = capture whatever is on screen
        s.write(b"S" + code.encode())
        time.sleep(2.0)                   # let colours fade and Buddy settle
    s.reset_input_buffer()
    s.write(b"P")
    buf = b""
    t0 = time.time()
    while b"SHOT " not in buf and time.time() - t0 < 5:
        buf += s.read(256)
    i = buf.index(b"SHOT ")
    hdr_end = buf.index(b"\n", i)
    w, h = map(int, buf[i + 5:hdr_end].split())
    data = buf[hdr_end + 1:]
    need = w * h * 2
    while len(data) < need and time.time() - t0 < 20:
        data += s.read(need - len(data))
    data = data[:need]
    rgb = bytearray()
    for k in range(0, need, 2):
        v = (data[k] << 8) | data[k + 1]  # TFT_eSPI readRect: big-endian 565
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    png(path, w, h, bytes(rgb))
    print("saved", path)


def main():
    port = sys.argv[1]
    s = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.5)
    if len(sys.argv) >= 4:
        grab(s, sys.argv[2], sys.argv[3])
        if sys.argv[2] == "-":
            return
    else:
        out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "screens")
        os.makedirs(out, exist_ok=True)
        for code, name in SHOTS:
            grab(s, code, os.path.join(out, name))
    s.write(b"X")


if __name__ == "__main__":
    main()
