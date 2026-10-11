#pragma once
#include <Arduino.h>

// ---------------- Board pins (LCDWIKI ES3C28P) ----------------
// Shared I2C bus: FT6336G touch (0x38) + ES8311 codec (0x18)
#define I2C_SDA        16
#define I2C_SCL        15
#define TOUCH_RST      18
#define TOUCH_ADDR     0x38
#define ES8311_ADDR    0x18

// ES8311 I2S
#define I2S_MCLK_PIN   4
#define I2S_BCLK_PIN   5
#define I2S_LRCK_PIN   7
#define I2S_DOUT_PIN   8   // ESP -> codec DAC (speaker)
#define I2S_DIN_PIN    6   // codec ADC (mic) -> ESP

// FM8002E speaker amp enable. BSP says "active low" -- flip if the chime is silent.
#define AMP_EN_PIN     1
#define AMP_ON_LEVEL   LOW

#define NEOPIXEL_PIN   42
#define BACKLIGHT_PIN  45

// ---------------- Audio ----------------
#define AUDIO_RATE     16000
#define AUDIO_FRAMES   256      // frames per block (16 ms)

// ---------------- Behaviour timing ----------------
#define LOUD_ENTER_S        0.25f  // must stay over the limit this long to count (ignores claps/bangs)
#define LOUD_EXIT_S         1.50f  // calm for this long before Buddy uncovers his ears
#define LOUD_EXIT_HYST_DB   3.0f
#define ALERT_ENTER_S       0.15f
#define ALERT_EXIT_S        0.80f
#define ALERT_EXIT_HYST_DB  2.0f
#define PROUD_SHOW_S        3.0f
#define CELEBRATE_SHOW_S    6.0f
#define OFFSET_GLIDE_DB_S   2.0f   // how fast the limit follows a zone change

#define PARENT_HOLD_MS      3000   // hold top-right corner to open parent settings
#define ZONE_HOLD_MS        3000   // CSI zone must be stable this long before it is used
#define CAL_LEADIN_MS       5000   // time for the parent to step away before calibration
#define CAL_CAPTURE_MS      6000
