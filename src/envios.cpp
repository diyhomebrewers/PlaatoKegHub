#include "config.h"
#include "envios.h"
#include "keg_data.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

unsigned long lastMqttSend = 0;
unsigned long lastBrewfatherSend = 0;
unsigned long lastTaplistSend = 0;
unsigned long lastHttpSend = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

uint16_t calculateEnviosChecksum(const EnviosConfig &config) {
  uint16_t sum = 0;
  const uint8_t* ptr = (const uint8_t*)&config;
  for (size_t i = 0; i < offsetof(EnviosConfig, checksum); i++) {
    sum += ptr[i];
  }
  return sum;
}

void setDefaultEnviosConfig(EnviosConfig &config) {
  config.magic = ENVIOS_MAGIC;
  //Defaults:::
  // MQTT
  config.mqtt.enabled = false;
  strlcpy(config.mqtt.server, "192.168.1.100", sizeof(config.mqtt.server));
  config.mqtt.port = 1883;
  strlcpy(config.mqtt.user, "", sizeof(config.mqtt.user));
  strlcpy(config.mqtt.pass, "", sizeof(config.mqtt.pass));
  strlcpy(config.mqtt.topic, "keg/data", sizeof(config.mqtt.topic));
  // Brewfather 
  config.brewfather.enabled = false;
  strlcpy(config.brewfather.device_name, "Keg1", sizeof(config.brewfather.device_name));
  strlcpy(config.brewfather.personal_id, "", sizeof(config.brewfather.personal_id));
  // Taplist.io 
  config.taplist.enabled = false;
  strlcpy(config.taplist.venue_id, "", sizeof(config.taplist.venue_id));
  strlcpy(config.taplist.tap_number, "1", sizeof(config.taplist.tap_number));
  strlcpy(config.taplist.api_token, "", sizeof(config.taplist.api_token));
  // HTTP Generic 
  config.http_generic.enabled = false;
  strlcpy(config.http_generic.url, "http://192.168.1.200:8080/api/data", sizeof(config.http_generic.url));
  strlcpy(config.http_generic.api_key, "", sizeof(config.http_generic.api_key));
  config.checksum = calculateEnviosChecksum(config);
}

bool loadEnviosConfig(EnviosConfig &config) {
  EEPROM.get(ENVIOS_CONFIG_ADDR, config);
  Serial.println("📦 Reading EnviosConfig from EEPROM:");
  Serial.print("  Magic: 0x"); Serial.println(config.magic, HEX);
  return true;
}

void saveEnviosConfig(const EnviosConfig &config) {
  Serial.println("💾 Saving EnviosConfig to EEPROM:");
  Serial.print("  Struct size: "); Serial.println(sizeof(EnviosConfig));
  Serial.print("  ENVIOS_CONFIG_ADDR: "); Serial.println(ENVIOS_CONFIG_ADDR);
  Serial.print("  Ending at: "); Serial.println(ENVIOS_CONFIG_ADDR + sizeof(EnviosConfig));
  Serial.print("  EEPROM_SIZE: "); Serial.println(EEPROM_SIZE);
  Serial.print("  mqtt.server before: "); Serial.println(config.mqtt.server);
  Serial.print("  mqtt.enabled before: "); Serial.println(config.mqtt.enabled);
  
  EEPROM.put(ENVIOS_CONFIG_ADDR, config);
  bool ok = EEPROM.commit();
  Serial.print("  commit result: "); Serial.println(ok);
  
  EnviosConfig verify;
  EEPROM.get(ENVIOS_CONFIG_ADDR, verify);
  Serial.print("  mqtt.server verify: "); Serial.println(verify.mqtt.server);
  Serial.print("  mqtt.enabled verify: "); Serial.println(verify.mqtt.enabled);
}

void sendToMQTT(float volume, float temperature, const MQTTConfig &config) {
  if (!config.enabled || strlen(config.server) == 0) return;
  Serial.println("📡 Sending to MQTT: " + String(config.server));
  mqttClient.setServer(config.server, config.port);
  String clientId = "PlaatoKeg_" + String(random(0xffff), HEX);
  bool connected = false;
  if (strlen(config.user) > 0) {
    connected = mqttClient.connect(clientId.c_str(), config.user, config.pass);
  } else {
    connected = mqttClient.connect(clientId.c_str());
  }
  if (connected) {
    String payload = "{";
    payload += "\"temperature\":" + String(temperature, 1) + ",";
    payload += "\"volume\":" + String(volume, 2);
    payload += "}";
    
    mqttClient.publish(config.topic, payload.c_str());
    mqttClient.disconnect();
    
    Serial.println("✅ MQTT sent successfully");
  } else {
    Serial.println("❌ Error connecting to MQTT");
  }
}

