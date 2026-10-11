"""Generates mockups/first-run-wizard.svg and mockups/settings-redesign.svg (design previews)."""
import math, os

BG, PANEL, TEXT, DIM = "#f6f4ee", "#e1ded6", "#322e28", "#968f87"
BTN, ON, OFF, GOOD, WARN, LOUD, GOLD = "#5a6e8c", "#46a06e", "#a09696", "#5abe78", "#f0be3c", "#f08264", "#ffbe28"
F = 'font-family="sans-serif"'


def t(x, y, s, size=13, color=TEXT, anchor="middle", weight=400):
    return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{weight}" fill="{color}" text-anchor="{anchor}">{s}</text>'


def btn(x, y, w, h, label, color=BTN, size=13):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="7" fill="{color}"/>' + t(x + w / 2, y + h / 2 + 5, label, size, "#fff")


def header(step, total, title):
    o = f'<rect width="320" height="240" fill="{BG}"/>'
    if step:
        for i in range(total):
            o += f'<circle cx="{160 - (total - 1) * 7 + i * 14}" cy="12" r="{4 if i == step - 1 else 3}" fill="{TEXT if i < step else PANEL}"/>'
        o += t(312, 16, f"{step}/{total}", 10, DIM, "end")
    o += t(160, 42, title, 18, TEXT, weight=500)
    return o


def progress(y, frac, color=GOOD):
    return (f'<rect x="40" y="{y}" width="240" height="14" rx="7" fill="none" stroke="{DIM}"/>'
            f'<rect x="42" y="{y + 2}" width="{max(10, 236 * frac):.0f}" height="10" rx="5" fill="{color}"/>')


def bottom(left=None, right=None, rcol=ON):
    o = ""
    if left: o += btn(10, 198, 100, 34, left, OFF)
    if right: o += btn(210, 198, 100, 34, right, rcol)
    return o


def phrase(y, s):
    return f'<rect x="24" y="{y}" width="272" height="34" rx="10" fill="#fff" stroke="{DIM}"/>' + t(160, y + 22, s, 14, TEXT)


def room(zone_hi=None, kid=None, dot_zone=None):
    """Top-down room: device at bottom centre, three zone arcs."""
    o = ""
    cx, cy = 160, 186
    for r, name in ((110, "Far"), (75, "Mid"), (40, "Near")):
        fill = "#d7efdd" if zone_hi == name else "#ece9e1"
        o += f'<path d="M{cx - r} {cy} A{r} {r} 0 0 1 {cx + r} {cy} Z" fill="{fill}" stroke="#d3cfc4"/>'
    for r, name in ((110, "Far"), (75, "Mid"), (40, "Near")):
        o += t(cx, cy - r + 14, name, 10, DIM)
    o += f'<rect x="{cx - 9}" y="{cy - 6}" width="18" height="12" rx="2" fill="{TEXT}"/>'
    if kid:
        kx, ky = kid
        o += f'<circle cx="{kx}" cy="{ky - 9}" r="5" fill="{BTN}"/><rect x="{kx - 5}" y="{ky - 4}" width="10" height="11" rx="4" fill="{BTN}"/>'
    return o


