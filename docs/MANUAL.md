# Voice Buddy — Parent Manual

Firmware 1.4.2 · October 2026

## What Voice Buddy is

Voice Buddy is a small touchscreen that shows your child, with a friendly character instead of words or numbers, when his/her voice is getting too loud. Calm talking earns gold stars toward a daily goal. Being loud only pauses that progress, and nothing beeps, flashes or scolds.

It listens only for voices, so claps, dropped toys, fans and the dishwasher don't count. A one-time setup teaches it your child's voice and your room. A web page on your home WiFi lets you check in, cheer from your phone and change settings, but Voice Buddy works fully without WiFi.

There are four main screens. Your child swipes left or right to switch between them:

- **Police pup:** a puppy in a police uniform with a voice meter beside him. He covers his ears when it's too loud.
- **Dial:** a big gauge with a needle and a small face.
- **Saturn:** a smiling planet whose ring changes color with his/her voice.
- **Solar system:** the planets light up outward from the Sun as the voice gets louder.

![Police pup, nice talking](screens/pup-happy.png)

![Police pup, too loud](screens/pup-too-loud.png)

![Dial, getting loud](screens/dial-alert.png)

![Saturn, star earned](screens/saturn-star.png)

![Solar system, too loud](screens/solar-too-loud.png)

## Getting started

Plug the board into any USB-C phone charger. The first time, it starts the first-time setup (about 3 minutes, see below); after that it opens straight to the main screen. There is no power button: unplugging turns it off, and settings and stars are kept.

**The device:** a black Freenove ESP32-S3 2.8" display board (model FNK0104B, also sold as LCDWIKI ES3C28P) with a touchscreen, a built-in microphone, a small speaker and a color light on the back.

**Where to put it:**

- Where it will stay, at his/her eye level, where he/she usually plays or talks.
- About 1 m from his/her usual spot. That spot becomes "Near" during setup.
- Away from fans, aquarium pumps and the TV speaker if you can. Voice Buddy ignores steady noise, but a quieter spot gives cleaner readings.
- Screen facing him/her. The light on the back glows the mood color, which can be seen from across the room.

## What your child sees

The stars count sits along the top of every screen; the character changes with his/her voice.

| His/her voice | The character | Background and light |
| --- | --- | --- |
| Quiet | Calm, blinking now and then | Pale mint |
| Quiet for a while (default 2 min) | Napping, with floating z's | Lavender |
| Talking nicely | Smiling | Mint green |
| Getting loud | Eyes wide, small "o" mouth | Soft yellow |
| Too loud | Covers his ears (pup), wobbles (planets, dial) | Soft peach |
| Earned a star | Bounces, with a star or a shooting star | Light gold |
| Reached the daily goal | Confetti for 6 seconds | Light gold |

It is built not to overreact. A sound must stay over the line for a quarter second before it counts, so a shout of surprise doesn't trigger it, and it takes 1.5 seconds of calmer voice before the character relaxes. Colors fade slowly and nothing flashes.

**Speech bubbles.** The character says short phrases picked at random: "That's too loud!" or "Inside voice, please" while it's too loud, "Thank you!" when it calms down, "Nice and quiet" or "Good job!" every 15 seconds of nice talking, and "Super star!" when a star is earned. Messages you send from the web page appear the same way, with a small "from a parent" tag.

**Stars** show as "earned / daily goal" with a thin bar that fills only while his/her voice is actually heard and under the line (default 45 seconds of talking per star). Silence, noise and loud moments don't fill it.

## First-time setup

Setup takes about 3 minutes and runs by itself the first time. You can rerun all of it, or just the voice or room part, from Settings › Setup. Every recording step works the same way: the screen explains what to do, nothing records until you tap **Start**, and afterwards it shows **Got it!** with **Redo** and **Next**.

