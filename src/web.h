#pragma once
#include <Arduino.h>

// HTTPS parent page. Runs only while g.webOn is set and WiFi is connected.
// The server task never touches the UI: anything that changes the device is
// queued as a WebCmd and applied by the main loop (webPollCmd).

enum WebCmdType : uint8_t { WC_STAR, WC_RESET_STARS, WC_BUBBLE, WC_VIEW, WC_PAUSE, WC_SETTINGS, WC_RESTART, WC_SIGNIN };
struct WebCmd {
  WebCmdType type;
  int16_t a;
  char text[48];
};

// live values the main loop publishes for /api/status
struct WebStatus {
  volatile float level, line, warn;
  volatile int mood;          // Mood enum, or -1 = paused
  volatile float prog;
  volatile bool paused;
};
extern WebStatus webStatus;

void webBegin();               // once in setup()
void webService();             // often from loop(): starts/stops with WiFi + g.webOn
bool webRunning();
bool webPollCmd(WebCmd &c);
int  webSessionCount();
void webRevokeAll();
void webRevokeById(const char *id);
