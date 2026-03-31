#include <WiFi.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "wifi_manager.h"
#include "webserver.h"
#include "keg_data.h"
#include "token_manager.h"
#include <SPIFFS.h>

WiFiServer server(1234);
WiFiClient client;
uint8_t buffer[1024];

unsigned long lastWebhookCheck = 0;
const unsigned long WEBHOOK_CHECK_INTERVAL = 10000; 
unsigned long lastHeartbeatSent = 0;
const unsigned long HEARTBEAT_INTERVAL = 8000; 
uint16_t heartbeatMsgId = 1000; 

void printHex(uint8_t* data, int len) {
  Serial.println("===== PLAATO RAW =====");
  Serial.print("Bytes: "); Serial.println(len);
  for(int i=0; i<len; i++){
    if(data[i]<16) Serial.print("0");
    Serial.print(data[i], HEX); Serial.print(" ");
    if((i+1)%16==0) Serial.println();
  }
  Serial.println("\n======================");
}

void sendBlynkSuccess(uint16_t msg_id) {
  uint8_t response[5];
  response[0] = 0x00;
  response[1] = (uint8_t)(msg_id >> 8);
  response[2] = (uint8_t)(msg_id & 0xFF);
  response[3] = 0x00;
  response[4] = 0xC8;

  client.write(response, 5);
  client.flush();
  Serial.print("✅ ACK sent for ID: "); Serial.println(msg_id);
}

void sendBlynkHeartbeat() {
  if (!client || !client.connected()) return;
    uint8_t heartbeat[5];
  heartbeat[0] = 0x06; 
  heartbeat[1] = (uint8_t)(heartbeatMsgId >> 8);
  heartbeat[2] = (uint8_t)(heartbeatMsgId & 0xFF);
  heartbeat[3] = 0x00; 
  heartbeat[4] = 0x00;  
  
  client.write(heartbeat, 5);
  client.flush();
  Serial.print("💓 Heartbeat sent (ID: ");
  Serial.print(heartbeatMsgId);
  Serial.println(")");
  
  heartbeatMsgId++;
  if (heartbeatMsgId > 65000) heartbeatMsgId = 1000; 
}

String cleanValue(String val) {
  val.replace("°C", "");
  val.replace("C", "");
  val.replace("L", "");
  val.replace("kg", "");
  val.replace("litre", "");
  val.replace(" ", "");
  val.trim();
  return val;
}

bool validateKegToken(uint8_t* data, int len) {
  if (len < 37) return false;
  
  char token_ascii[33] = {0};
  for (int i = 0; i < 32; i++) {
    token_ascii[i] = (char)data[5 + i];
  }
  
  for (int i = 0; i < 32; i++) {
    token_ascii[i] = toupper(token_ascii[i]);
  }
  
  char expected_token[33] = {0};
  for (int i = 0; i < 16; i++) {
    sprintf(&expected_token[i*2], "%02X", active_plaato_token[i]);
  }
  
  return (strncmp(token_ascii, expected_token, 32) == 0);
}

