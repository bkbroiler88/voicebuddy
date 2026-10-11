#pragma once
#include <Arduino.h>

// Home WiFi for CSI sensing: joins the network, then pings the router fast so a
// steady stream of frames comes back from it (each one carries CSI).

struct ApInfo { char ssid[33]; int rssi; bool secure; };

bool        netHasCreds();
const char *netSsid();
void        netSetCreds(const char *ssid, const char *pass);   // saved on the device only

int   netScan(ApInfo *out, int maxN);         // blocking ~3 s, strongest first, 2.4 GHz only
bool  netConnectBlocking(uint32_t timeoutMs); // uses saved credentials
void  netBegin();                             // non-blocking connect with saved credentials
bool  netConnected();
void  netDisconnect();

const uint8_t *netBssid();                    // router MAC, valid while connected
int   netChannel();

void  netStartTraffic();                      // ~125 pings/s to the gateway
void  netStopTraffic();
