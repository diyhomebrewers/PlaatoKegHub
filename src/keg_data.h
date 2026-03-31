#ifndef KEG_DATA_H
#define KEG_DATA_H

#include <Arduino.h>
#include "config.h"

struct PlaatoData {
  float amount_left = 0.0;
  float percent_of_beer_left = 0.0;
  float keg_temperature = 0.0;
  float chip_temperature = 0.0;
  bool is_pouring = false;
  int wifi_signal_strength = 0;
  String last_pour = "0.00L";
  float last_pour_value = 0.0;
  float pressure = 0.0;
  int rssi = 0;
  String firmware_version = "2.0.10a";
  String device_type = "ESP32";
  String build_date = "Jul 20 2020 12:31:35";
  
  KegConfig config;
  
  WebhookConfig webhook;
  
  float v52 = 0.0, v53 = 0.0, v54 = 0.0, v59 = 0.0, v62 = 0.0, v63 = 0.0;
  float v65 = 0.0, v66 = 0.0, v69 = 0.0, v71 = 0.0, v73 = 0.0, v74 = 0.0;
  float v75 = 0.0, v76 = 0.0, v82 = 0.0, v86 = 0.0, v87 = 0.0;
  
  float og = 0.0;
  float fg = 0.0;
  float abv = 0.0;
  int mode = 2;
};

extern PlaatoData keg;

void loadKegDataConfig();
void saveKegDataConfig();
void applyKegConfigToKeg();

#endif