bool sendDataToWebhook() {
  if (!keg.webhook.enabled) {
    Serial.println("📤 Webhook not enabled, skipping send");
    return false;
  }
  
  if (strlen(keg.webhook.api_key) == 0 || strlen(keg.webhook.device_name) == 0) {
    Serial.println("📤 Webhook: Missing API Key or Device Name");
    return false;
  }
  
  unsigned long now = millis() / 1000;
  if (keg.webhook.last_send != 0 && (now - keg.webhook.last_send) < 300) {
    unsigned long remaining = 300 - (now - keg.webhook.last_send);
    Serial.print("📤 Webhook: Waiting ");
    Serial.print(remaining);
    Serial.println(" seconds for next send");
    return false;
  }
  
  Serial.println("\n=== SENDING DATA TO WEBHOOK ===");
  
  String url = "https://diyhomebrewers.com/wp-json/trk/v1/barrel/dh?api_key=";
  url += String(keg.webhook.api_key);
  String payload = "{";
  payload += "\"name\":\"" + String(keg.webhook.device_name) + "\",";
  payload += "\"temperature\":" + String(keg.keg_temperature, 1) + ",";
  payload += "\"volume\":" + String(keg.amount_left, 2);
  payload += "}";
  
  Serial.print("📤 URL: "); Serial.println(url);
  Serial.print("📦 Payload: "); Serial.println(payload);
  
  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();
  
  bool success = false;
  
  if (clientSecure.connect("diyhomebrewers.com", 443)) {
    clientSecure.println("POST /wp-json/trk/v1/barrel/dh?api_key=" + String(keg.webhook.api_key) + " HTTP/1.1");
    clientSecure.println("Host: diyhomebrewers.com");
    clientSecure.println("Content-Type: application/json");
    clientSecure.print("Content-Length: ");
    clientSecure.println(payload.length());
    clientSecure.println();
    clientSecure.println(payload);
    
    unsigned long timeout = millis() + 5000;
    while (clientSecure.available() == 0) {
      if (millis() > timeout) {
        Serial.println("❌ Timeout waiting for response");
        clientSecure.stop();
        return false;
      }
    }
    
    String response = "";
    while (clientSecure.available()) {
      response += clientSecure.readString();
    }
    
    Serial.print("📥 Response: "); Serial.println(response);
    
    if (response.indexOf("200 OK") > 0 || response.indexOf("200") > 0) {
      Serial.println("✅ Data sent successfully");
      success = true;
    } else {
      Serial.println("❌ Remote server error");
      Serial.println(response.substring(0, 200));
    }
    
    clientSecure.stop();
  } else {
    Serial.println("❌ Could not connect to server");
  }
  
  if (success) {
    keg.webhook.last_send = millis() / 1000;
    saveKegConfig(keg.config);
    Serial.println("💾 Timestamp updated in EEPROM");
  }
  
  Serial.println("=== WEBHOOK SEND END ===\n");
  return success;
}