def screens():
    S = []
    # 1 welcome
    o = header(0, 7, "Hi! Let's set up Voice Buddy")
    o += t(160, 72, "About 3 minutes. You'll need:", 13)
    for i, s in enumerate(["Your WiFi password", "A quiet moment in the room", "Your child, and a grown-up voice"]):
        o += f'<circle cx="62" cy="{96 + i * 22}" r="4" fill="{GOOD}"/>' + t(74, 101 + i * 22, s, 13, TEXT, "start")
    o += t(160, 176, "Put Buddy where it will stay.", 12, DIM)
    o += bottom("Later", "Start")
    S.append(("1. Welcome", o))

    # 2 wifi list
    o = header(1, 7, "Connect to WiFi")
    o += t(160, 62, "Buddy uses WiFi to sense where your child is.", 11, DIM)
    for i, (n, bars, sel) in enumerate([("HomeNet-2G", 4, True), ("HomeNet-5G", 3, False), ("Neighbor_Guest", 1, False)]):
        y = 72 + i * 36
        o += f'<rect x="20" y="{y}" width="280" height="30" rx="7" fill="{"#dfe7f2" if sel else "#fff"}" stroke="{BTN if sel else PANEL}"/>'
        o += t(34, y + 20, n, 13, TEXT, "start")
        for b in range(4):
            o += f'<rect x="{268 + b * 6}" y="{y + 22 - (b + 1) * 4}" width="4" height="{(b + 1) * 4}" fill="{TEXT if b < bars else PANEL}"/>'
    o += t(160, 190, "5 GHz networks can't be used (2.4 GHz only)", 10, DIM)
    o += bottom("Back", "Next")
    S.append(("2. Pick the home WiFi", o))

    # 3 password keyboard
    o = header(1, 7, "HomeNet-2G password")
    o += f'<rect x="20" y="54" width="280" height="26" rx="6" fill="#fff" stroke="{DIM}"/>' + t(30, 72, "••••••••k", 14, TEXT, "start")
    o += btn(250, 57, 46, 20, "show", PANEL, 10).replace('fill="#fff"', f'fill="{TEXT}"')
    rows = ["1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"]
    for r, keys in enumerate(rows):
        w = 28
        x0 = 160 - len(keys) * (w + 2) / 2
        for i, k in enumerate(keys):
            o += f'<rect x="{x0 + i * (w + 2):.0f}" y="{86 + r * 26}" width="{w}" height="23" rx="4" fill="#fff" stroke="{PANEL}"/>' + t(x0 + i * (w + 2) + w / 2, 102 + r * 26, k, 12)
    o += btn(14, 190, 46, 24, "⇧", PANEL, 12).replace('fill="#fff"', f'fill="{TEXT}"')
    o += btn(64, 190, 46, 24, "123", PANEL, 11).replace('fill="#fff"', f'fill="{TEXT}"')
    o += f'<rect x="114" y="190" width="110" height="24" rx="4" fill="#fff" stroke="{PANEL}"/>'
    o += btn(228, 190, 40, 24, "⌫", PANEL, 12).replace('fill="#fff"', f'fill="{TEXT}"')
    o += btn(272, 190, 40, 24, "OK", ON, 12)
    S.append(("3. Password (saved only on the device)", o))

    # 4 quiet room
    o = header(2, 7, "Shh... learning the room")
    o += t(160, 66, "Everyone stay quiet. Leave the TV, fans", 12)
    o += t(160, 82, "and lights the way they normally are.", 12)
    o += f'<circle cx="160" cy="128" r="30" fill="none" stroke="{PANEL}" stroke-width="8"/>'
    a = 2 * math.pi * 0.6
    o += f'<path d="M160 98 A30 30 0 1 1 {160 + 30 * math.sin(a):.1f} {128 - 30 * math.cos(a):.1f}" fill="none" stroke="{GOOD}" stroke-width="8" stroke-linecap="round"/>'
    o += t(160, 134, "9 s", 16, TEXT, weight=500)
    o += t(160, 180, "Room sound: 34 dB    WiFi: 148 pkt/s", 11, DIM)
    o += bottom("Cancel")
    S.append(("4. Room baseline (15 s, empty and quiet)", o))

    # 5 zone training
    o = header(3, 7, "Play near Buddy")
    o += t(160, 62, "Your child plays in the green area for 10 s.", 11, DIM)
    o += t(160, 76, "Moving around is good!", 11, DIM)
    o += room("Near", kid=(140, 166))
    o += progress(192, 0.45)
    o += t(160, 222, "Then: middle of the room, then far side", 10, DIM)
    S.append(("5. Teach the zones: Near, Mid, Far", o))

    # 6 inside voice
    o = header(4, 7, "Inside voice")
    o += t(160, 64, "From the Near spot, have your child say:", 12, DIM)
    o += phrase(74, "“Hi Buddy, I'm using my inside voice!”")
    o += t(160, 130, "hearing voice   290 Hz", 12, GOOD)
    for i in range(24):
        h = 6 + 16 * abs(math.sin(i * 0.9)) * (0.6 + 0.4 * math.sin(i * 0.3))
        o += f'<rect x="{88 + i * 6}" y="{160 - h:.0f}" width="4" height="{h:.0f}" rx="2" fill="{GOOD}"/>'
    o += progress(170, 0.7)
    o += bottom("Redo")
    S.append(("6. Inside voice (phrase shown on screen)", o))

    # 7 excited voice
    o = header(5, 7, "Excited voice")
    o += t(160, 64, "Same spot. Now say it like cheering!", 12, DIM)
    o += phrase(74, "“Hi Buddy, I'm SO excited!”")
    o += t(160, 130, "hearing voice   410 Hz", 12, WARN)
    for i in range(24):
        h = 10 + 24 * abs(math.sin(i * 0.8))
        o += f'<rect x="{88 + i * 6}" y="{160 - h:.0f}" width="4" height="{h:.0f}" rx="2" fill="{WARN}"/>'
    o += progress(170, 0.35, WARN)
    o += bottom("Skip")
    S.append(("7. Excited voice (sets the too-loud line)", o))

    # 8 far voice
    o = header(5, 7, "Inside voice, far away")
    o += t(160, 62, "From the far side, the same inside voice.", 11, DIM)
    o += t(160, 76, "Buddy learns how sound travels in this room.", 11, DIM)
    o += room("Far", kid=(222, 104))
    o += progress(192, 0.55)
    o += t(160, 222, "Detected zone: FAR (91% sure)", 10, GOOD)
    S.append(("8. Same voice from the far side", o))

    # 9 grown-up voice
    o = header(6, 7, "Now a grown-up")
    o += t(160, 64, "A parent says the phrase, so Buddy can", 12, DIM)
    o += t(160, 80, "tell your voices apart.", 12, DIM)
    o += phrase(90, "“Hi Buddy, I'm a grown-up!”")
    o += t(160, 146, "hearing voice   130 Hz", 12, BTN)
    o += progress(160, 0.8, BTN)
    o += t(160, 190, "Add another grown-up after this?", 11, DIM)
    o += bottom("Skip")
    S.append(("9. Grown-up voice (so it can ignore you)", o))

    # 10 summary
    o = header(7, 7, "All set!")
    o += t(20, 66, "Too-loud line", 12, TEXT, "start") + t(300, 66, "72 dB near, 66 dB far", 12, TEXT, "end", 500)
    o += t(20, 86, "Room background", 12, TEXT, "start") + t(300, 86, "34 dB", 12, TEXT, "end", 500)
    o += t(20, 106, "Voice pitch", 12, TEXT, "start")
    # pitch ranges bar: 80..600 Hz
    def hx(f): return 40 + (math.log(f) - math.log(80)) / (math.log(700) - math.log(80)) * 240
    o += f'<rect x="40" y="114" width="240" height="10" rx="5" fill="{PANEL}"/>'
    o += f'<rect x="{hx(105):.0f}" y="114" width="{hx(175) - hx(105):.0f}" height="10" rx="5" fill="{BTN}"/>'
    o += f'<rect x="{hx(240):.0f}" y="114" width="{hx(520) - hx(240):.0f}" height="10" rx="5" fill="{GOOD}"/>'
    o += t(hx(140), 138, "grown-up", 10, BTN) + t(hx(350), 138, "your child", 10, GOOD)
    o += t(160, 160, "Voices are easy to tell apart ✔", 12, GOOD)
    o += t(160, 178, "Zones: Near / Mid / Far learned", 11, DIM)
    o += bottom("Redo", "Save")
    S.append(("10. Summary: limits, voices, zones", o))

    # 11 live check
    o = header(0, 7, "Check the zones")
    o += t(160, 62, "Walk around: the green area should follow.", 11, DIM)
    o += room("Mid", kid=(118, 128))
    o += t(160, 210, "MID   84% sure   146 pkt/s", 12, TEXT, weight=500)
    o += bottom(None, "Done")
    S.append(("11. Live zone check (Settings or end of setup)", o))
    return S