1. **WiFi (optional).** Pick your 2.4 GHz network and type the password on the on-screen keyboard. Tap **Skip WiFi** to set up without it; distance zones and web control then stay off, and everything else works.
2. **Quiet room, 15 s.** Everyone stays quiet with the room as it normally is (fridge, fan, lights). Buddy learns the background sound and, with WiFi, what the empty room looks like.
3. **Room zones, 10 s each (WiFi only).** Your child plays in the green area near Buddy, then mid-room, then the far side. Moving around is good.
4. **Inside voice, 10 s of talking.** From his/her usual spot your child says the phrase on screen, *"Hi Buddy, I'm using my inside voice!"*, repeating as needed. Only time with a voice counts, so pauses are fine.
5. **Excited voice, 6 s.** Same spot, like cheering for a favorite character. This sets where the too-loud line goes. Skip it if he/she isn't up for it.
6. **Inside voice from far away, 6 s.** From the far side of the room. This measures how much quieter a voice arrives from there in your room.
7. **A grown-up, 6 s.** A parent says the phrase, so Buddy can tell your voices apart. **Add another** records a second grown-up.
8. **Summary.** The too-loud line (near and far), the room's background level, and a bar showing your child's and the grown-ups' pitch ranges. Tap **Save**, or **Redo** to start again.
9. **Live zone check (WiFi only).** Walk between the zones and watch the green area follow.

![Inside voice step](screens/voice.png)

![Live zone check](screens/check.png)

**Tips for good recordings:** keep the room quiet apart from the voice being recorded, let each bar fill all the way, and use your child's real everyday voice rather than an extra-careful one. The line is set between the loud end of his/her inside voice and the excited voice, never below his/her normal loud moments. If the summary says the voices overlap, "Listen only for his/her voice" may also let grown-ups through.

## Parent settings on the device

Press and hold the **top-right corner** of the main screen for 3 seconds (a thin bar fills while you hold), then enter the 4-digit parent PIN. The default PIN is **1234**: change it under Screen › Parent PIN. Three wrong PINs, or 30 seconds without input, return to the main screen. Tap **Done** to save and go back.

![Voice tab](screens/settings-voice.png)

| Tab | Setting | What it does | Default |
| --- | --- | --- | --- |
| Voice | Too-loud line | Where Buddy covers his ears. Setup sets it for you | 70 dB |
| Voice | Early warning | How much sooner the yellow "getting loud" stage starts | 6 dB |
| Voice | Listen only for his/her voice | Ignores voices outside his/her pitch range (needs voice setup) | On after setup |
| Voice | Ignore bangs and noise | Only voices move the meter and the stars | On |
| Voice | Talk bubbles | Short phrases in speech bubbles | On |
| Room | Adjust for distance | Lowers the line when he/she is far away (needs zones) | On after setup |
| Room | Where is he/she? | Opens the live zone check |  |
| Room | Far-away adjustment | How much lower the line is far away. Measured during setup | −6 dB typical |
| Room | Louder room, higher line | Allows a little more when the room is loud, e.g. the TV is on | On |
| Room | WiFi & web control | Network, the web page switch, address, QR code, signed-in browsers |  |
| Stars | Star every | Seconds of his/her voice (under the line) per star | 45 s |
| Stars | Daily goal | Stars needed for the confetti celebration | 10 |
| Stars | Loud empties the star bar | On: a loud moment empties the bar. Off: it only pauses | Off |
| Stars | Chime | A soft two-note sound for each star | Off |
| Stars | Today's stars | Reset puts the count back to 0 |  |
| Screen | Brightness | Screen backlight | 80% |
| Screen | Back light glow | The light on the back matches the mood | On |
| Screen | Nap after quiet | Quiet time before the character naps | 2 min |
| Screen | Start on | Which of the four screens shows after power-up | Pup |
| Screen | Parent PIN | Change the 4-digit PIN | 1234 |
| Setup | First-time setup | Runs the whole setup again |  |
| Setup | Redo his/her voice only | Voice steps only, e.g. after a birthday or a cold |  |
| Setup | Redo room zones only | After moving furniture or Buddy |  |
| Setup | Calibrate dB readings | Optional: makes Buddy's numbers match a phone sound-meter app. Do it before the voice setup, or redo his/her voice afterwards | 110 |
| Setup | Grown-up voices | Add another grown-up's voice |  |
| Setup | Factory reset | Erases everything and starts over |  |

![Setup tab](screens/settings-setup.png)

## Distance zones over WiFi

A voice arrives quieter from across the room, so without help a shout from far away can read like normal talking up close. With zones on, Buddy works out roughly where your child is (Near, Mid or Far) and lowers the too-loud line when he/she is farther away.

**How it works.** Buddy joins your WiFi and pings the router about 125 times a second. A person changes how that signal reaches Buddy, across 52 separate channels. During setup Buddy records the pattern for an empty room and for each zone, then matches what it sees against them, with a confidence score. A zone must hold for 2 seconds before it's used. When Buddy isn't sure, it uses the middle adjustment rather than guessing.

