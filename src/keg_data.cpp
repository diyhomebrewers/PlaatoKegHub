#include "keg_data.h"
#include "config.h"

PlaatoData keg;

extern bool sendCommandToKeg(uint16_t pin, String value);

static KegConfig lastSentConfig;

void loadKegDataConfig() {
  Serial.println("📂 Loading keg configuration from EEPROM...");
  
  if (!loadKegConfig(keg.config)) {
    Serial.println("📝 No KEG configuration saved, using default values");
    saveKegConfig(keg.config);
  }
  
  if (!loadWebhookConfig(keg.webhook)) {
    Serial.println("📝 No Webhook configuration saved, using default values");
    setDefaultWebhookConfig(keg.webhook);
    saveWebhookConfig(keg.webhook);
  }
  
  memcpy(&lastSentConfig, &keg.config, sizeof(KegConfig));
  
  Serial.println("✅ Configuration loaded:");
  Serial.println("  Tare: " + String(keg.config.empty_keg_weight, 1) + " kg");
  Serial.println("  Max Vol: " + String(keg.config.max_keg_volume, 1) + " L");
  Serial.println("  Webhook Enabled: " + String(keg.webhook.enabled ? "YES" : "NO"));
  Serial.println("  Webhook Device: [" + String(keg.webhook.device_name) + "]");
}

void saveKegDataConfig() {
  Serial.println("💾 Saving configuration to EEPROM...");
  saveKegConfig(keg.config);
  saveWebhookConfig(keg.webhook);  
  Serial.println("✅ Configuration saved");
}

void applyKegConfigToKeg() {
  Serial.println("\n🔄 APPLYING CONFIGURATION TO KEG...");
  
  if (!sendCommandToKeg) {  
    Serial.println("❌ Error: sendCommandToKeg not available");
    return;
  }
  
  int sent = 0;
  int failed = 0;
  
  auto sendIfChanged = [&](uint16_t pin, float value, const char* name) {
    float lastValue = 0;
    switch(pin) {
      case 51: lastValue = lastSentConfig.empty_keg_weight; break;
      case 76: lastValue = lastSentConfig.max_keg_volume; break;
      case 88: lastValue = lastSentConfig.display_mode; break;
    }
    
    if (abs(value - lastValue) > 0.01) {  
      Serial.print("  📤 Sending "); Serial.print(name); 
      Serial.print(": "); Serial.println(value);
      
      if (sendCommandToKeg(pin, String(value, (pin == 88) ? 0 : 1))) {
        sent++;
        switch(pin) {
          case 51: lastSentConfig.empty_keg_weight = value; break;
          case 76: lastSentConfig.max_keg_volume = value; break;
          case 88: lastSentConfig.display_mode = (int)value; break;
        }
        delay(150); 
      } else {
        Serial.println("  ❌ Failed to send " + String(name));
        failed++;
      }
    } else {
      Serial.print("  ⏭️  "); Serial.print(name); 
      Serial.println(" was already configured");
    }
  };
  
  sendIfChanged(51, keg.config.empty_keg_weight, "Tare (V51)");
  sendIfChanged(76, keg.config.max_keg_volume, "Max Volume (V76)");
  sendIfChanged(88, keg.config.display_mode, "Display Mode (V88)");
  
  if (keg.config.calibration_factor != 1.0) {
  }
  
  if (keg.config.temperature_offset != 0.0) {
  }
  
  if (keg.config.unit_system != 1) {
  }
  
  Serial.print("✅ Configuration applied: ");
  Serial.print(sent); Serial.print(" sent, ");
  Serial.print(failed); Serial.println(" failed");
}

void forceReapplyKegConfig() {
  Serial.println("🔄 FORCING configuration reapplication...");
  memset(&lastSentConfig, 0, sizeof(KegConfig));
  applyKegConfigToKeg();
}