def settings():
    S = []

    def tabs(active):
        names = ["Voice", "Room", "Stars", "Screen", "Setup"]
        o = ""
        for i, n in enumerate(names):
            on = i == active
            o += f'<rect x="{i * 64}" y="0" width="64" height="28" fill="{BTN if on else PANEL}"/>' + t(i * 64 + 32, 19, n, 12, "#fff" if on else TEXT)
        return o

    def row(i, label, sub, kind, val=None):
        y = 34 + i * 36
        o = t(8, y + 15, label, 13, TEXT, "start") + t(8, y + 29, sub, 10, DIM, "start")
        if kind == "pm":
            o += t(192, y + 22, val, 13, TEXT, "end", 500) + btn(200, y + 6, 50, 24, "−") + btn(262, y + 6, 50, 24, "+")
        elif kind == "tog":
            o += btn(232, y + 6, 80, 24, "ON" if val else "OFF", ON if val else OFF)
        elif kind == "act":
            o += btn(212, y + 6, 100, 24, val)
        o += f'<line x1="6" y1="{y + 34}" x2="314" y2="{y + 34}" stroke="{PANEL}"/>'
        return o

    def foot(msg):
        return t(8, 228, msg, 11, DIM, "start") + btn(240, 212, 76, 24, "Done", ON, 12)

    o = f'<rect width="320" height="240" fill="{BG}"/>' + tabs(0)
    o += row(0, "Too-loud line", "Where Buddy covers his ears", "pm", "72 dB")
    o += row(1, "Early warning", "Yellow starts this much sooner", "pm", "6 dB")
    o += row(2, "Listen only for his/her voice", "Ignores grown-ups' voices", "tog", True)
    o += row(3, "Ignore bangs and noise", "Only voices move the meter", "tog", True)
    o += row(4, "Talk bubbles", "Buddy says short phrases", "tog", True)
    o += foot("Now: 58 dB  (line 72)")
    S.append(("Voice", o))

    o = f'<rect width="320" height="240" fill="{BG}"/>' + tabs(1)
    o += row(0, "Adjust for distance", "Lower the line when he/she is far", "tog", True)
    o += row(1, "Where is he/she?", "Live: MID, 84% sure", "act", "Check")
    o += row(2, "Far-away adjustment", "Measured in setup", "pm", "-6 dB")
    o += row(3, "Louder room, higher line", "Allow a bit more over the TV", "tog", True)
    o += row(4, "WiFi", "HomeNet-2G, 148 pkt/s", "act", "Change")
    o += foot("Room background: 34 dB")
    S.append(("Room", o))

    o = f'<rect width="320" height="240" fill="{BG}"/>' + tabs(4)
    o += row(0, "First-time setup", "Room, zones and voices (3 min)", "act", "Start")
    o += row(1, "Redo his/her voice only", "If he/she has grown or it's off", "act", "Start")
    o += row(2, "Redo room zones only", "After moving furniture or Buddy", "act", "Start")
    o += row(3, "Match a phone dB meter", "Optional: real decibel numbers", "pm", "110")
    o += row(4, "Parent PIN", "Hold top-right corner 3 s to open", "act", "Change")
    o += foot("Settings save when you tap Done")
    S.append(("Setup", o))
    return S