**Check it:** Settings › Room › Where is he/she? › Check, then walk around and watch the green area. If it doesn't follow well, redo the zones (Settings › Setup › Redo room zones only), with more movement in each spot.

**Limits:** zones are rough areas, not exact distances. Someone sitting very still disturbs the signal less than someone moving, other people moving around can confuse it, and moving furniture, the router or Buddy means redoing the zones. Without WiFi this feature is simply off.

## Web control from a phone

An optional web page lets you check on Buddy and change things from any phone or computer on your home WiFi. It is **off by default**; with it off, nothing on Buddy listens on the network.

**Turn it on:** Settings › Room › WiFi & web control › **Web control** › ON. The first time, Buddy creates its own security certificate (about a second).

**First visit:**

1. Tap **Show QR** on the same page and scan it with your phone camera, or type `https://voicebuddy.local`.
2. The browser warns that the certificate isn't from a public authority. That's expected for a home device. Check that the last digits of the fingerprint match the ones under Buddy's QR code, then continue.
3. Enter the parent PIN. That browser stays signed in for 30 days.

![Web home](screens/web-home.jpg)

**What the page does:**

- **Home:** live voice level and line, where he/she is, today's stars, **Give a star**, reset, switch Buddy's screen, **Pause Buddy** (naps and stops counting, e.g. during a call), and **Send a bubble** with a short message.
- **History:** today's voice level in 10-minute steps with loud moments and stars marked, a list of today's loud moments, and stars per day for the last week.
- **Settings:** the same settings as on Buddy, saved straight to the device.
- **Schedule:** reset stars every morning at a set time, quiet hours with a lower bedtime line, and your time zone.
- **Device:** firmware version, certificate fingerprint, signed-in browsers (sign any of them out), firmware updates, settings backup and restore, restart.

![Web history](screens/web-history.jpg)

History and the schedule need the time, which Buddy gets from the internet through your WiFi. They start once the clock is set.

**Security:**

- HTTPS only. Plain http isn't served at all.
- Sign-in uses the parent PIN. Five wrong PINs lock sign-in for 5 minutes.
- Whenever a new browser signs in, Buddy shows a notice with a **Sign it out** button. If it wasn't you, sign it out and change the PIN.
- Settings › Room › WiFi & web control › Signed-in browsers › **Sign out** signs out every browser at once.

## Firmware updates over WiFi

New versions install from the web page (Device › Update firmware) with no cable. Buddy only accepts files **signed with the owner's private key**, so nobody else can install firmware on it, even if they learned the PIN.

![Web device page](screens/web-device.jpg)

**What happens during an update:**

1. You choose a `voicebuddy-x.y.z.signed.bin` file. Buddy writes it into its spare firmware slot, so the working version isn't touched.
2. Buddy checks the signature. An unsigned or altered file is rejected and nothing changes.
3. Buddy restarts into the new version. It must run healthily for 30 seconds (screen, microphone, no crash). If it doesn't, Buddy switches back to the previous version by itself.

**The signing key** is the file `keys/signing_key.pem` on the computer that builds the firmware. It never goes on the device or into the public GitHub repository. **Keep a backup** (for example in a password manager or on a USB stick): without it, new updates can only be installed with a USB cable.

To make an update: build with `pio run`, then run `python tools/sign_firmware.py`, which writes the signed file into the `bin` folder.

## Factory reset

Settings › Setup › **Factory reset** › **Reset** shows exactly what will be erased, then **Erase everything** wipes:

- settings, the parent PIN and stars
- WiFi and the room zones
- his/her voice and the grown-up voices
- history and signed-in browsers
- the web certificate (a new one is made next time web control is turned on)

Buddy then restarts into the first-time setup. Use it when moving to a new home or network, giving Buddy to another family, or to start the setup completely fresh. If you've forgotten the parent PIN itself, reflash the firmware over USB with a full erase (see the Technical reference).

## Tips for using it with your child

These are general suggestions. Adapt them to what works for your child and to any guidance from his/her therapists.

