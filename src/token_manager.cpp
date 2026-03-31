#include "config.h"
#include <Arduino.h>
#include "token_manager.h"

const uint8_t DEFAULT_PLAATO_TOKEN[16] = {
  0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x7A, 0x8B,
  0x9C, 0x0D, 0x1E, 0x2F, 0x3A, 0x4B, 0x5C, 0x6D
};

uint8_t active_plaato_token[16];

static uint16_t calculateTokenChecksum(const PlaatoTokenConfig &config) {
  uint16_t sum = 0;
  const uint8_t* ptr = (const uint8_t*)&config;
  for (size_t i = 0; i < offsetof(PlaatoTokenConfig, checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}

void setDefaultPlaatoToken(PlaatoTokenConfig &config) {
  config.magic = TOKEN_MAGIC;
  
  memcpy(config.raw_token, DEFAULT_PLAATO_TOKEN, 16);
  
  char hex_str[33] = {0};
  for (int i = 0; i < 16; i++) {
    sprintf(&hex_str[i*2], "%02X", config.raw_token[i]);
  }
  strlcpy(config.token, hex_str, sizeof(config.token));
  
  config.checksum = calculateTokenChecksum(config);
  
  memcpy(active_plaato_token, config.raw_token, 16);
  
  Serial.println("🔑 Default token set");
  Serial.print("Token HEX: "); Serial.println(config.token);
}

bool loadPlaatoToken(PlaatoTokenConfig &config) {
  EEPROM.get(PLAATO_TOKEN_ADDR, config);
  
  if (config.magic == TOKEN_MAGIC) {
    uint16_t calc_checksum = calculateTokenChecksum(config);
    if (calc_checksum == config.checksum) {
      Serial.println("✅ Valid token loaded from EEPROM");
      memcpy(active_plaato_token, config.raw_token, 16);
      return true;
    } else {
      Serial.println("⚠️ Invalid token checksum, using default");
    }
  } else {
    Serial.println("📝 No token saved, using default");
  }
  
  setDefaultPlaatoToken(config);
  return false;
}

void savePlaatoToken(const PlaatoTokenConfig &config) {
  PlaatoTokenConfig configToSave = config;
  configToSave.magic = TOKEN_MAGIC;
  configToSave.checksum = calculateTokenChecksum(configToSave);
  
  EEPROM.put(PLAATO_TOKEN_ADDR, configToSave);
  EEPROM.commit();
  
  memcpy(active_plaato_token, configToSave.raw_token, 16);
  
  Serial.println("💾 Token saved to EEPROM");
  Serial.print("Token: "); Serial.println(configToSave.token);
}

bool validatePlaatoToken(const uint8_t* received_token) {
  for (int i = 0; i < 16; i++) {
    if (received_token[i] != active_plaato_token[i]) {
      return false;
    }
  }
  return true;
}

bool hexStringToBytes(const char* hex_str, uint8_t* bytes, int len) {
  char hex_byte[3] = {0};
  for (int i = 0; i < len; i++) {
    hex_byte[0] = hex_str[i*2];
    hex_byte[1] = hex_str[i*2 + 1];
    char* endptr;
    bytes[i] = (uint8_t)strtol(hex_byte, &endptr, 16);
    if (*endptr != '\0') return false;
  }
  return true;
}