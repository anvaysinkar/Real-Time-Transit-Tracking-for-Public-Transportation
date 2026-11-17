#pragma once
#include "Arduino.h"
enum { WIFI_AP=2, WIFI_AP_STA=3, WL_CONNECTED=3 };
struct IPAddress { String toString() const { return String("192.168.4.1"); } };
struct WiFiClass { void mode(int){} bool softAP(const char*,const char*){return true;} void begin(const char*,const char*){} void setAutoReconnect(bool){} int status(){return 0;} IPAddress softAPIP(){return {};} };
extern WiFiClass WiFi;