void handleBlynkPacket(uint8_t* data, int len) {
  if (len < 5) return;
  uint8_t command = data[0];
  uint16_t msg_id = (uint16_t)data[1] << 8 | data[2];
  uint16_t payload_len = (uint16_t)data[3] << 8 | data[4];

  Serial.print("📦 Command: 0x"); Serial.print(command, HEX);
  Serial.print(", ID: "); Serial.print(msg_id);
  Serial.print(", Payload: "); Serial.println(payload_len);

  sendBlynkSuccess(msg_id);
  if (command == 0x06) {
    Serial.println("💓 Heartbeat received from Keg");
    return;
  }

  if (command == 0x14 && payload_len > 0) {
    String payload = "";
    for (int i = 0; i < payload_len; i++) {
      payload += (char)data[5 + i];
    }
    
    int pos = 0;
    while (pos < payload.length()) {
      if (payload.substring(pos, pos + 2) == "vw") {
        pos += 3; 
        
        int pin_end = payload.indexOf('\0', pos);
        if (pin_end == -1) break;
        String pin_str = payload.substring(pos, pin_end);
        pos = pin_end + 1;
        
        int val_end = payload.indexOf('\0', pos);
        if (val_end == -1) val_end = payload.length();
        String val_str = payload.substring(pos, val_end);
        pos = val_end + 1;

        int pin = pin_str.toInt();
        
        Serial.print("🔹 Pin V"); Serial.print(pin);
        Serial.print(" = "); Serial.println(val_str);

        String clean_val = cleanValue(val_str);
        
        bool isNumeric = true;
        for (int i = 0; i < clean_val.length(); i++) {
          char c = clean_val[i];
          if (!(isdigit(c) || c == '.' || c == '-' || c == '+')) {
            isNumeric = false;
            break;
          }
        }
        
        if (isNumeric && clean_val.length() > 0) {
          float num_val = clean_val.toFloat();
          
          if (num_val > 1000000 || num_val < -1000000) {
            Serial.println("⚠️ Suspicious value ignored");
            continue;
          }
          
          switch(pin) {
            case 51: keg.amount_left = num_val; break;
            case 48: keg.percent_of_beer_left = num_val; break;
            case 56: keg.keg_temperature = num_val; break;
            case 92: keg.chip_temperature = num_val; break;
            case 49: keg.is_pouring = (num_val != 0); break;
            case 81: keg.wifi_signal_strength = (int)num_val; break;
            case 47: 
              keg.last_pour = val_str;
              keg.last_pour_value = num_val;
              break;
            case 55: keg.pressure = num_val; break;
            case 83: keg.rssi = (int)num_val; break;
            case 52: keg.v52 = num_val; break;
            case 53: keg.v53 = num_val; break;
            case 54: keg.v54 = num_val; break;
            case 59: keg.v59 = num_val; break;
            case 62: keg.v62 = num_val; break;
            case 63: keg.v63 = num_val; break;
            case 65: keg.v65 = num_val; break;
            case 66: keg.v66 = num_val; break;
            case 69: keg.v69 = num_val; break;
            case 71: keg.v71 = num_val; break;
            case 73: keg.v73 = num_val; break;
            case 74: keg.v74 = num_val; break;
            case 75: keg.v75 = num_val; break;
            case 76: keg.v76 = num_val; break;
            case 82: keg.v82 = num_val; break;
            case 86: keg.v86 = num_val; break;
            case 87: keg.v87 = num_val; break;
            case 93: keg.firmware_version = val_str; break;
          }
        } else {
          Serial.println("🔤 Non-numeric value: " + val_str);
          switch(pin) {
            case 73: /* kg */ break;
            case 82: /* litre */ break;
            case 80: /* °C */ break;
            case 93: keg.firmware_version = val_str; break;
          }
        }
      } else {
        pos++;
      }
    }
  }
  
  if (command == 0x13 && payload_len > 0) {
    String payload = "";
    for (int i = 0; i < payload_len; i++) {
      payload += (char)data[5 + i];
    }
    Serial.print("⚙️ Config: "); Serial.println(payload);
  }
}

bool sendCommandToKeg(uint16_t pin, String value) {
  Serial.print("🔌 Attempting to send to V");
  Serial.print(pin);
  Serial.print(" = ");
  Serial.println(value);
  
  if (!client || !client.connected()) {
    Serial.println("❌ Keg NOT connected");
    return false;
  }
  
  String payload = "vw";
  payload += '\0';
  payload += String(pin);
  payload += '\0';
  payload += value;
  payload += '\0';
  
  uint8_t buffer[256];
  buffer[0] = 0x14;
  buffer[1] = 0x00;
  buffer[2] = 0x01;
  buffer[3] = (payload.length() >> 8) & 0xFF;
  buffer[4] = payload.length() & 0xFF;
  
  for (int i = 0; i < payload.length(); i++) {
    buffer[5 + i] = payload[i];
  }
  
  size_t bytesSent = client.write(buffer, 5 + payload.length());
  client.flush();
  
  if (bytesSent > 0) {
    Serial.print("✅ Command sent to V");
    Serial.print(pin);
    Serial.print(": ");
    Serial.println(value);
    return true;
  } else {
    Serial.println("❌ Error sending");
    return false;
  }
}

void sendTareToKeg(float weight) {
  sendCommandToKeg(51, String(weight, 1));
}

void sendMaxVolumeToKeg(float volume) {
  sendCommandToKeg(76, String(volume, 1));
}

