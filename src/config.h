#ifndef CONFIG_H
#define CONFIG_H

#include <EEPROM.h>
#include <WiFi.h>

#define EEPROM_SIZE 2000

#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 32
#define HOSTNAME_ADDR 96
#define WIFI_MAGIC_ADDR 208    
#define WIFI_MAGIC 0x4547
#define KEG_CONFIG_ADDR 220
#define KEG_MAGIC_ADDR 260     
#define KEG_MAGIC 0x4B4547
#define PLAATO_TOKEN_ADDR 270
#define TOKEN_MAGIC_ADDR 320   
#define TOKEN_MAGIC 0x544F4B
#define WEBHOOK_CONFIG_ADDR 330
#define WEBHOOK_MAGIC_ADDR 450 
#define WEBHOOK_MAGIC 0x57484B
#define ENVIOS_CONFIG_ADDR 460      
#define ENVIOS_MAGIC_ADDR 1000     
#define ENVIOS_MAGIC 0x454E56   

struct WiFiConfig {
  char ssid[32];
  char password[64];
  char hostname[32];     
  bool use_static_ip;
  char static_ip[16];
  char gateway[16];
  char subnet[16];
  char dns1[16];
  char dns2[16];
};

struct WebhookConfig {
  uint32_t magic;
  char api_key[65];
  char device_name[33];
  bool enabled;
  unsigned long last_send;
  uint16_t checksum;
};

struct KegConfig {
  uint32_t magic;
  float empty_keg_weight;
  float max_keg_volume;
  float calibration_factor;
  float temperature_offset;
  int sensitivity;
  int unit_system;
  int display_mode;
  float min_temperature_alarm;
  float max_temperature_alarm;
  uint16_t checksum;
};

struct PlaatoTokenConfig {
  uint32_t magic;
  char token[33];
  uint8_t raw_token[16];
  uint16_t checksum;
};

struct MQTTConfig {
  char server[64];
  uint16_t port;
  char user[32];
  char pass[32];
  char topic[64];
  bool enabled;
};

struct BrewfatherConfig {
  char device_name[32];
  char personal_id[64];
  bool enabled;
};

struct TaplistConfig {
  char venue_id[32];
  char tap_number[8];
  char api_token[64];
  bool enabled;
};

struct HTTPConfig {
  char url[128];
  char api_key[64];
  bool enabled;
};

struct EnviosConfig {
  uint32_t magic;
  MQTTConfig mqtt;
  BrewfatherConfig brewfather;
  TaplistConfig taplist;
  HTTPConfig http_generic;
  uint16_t checksum;
};


// Funciones
bool loadWiFiConfig(WiFiConfig &config);
void saveWiFiConfig(const WiFiConfig &config);
void clearWiFiConfig();
bool loadKegConfig(KegConfig &config);
void saveKegConfig(const KegConfig &config);
void setDefaultKegConfig(KegConfig &config);
void printKegConfig(const KegConfig &config);
bool loadWebhookConfig(WebhookConfig &config);
void saveWebhookConfig(const WebhookConfig &config);
void setDefaultWebhookConfig(WebhookConfig &config);
bool loadPlaatoToken(PlaatoTokenConfig &tokenConfig);
void savePlaatoToken(const PlaatoTokenConfig &tokenConfig);
void setDefaultPlaatoToken(PlaatoTokenConfig &tokenConfig);
bool validatePlaatoToken(const uint8_t* received_token);
bool loadEnviosConfig(EnviosConfig &config);
void saveEnviosConfig(const EnviosConfig &config);
void setDefaultEnviosConfig(EnviosConfig &config);
void sendToAllDestinations(float volume, float temperature);
#endif