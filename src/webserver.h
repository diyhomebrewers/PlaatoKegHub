#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "keg_data.h"
void setupWebServer();
void handleWebServer();
void handleAcerca(WiFiClient &client);
void handleOTAUpload(WiFiClient &client, String &header, uint8_t* data, size_t dataLen);
void processOTAFromSPIFFS(WiFiClient &client); 

#endif