void sendToBrewfather(float volume, float temperature, const BrewfatherConfig &config) {
  if (!config.enabled || strlen(config.personal_id) == 0) return;
  Serial.println("🍺 Sending to Brewfather");
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://log.brewfather.net/stream?id=" + String(config.personal_id);
  String payload = "{";
  payload += "\"name\":\"" + String(config.device_name) + "\",";
  payload += "\"temperature\":" + String(temperature, 1) + ",";
  payload += "\"volume\":" + String(volume, 2);
  payload += "}";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.println("✅ Brewfather: " + String(httpCode));
  } else {
    Serial.println("❌ Brewfather error: " + String(httpCode));
  }
  
  http.end();
}

void sendToTaplist(float volume, float temperature, const TaplistConfig &config) {
  if (!config.enabled || strlen(config.api_token) == 0) return;
  Serial.println("🍻 Sending to Taplist.io");
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  float totalVolume = keg.config.max_keg_volume;
  float servedVolume = (totalVolume - volume) * 1000; 
  
  String endpoint = "https://api.taplist.io/api/v1/venues/" + String(config.venue_id) + 
                   "/taps/" + String(config.tap_number) + "/current-keg";
  
  String payload = "{";
  payload += "\"full_volume_ml\":" + String(totalVolume * 1000, 0) + ",";
  payload += "\"served_volume_ml\":" + String(servedVolume, 0);
  payload += "}";
  http.begin(client, endpoint);
  http.addHeader("Authorization", "token " + String(config.api_token));
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.sendRequest("PATCH", payload);
  
  if (httpCode > 0) {
    Serial.println("✅ Taplist.io: " + String(httpCode));
  } else {
    Serial.println("❌ Taplist.io error: " + String(httpCode));
  }
  
  http.end();
}

void sendToHTTP(float volume, float temperature, const HTTPConfig &config) {
  if (!config.enabled || strlen(config.url) == 0) return;
  Serial.println("🌐 Sending to HTTP: " + String(config.url));
  
  WiFiClient client;
  HTTPClient http;
  bool isSecure = String(config.url).startsWith("https");
  WiFiClientSecure secureClient;
  
  if (isSecure) {
    secureClient.setInsecure();
  }
  
  WiFiClient &httpClient = isSecure ? (WiFiClient&)secureClient : client;
  String payload = "{";
  payload += "\"api_key\":\"" + String(config.api_key) + "\",";
  payload += "\"temperature\":" + String(temperature, 1) + ",";
  payload += "\"volume\":" + String(volume, 2);
  payload += "}";
  http.begin(httpClient, config.url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.println("✅ HTTP: " + String(httpCode));
  } else {
    Serial.println("❌ HTTP error: " + String(httpCode));
  }
  
  http.end();
}

void sendToAllDestinations(float volume, float temperature) {
  static EnviosConfig enviosConfig;
  static unsigned long lastConfigLoad = 0;
  unsigned long now = millis();
  if (now - lastConfigLoad > 10000) {
    loadEnviosConfig(enviosConfig);
    lastConfigLoad = now;
    Serial.println("=== ENVIOS CONFIG ===");
    Serial.print("  MQTT enabled: "); Serial.println(enviosConfig.mqtt.enabled);
    Serial.print("  MQTT server: "); Serial.println(enviosConfig.mqtt.server);
    Serial.print("  Brewfather enabled: "); Serial.println(enviosConfig.brewfather.enabled);
    Serial.print("  Brewfather id: "); Serial.println(enviosConfig.brewfather.personal_id);
    Serial.print("  Taplist enabled: "); Serial.println(enviosConfig.taplist.enabled);
    Serial.print("  HTTP enabled: "); Serial.println(enviosConfig.http_generic.enabled);
    Serial.println("====================");
  }
  
  if (enviosConfig.mqtt.enabled && (now - lastMqttSend > 10000)) {
    sendToMQTT(volume, temperature, enviosConfig.mqtt);
    lastMqttSend = now;
  }
  
  if (enviosConfig.brewfather.enabled && (now - lastBrewfatherSend > 900000)) {
    sendToBrewfather(volume, temperature, enviosConfig.brewfather);
    lastBrewfatherSend = now;
  }
  
  if (enviosConfig.taplist.enabled && (now - lastTaplistSend > 30000)) {
    sendToTaplist(volume, temperature, enviosConfig.taplist);
    lastTaplistSend = now;
  }
  
  if (enviosConfig.http_generic.enabled && (now - lastHttpSend > 300000)) {
    sendToHTTP(volume, temperature, enviosConfig.http_generic);
    lastHttpSend = now;
  }
}