void sendDisplayModeToKeg(int mode) {
  if (mode == 1 || mode == 2) {
    sendCommandToKeg(88, String(mode));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ Error mounting SPIFFS");
    } else {
        Serial.println("✅ SPIFFS mounted successfully");
    }
  Serial.println("\n\n=== PLAATO KEG ESP32 ===");
  if (!SPIFFS.begin(true)) {
    Serial.println("⚠️ Error initializing SPIFFS");
  } else {
    Serial.println("✅ SPIFFS initialized");
    Serial.print("SPIFFS Total: "); Serial.print(SPIFFS.totalBytes());
    Serial.print(", Used: "); Serial.println(SPIFFS.usedBytes());
  }

  EEPROM.begin(EEPROM_SIZE);
  
  Serial.println("\n🔍=== COMPLETE EEPROM DUMP ===");
  for (int i = 0; i < EEPROM_SIZE; i++) {
    uint8_t value = EEPROM.read(i);
    if (value != 0) {
      Serial.print("Addr "); 
      Serial.print(i);
      Serial.print(": 0x");
      if (value < 16) Serial.print("0");
      Serial.print(value, HEX);
      Serial.print(" (");
      Serial.print(value);
      Serial.println(")");
    }
  }
  Serial.println("=== DUMP END ===\n");
  
  Serial.println("\n🔑=== LOADING PLAATO TOKEN ===");
  PlaatoTokenConfig tokenConfig;
  loadPlaatoToken(tokenConfig);
  Serial.print("Active token: ");
  for (int i = 0; i < 16; i++) {
    if (active_plaato_token[i] < 16) Serial.print("0");
    Serial.print(active_plaato_token[i], HEX);
  }
  Serial.println("\n=============================\n");
  
  Serial.println("📂 Loading keg configuration from EEPROM...");
  loadKegDataConfig();
  
  if (loadKegConfig(keg.config)) {
    Serial.println("✅ Configuration loaded SUCCESSFULLY:");
  } else {
    Serial.println("⚠️ No valid configuration, using DEFAULT values:");
    setDefaultKegConfig(keg.config);
  }
  
  Serial.println("=== CURRENT CONFIGURATION IN MEMORY ===");
  Serial.print("  Tare: "); Serial.print(keg.config.empty_keg_weight); Serial.println(" kg");
  Serial.print("  Max Volume: "); Serial.print(keg.config.max_keg_volume); Serial.println(" L");
  Serial.print("  Calibration: "); Serial.println(keg.config.calibration_factor);
  Serial.print("  Temp Offset: "); Serial.print(keg.config.temperature_offset); Serial.println(" °C");
  Serial.print("  Sensitivity: "); Serial.println(keg.config.sensitivity);
  Serial.print("  Units: "); Serial.println(keg.config.unit_system == 1 ? "Metric" : "Imperial");
  Serial.print("  Mode: "); Serial.println(keg.config.display_mode == 2 ? "Volume" : "Weight");
  Serial.print("  Min Alarm: "); Serial.print(keg.config.min_temperature_alarm); Serial.println(" °C");
  Serial.print("  Max Alarm: "); Serial.print(keg.config.max_temperature_alarm); Serial.println(" °C");
  Serial.println("  === WEBHOOK ===");
  Serial.print("    Enabled: "); Serial.println(keg.webhook.enabled ? "YES" : "NO");
  Serial.print("    Device Name: "); Serial.println(keg.webhook.device_name);
  Serial.print("    API Key: "); Serial.println(keg.webhook.api_key);
  Serial.print("    Last Send: "); Serial.println(keg.webhook.last_send);
  Serial.println("==========================================");
  
  initWiFi();
  
  server.begin();
  server.setNoDelay(true);
  Serial.println("🎯 Plaato Keg server ready (port 1234)");
  
  setupWebServer();
  
  Serial.println("✅ System ready!");
}

