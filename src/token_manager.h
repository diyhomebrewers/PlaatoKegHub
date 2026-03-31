#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include <Arduino.h>
#include "config.h"

extern uint8_t active_plaato_token[16];

void setDefaultPlaatoToken(PlaatoTokenConfig &config);
bool loadPlaatoToken(PlaatoTokenConfig &config);
void savePlaatoToken(const PlaatoTokenConfig &config);
bool validatePlaatoToken(const uint8_t* received_token);
bool hexStringToBytes(const char* hex_str, uint8_t* bytes, int len);

#endif