- **Introduce the character first.** Play together: whisper and watch it smile, talk loudly on purpose and watch it cover its ears. Let your child try both before Buddy is used in real moments.
- **Frame it around the character's feelings.** "Buddy's ears are sensitive. Can we use an inside voice so he's comfortable?" works better for many children than "you're too loud."
- **Start easy.** Shorten Star every at first so stars come quickly, and raise the too-loud line a couple of dB if he/she sees the yellow or peach screens often. Tighten slowly over days or weeks.
- **Celebrate stars together.** Name it when a star pops. From the web page you can also send a bubble or give a star when you notice good moments from another room.
- **Expect excited moments.** Being loud only pauses the star bar, and the character relaxes quickly once things calm down, so a slip never feels like a failure.
- **Keep it predictable.** Same place every day, stars reset at the same time each morning (Schedule on the web page), and quiet hours for a calmer bedtime line.

## Troubleshooting

| Problem | Likely cause | What to do |
| --- | --- | --- |
| The character never reacts to talking | Line too high, or "Listen only for his/her voice" filtering him/her out | Settings › Voice: lower the too-loud line, or turn off "Listen only" and redo his/her voice |
| It reacts too easily | Line too low | Raise the too-loud line 2–3 dB, or redo the voice setup with the excited voice step |
| Grown-ups' voices still count | Voices overlap, or no grown-up recorded | Settings › Setup › Grown-up voices › Add; check "Listen only for his/her voice" is on |
| Stars come too fast or too slow | Star timing | Settings › Stars › Star every |
| Zones don't follow him/her | Zones trained with too little movement, or the room changed | Settings › Setup › Redo room zones only; check the live zone check shows 100+ pkt/s |
| "WiFi not connected" | Out of range, router down, or a new network | Buddy retries every 20 seconds. For a new network: Settings › Room › WiFi & web control › WiFi › Change |
| Only my old network shows | Moved house | Change the WiFi as above, or do a factory reset to start fresh |
| The web page won't open | Web control off, phone on another network, or WiFi down | Turn web control on, use the same WiFi, try the IP address under the QR code |
| Browser warning every visit | Certificate not trusted on that phone | Expected for a home device. Accept it once per browser after checking the fingerprint |
| Firmware upload refused | File isn't signed with the owner's key | Use the `.signed.bin` file from the owner's build or releases |
| "Mic codec not found" at start-up | The audio chip didn't answer | Unplug for 10 seconds and plug back in |
| Forgot the parent PIN |  | Reflash over USB with a full erase (Technical reference), then run setup again |

## Technical reference

Source code and the flashable image: [github.com/bkbroiler88/voicebuddy](https://github.com/bkbroiler88/voicebuddy). Built with PlatformIO (espressif32 6.12.0, Arduino core 2.0.17).

**Flash it over USB.** The whole image flashes at address 0x0, for example with ESP Web Flasher or `esptool.py --chip esp32s3 write_flash 0x0 bin/voicebuddy-es3c28p.bin`. From source: `pio run -t erase` then `pio run -t upload`. A full erase also clears a forgotten PIN.

**Pins (Freenove FNK0104B / LCDWIKI ES3C28P)**

| Part | Pins |
| --- | --- |
| Display ILI9341 (SPI) | MOSI 11, SCLK 12, MISO 13, CS 10, DC 46, backlight 45 |
| Touch FT6336G (I2C 0x38) | SDA 16, SCL 15, RST 18 |
| Audio codec ES8311 (I2C 0x18) | Same I2C bus as touch |
| Audio data (I2S) | MCLK 4, BCLK 5, LRCK 7, mic in 6, speaker out 8, amp enable 1 |
| Color light WS2812B | 42 |

The board's own documentation lists the audio data pins the other way round. On this board the microphone is really on IO6.

**Flash layout (16 MB):** settings 20 KB · firmware slot A 6.25 MB · firmware slot B 6.25 MB (for updates) · storage 3.4 MB (certificate, signed-in browsers, history) · crash log 64 KB. The firmware is about 1.2 MB.

**How the work is split:** one processor core runs the microphone, pitch detection, WiFi sensing and the web server; the other only draws the screen, so animations stay smooth. Screen buffers live in the board's 8 MB PSRAM.

**Serial console (115200 baud, USB):** a status line every second (level, pitch, noise, line, zone, stars, memory, WiFi). Commands: `d` audio chip check, `S<view><mood>` show a screen, `W<n>` open a setup step, `T<n>` open a settings tab, `E1`/`E0` web control on/off, `P` screenshot dump, `X` back to normal. `tools/screenshot.py` saves screenshots as PNG.
