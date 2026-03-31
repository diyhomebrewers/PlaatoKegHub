#ifndef ENVIOS_H
#define ENVIOS_H

#include <Arduino.h>
#include "config.h"
#include "keg_data.h"  

void sendToAllDestinations(float volume, float temperature);
bool loadEnviosConfig(EnviosConfig &config);
void saveEnviosConfig(const EnviosConfig &config);
void setDefaultEnviosConfig(EnviosConfig &config);

#endif