void loop() {
  static bool configApplied = false;
  static unsigned long lastPrint = 0;
  static unsigned long connectionTime = 0;
  static bool firstPacket = true;
  
  if (!client || !client.connected()) {
    configApplied = false;
    firstPacket = true;
    WiFiClient newClient = server.available();
    if (newClient) {
      if (client) client.stop();
      client = newClient;
      client.setNoDelay(true);
      Serial.println("\n🔌 PLAATO KEG CONNECTED 🔌");
      connectionTime = millis();
      lastHeartbeatSent = millis();
    }
  }

  if (client && client.connected()) {
    if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
      sendBlynkHeartbeat();
      lastHeartbeatSent = millis();
    }
  }

  if (client && client.connected()) {
    int avail = client.available();
    if (avail > 0) {
      int bytes_read = client.read(buffer, sizeof(buffer));
      if (bytes_read > 0) {
        printHex(buffer, bytes_read);
        if (firstPacket) {
          if (validateKegToken(buffer, bytes_read)) {
            Serial.println("✅ Correct token - Processing data");
            firstPacket = false;
            handleBlynkPacket(buffer, bytes_read);
          } else {
            Serial.println("❌ Incorrect token - Ignoring this Plaato");
          }
        } else {
          handleBlynkPacket(buffer, bytes_read);
        }
      }
    }
  }

  if (client && client.connected() && !firstPacket && !configApplied) {
    if (millis() - connectionTime > 3000 && keg.amount_left > 0) {
      delay(500);
      Serial.println("⚙️ Applying saved configuration to Keg...");
      applyKegConfigToKeg();
      configApplied = true;
    }
  }

  if (millis() - lastWebhookCheck > WEBHOOK_CHECK_INTERVAL) {
    lastWebhookCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToWebhook();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    sendToAllDestinations(keg.amount_left, keg.keg_temperature);
  }
  
  // Debug 
  if (millis() - lastPrint > 10000) {
    lastPrint = millis();
    if (client && client.connected() && !firstPacket) {
      Serial.println("\n📊 === COMPLETE KEG STATUS ===");
      Serial.print("🍺 Volume: "); Serial.print(keg.amount_left, 3); Serial.println(" L");
      Serial.print("📊 Percentage: "); Serial.print(keg.percent_of_beer_left, 1); Serial.println(" %");
      Serial.print("🌡️  Keg Temp: "); Serial.print(keg.keg_temperature, 1); Serial.println(" °C");
      Serial.print("🔥 Chip Temp: "); Serial.print(keg.chip_temperature, 1); Serial.println(" °C");
      Serial.print("📶 WiFi Signal: "); Serial.print(keg.wifi_signal_strength); Serial.println(" %");
      Serial.print("📻 RSSI: "); Serial.print(keg.rssi); Serial.println(" dBm");
      Serial.print("🍻 Pouring: "); Serial.println(keg.is_pouring ? "YES 🔴" : "NO ⚪");
      Serial.print("💧 Last Pour: "); Serial.print(keg.last_pour_value, 2); Serial.println(" L");
      Serial.print("⚙️  Pressure: "); Serial.print(keg.pressure, 2); Serial.println(" ?");
      Serial.print("📱 Firmware: "); Serial.println(keg.firmware_version);
      Serial.println("--- Current Configuration ---");
      Serial.print("⚖️  Tare: "); Serial.print(keg.config.empty_keg_weight, 1); Serial.println(" kg");
      Serial.print("📦 Max Vol: "); Serial.print(keg.config.max_keg_volume, 1); Serial.println(" L");
      Serial.print("🎯 Mode: "); Serial.println(keg.config.display_mode == 2 ? "Volume" : "Weight");
      Serial.println("--- Webhook ---");
      Serial.print("📤 Status: "); Serial.println(keg.webhook.enabled ? "ACTIVE" : "INACTIVE");
      if (keg.webhook.enabled) {
        Serial.print("  Last send: "); 
        if (keg.webhook.last_send == 0) {
          Serial.println("Never");
        } else {
          unsigned long seconds_ago = (millis() / 1000) - keg.webhook.last_send;
          Serial.print(seconds_ago); Serial.println(" seconds ago");
        }
      }
      Serial.println("===================================\n");
    }
  }
  
  handleWebServer();
  delay(10);
}