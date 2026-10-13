"""Start the speech test over USB and print what Buddy logs.
python tools/srtest_watch.py COM36 [seconds] [--all]   (--all also prints the per-second [lvl] lines)"""
import sys, time, serial

port = sys.argv[1]
secs = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 60
show_all = "--all" in sys.argv
s = serial.Serial(port, 115200, timeout=0.3)
time.sleep(0.5)
last = ""
t = time.time()
while time.time() - t < 4:
    l = s.readline().decode(errors="replace").rstrip()
    if l.startswith("[lvl]"): last = l
print("before:", last[last.find("heap"):][:60])
s.write(b"V")
t = time.time()
nlvl = 0
while time.time() - t < secs:
    l = s.readline().decode(errors="replace").rstrip()
    if not l: continue
    if l.startswith("[lvl]"):
        last = l; nlvl += 1
        if show_all: print(time.strftime("%H:%M:%S"), "[lvl]", l[6:l.find("line")].strip(), "|", l[l.find("heap"):][:30], flush=True)
    else: print(time.strftime("%H:%M:%S"), l[:180], flush=True)
print(f"after: {last[last.find('heap'):][:60]}   ({nlvl} [lvl] lines in {secs} s)")