def grid(items, cols, scale, title_h=18):
    W, H, gap = 320 * scale, 240 * scale, 16
    rows = math.ceil(len(items) / cols)
    total_w = cols * W + (cols - 1) * gap
    total_h = rows * (H + title_h + 14)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {total_w:.0f} {total_h:.0f}" width="{total_w:.0f}" height="{total_h:.0f}">',
           f'<rect width="100%" height="100%" fill="#ffffff"/>']
    for i, (label, body) in enumerate(items):
        c, r = i % cols, i // cols
        x, y = c * (W + gap), r * (H + title_h + 14)
        out.append(t(x, y + 13, label, 13, "#444", "start", 500))
        out.append(f'<g transform="translate({x:.1f} {y + title_h:.1f}) scale({scale})"><clipPath id="k{i}"><rect width="320" height="240" rx="8"/></clipPath>'
                   f'<g clip-path="url(#k{i})">{body}</g><rect width="320" height="240" rx="8" fill="none" stroke="#1b1b1b" stroke-width="5"/></g>')
    out.append("</svg>")
    return "".join(out)


here = os.path.dirname(os.path.abspath(__file__))
outdir = os.path.join(here, "..", "mockups")
os.makedirs(outdir, exist_ok=True)
open(os.path.join(outdir, "first-run-wizard.svg"), "w", encoding="utf-8").write(grid(screens(), 3, 0.9))
open(os.path.join(outdir, "settings-redesign.svg"), "w", encoding="utf-8").write(grid(settings(), 3, 0.9))
print("ok")
