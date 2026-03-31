#include "config.h"
#include <Arduino.h>

bool loadWiFiConfig(WiFiConfig &config) {
  uint16_t magic;
  EEPROM.get(WIFI_MAGIC_ADDR, magic);
  
  Serial.print("🔍 WiFi Magic: 0x"); Serial.println(magic, HEX);
  
  memset(&config, 0, sizeof(WiFiConfig));
  
  if (magic == WIFI_MAGIC) {
    EEPROM.get(WIFI_SSID_ADDR, config);
    
    config.ssid[sizeof(config.ssid)-1] = '\0';
    config.password[sizeof(config.password)-1] = '\0';
    config.hostname[sizeof(config.hostname)-1] = '\0';
    config.static_ip[sizeof(config.static_ip)-1] = '\0';
    config.gateway[sizeof(config.gateway)-1] = '\0';
    config.subnet[sizeof(config.subnet)-1] = '\0';
    config.dns1[sizeof(config.dns1)-1] = '\0';
    config.dns2[sizeof(config.dns2)-1] = '\0';
    
    Serial.println("✅ WiFi config loaded");
    return true;
  }
  
  strlcpy(config.hostname, "PlaatoKegHub", sizeof(config.hostname));
  config.use_static_ip = false;
  return false;
}

void saveWiFiConfig(const WiFiConfig &config) {
  EEPROM.put(WIFI_SSID_ADDR, config);
  EEPROM.put(WIFI_MAGIC_ADDR, WIFI_MAGIC);
  EEPROM.commit();
  Serial.println("💾 WiFi saved");
}

// ===== KEG FUNCTIONS =====
uint16_t calculateKegChecksum(const KegConfig &config) {
  uint16_t sum = 0;
  const uint8_t* ptr = (const uint8_t*)&config;
  for (size_t i = 0; i < offsetof(KegConfig, checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}

void setDefaultKegConfig(KegConfig &config) {
  config.magic = KEG_MAGIC;
  config.empty_keg_weight = 2.8;
  config.max_keg_volume = 19.8;
  config.calibration_factor = 1.0;
  config.temperature_offset = 0.0;
  config.sensitivity = 10;
  config.unit_system = 1;
  config.display_mode = 2;
  config.min_temperature_alarm = 0.0;
  config.max_temperature_alarm = 30.0;
  config.checksum = calculateKegChecksum(config);
}

bool loadKegConfig(KegConfig &config) {
  EEPROM.get(KEG_CONFIG_ADDR, config);
  Serial.println("📦 Reading KegConfig from EEPROM:");
  Serial.print("  Magic: 0x"); Serial.println(config.magic, HEX);
  Serial.print("  Tare: "); Serial.println(config.empty_keg_weight);
  return true;
}

void saveKegConfig(const KegConfig &config) {
  Serial.println("💾 Saving KegConfig to EEPROM:");
  Serial.print("  Tare: "); Serial.println(config.empty_keg_weight);
  EEPROM.put(KEG_CONFIG_ADDR, config);
  EEPROM.commit();
  KegConfig verify;
  EEPROM.get(KEG_CONFIG_ADDR, verify);
  Serial.print("  Verified Tare: "); Serial.println(verify.empty_keg_weight);
}

void printKegConfig(const KegConfig &config) {
  Serial.println("=== KEG Configuration ===");
  Serial.print("  Tare: "); Serial.print(config.empty_keg_weight); Serial.println(" kg");
  Serial.print("  Max Volume: "); Serial.print(config.max_keg_volume); Serial.println(" L");
  Serial.print("  Calibration: "); Serial.println(config.calibration_factor);
  Serial.print("  Temp Offset: "); Serial.print(config.temperature_offset); Serial.println(" °C");
  Serial.print("  Sensitivity: "); Serial.println(config.sensitivity);
  Serial.print("  Units: "); Serial.println(config.unit_system == 1 ? "Metric" : "Imperial");
  Serial.print("  Mode: "); Serial.println(config.display_mode == 2 ? "Volume" : "Weight");
  Serial.print("  Min Alarm: "); Serial.print(config.min_temperature_alarm); Serial.println(" °C");
  Serial.print("  Max Alarm: "); Serial.print(config.max_temperature_alarm); Serial.println(" °C");
  Serial.println("==========================");
}

uint16_t calculateWebhookChecksum(const WebhookConfig &config) {
  uint16_t sum = 0;
  const uint8_t* ptr = (const uint8_t*)&config;
  for (size_t i = 0; i < offsetof(WebhookConfig, checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}

void setDefaultWebhookConfig(WebhookConfig &config) {
  config.magic = WEBHOOK_MAGIC;
  memset(config.api_key, 0, sizeof(config.api_key));
  memset(config.device_name, 0, sizeof(config.device_name));
  config.enabled = false;
  config.last_send = 0;
  config.checksum = calculateWebhookChecksum(config);
}

bool loadWebhookConfig(WebhookConfig &config) {
  uint32_t magic;
  EEPROM.get(WEBHOOK_MAGIC_ADDR, magic);
  
  Serial.print("🔍 Webhook Magic separate: 0x"); Serial.println(magic, HEX);
  
  if (magic != WEBHOOK_MAGIC) {
    Serial.println("⚠️ Invalid Webhook magic, using defaults");
    setDefaultWebhookConfig(config);
    return false;
  }
  
  EEPROM.get(WEBHOOK_CONFIG_ADDR, config);
  config.magic = WEBHOOK_MAGIC;
  uint16_t calculated = calculateWebhookChecksum(config);
  if (config.checksum != calculated) {
    Serial.println("⚠️ Invalid Webhook checksum, recalculating...");
    config.checksum = calculated;
    saveWebhookConfig(config);
  }
  
  Serial.println("✅ WebhookConfig loaded");
  Serial.print("  Enabled: "); Serial.println(config.enabled);
  Serial.print("  Device: "); Serial.println(config.device_name);
  return true;
}

void saveWebhookConfig(const WebhookConfig &config) {
  WebhookConfig toSave = config;
  toSave.magic = WEBHOOK_MAGIC;
  toSave.checksum = calculateWebhookChecksum(toSave);
  Serial.println("💾 Saving WebhookConfig:");
  Serial.print("  Enabled: "); Serial.println(toSave.enabled);
  Serial.print("  Device: "); Serial.println(toSave.device_name);
  
  EEPROM.put(WEBHOOK_CONFIG_ADDR, toSave);
  EEPROM.put(WEBHOOK_MAGIC_ADDR, WEBHOOK_MAGIC);
  
  if (EEPROM.commit()) {
    Serial.println("✅ WebhookConfig saved OK");
  } else {
    Serial.println("❌ Error saving WebhookConfig");
  }
}