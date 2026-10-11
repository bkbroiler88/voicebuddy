# Voice Buddy

An inside-voice coach for kids, running on a $20 ESP32-S3 touchscreen. A friendly character listens through the built-in microphone and shows a child, without words or numbers, when their voice is getting too loud. Calm talking earns stars toward a daily goal. Nothing beeps or flashes.

Built for the **Freenove FNK0104B** (also sold as **LCDWIKI ES3C28P**): black ESP32-S3 2.8" ILI9341 board with capacitive touch, ES8311 audio codec + mic, and 8 MB PSRAM.

| Police pup | Too loud | Dial |
|---|---|---|
| ![Pup, nice talking](docs/screens/pup-happy.png) | ![Pup, too loud](docs/screens/pup-too-loud.png) | ![Dial, getting loud](docs/screens/dial-alert.png) |
| **Saturn** | **Star earned** | **Solar system** |
| ![Saturn, nice talking](docs/screens/saturn-happy.png) | ![Saturn, star earned](docs/screens/saturn-star.png) | ![Solar system, too loud](docs/screens/solar-too-loud.png) |

All screenshots are captured from the device itself (`tools/screenshot.py`).

## What it does

- **Four views, swipe to switch:** police pup, dial, Saturn, and a solar system where planets light up outward as the voice gets louder.
- **Voices only:** pitch detection ignores claps, bangs and fan noise, and a learned noise floor ignores steady background sound.
- **Stars** fill only while the child's voice is actually heard and under the line.
- **Speech bubbles** ("Nice and quiet", "That's too loud!", "Super star!").
- **Knows its own child's voice:** setup records the child and a grown-up, so adult voices can be ignored.
- **Distance aware:** WiFi CSI learns Near / Mid / Far zones in your room and lowers the line when the child is far away.
- **Louder room, higher line:** with the TV on, the line rises a little.

## First-time setup

Runs automatically on first boot (about 3 minutes), or from Settings › Setup:

1. Join your 2.4 GHz WiFi (on-screen keyboard)
2. 15 s of quiet to learn the room
3. Play near, mid-room and far away, 10 s each, to learn the zones
4. The child's inside voice (10 s), excited voice (6 s), and inside voice from far away
5. A grown-up's voice
6. Summary, then a live zone check

| Zone check | Voice step | Settings |
|---|---|---|
| ![Zone check](docs/screens/check.png) | ![Inside voice](docs/screens/voice.png) | ![Settings](docs/screens/settings-voice.png) |

**Parent settings:** hold the top-right corner for 3 seconds, then enter the PIN (default `1234`, change it under Screen).

## Flash it

**Easiest:** flash [`bin/voicebuddy-es3c28p.bin`](bin/voicebuddy-es3c28p.bin) at address **0x0**, for example with [ESP Web Flasher](https://esptool.spacehuhn.com/), or:

```
esptool.py --chip esp32s3 write_flash 0x0 bin/voicebuddy-es3c28p.bin
```

**From source** ([PlatformIO](https://platformio.org/)):

```
pio run -t erase
pio run -t upload
python tools/make_bin.py        # rebuild the 0x0 image
```

## Hardware notes

| Part | Pins |
|---|---|
| Display ILI9341 (FSPI) | MOSI 11, SCLK 12, MISO 13, CS 10, DC 46, backlight 45 |
| Touch FT6336G (I2C 0x38) | SDA 16, SCL 15, RST 18 |
| Audio codec ES8311 (I2C 0x18) | same I2C bus as touch |
| Audio I2S | MCLK 4, BCLK 5, LRCK 7, **mic data in 6**, speaker out 8, amp enable 1 |
| RGB LED WS2812B | 42 |

The mic data line is **IO6**, the reverse of what the board's BSP docs list. With IO8 as input the mic reads only zeros.

Core 0 runs audio and pitch detection plus CSI sensing; Core 1 only draws. Screen sprites live in PSRAM.

## Serial commands (115200)

`d` codec register dump · `S<view><mood>` force a screen · `W<mode>` open a setup step · `T<tab>` open a settings tab · `P` dump the screen · `X` back to normal
