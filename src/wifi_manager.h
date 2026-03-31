#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include "config.h"

void initWiFi();
bool connectToWiFi(const char* ssid, const char* password);
void startAPMode();
String getConnectionStatus();
IPAddress getIP();

#endif