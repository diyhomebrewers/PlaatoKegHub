#include "wifi_manager.h"
#include "config.h"
#include <ESPmDNS.h>

bool isAPMode = false;

void initWiFi() {
  WiFiConfig config;
  bool hasConfig = loadWiFiConfig(config);
  
  Serial.println("🔍 DEBUG - Loaded config:");
  Serial.print("  SSID: '"); Serial.print(config.ssid); Serial.println("'");
  Serial.print("  Use Static IP: "); Serial.println(config.use_static_ip);
  Serial.print("  IP string: '"); Serial.print(config.static_ip); Serial.println("'");
  Serial.print("  Gateway string: '"); Serial.print(config.gateway); Serial.println("'");
  Serial.print("  Subnet string: '"); Serial.print(config.subnet); Serial.println("'");
  Serial.print("  DNS1 string: '"); Serial.print(config.dns1); Serial.println("'");
  Serial.print("  DNS2 string: '"); Serial.print(config.dns2); Serial.println("'");
  
  if (hasConfig && strlen(config.ssid) > 0) {
    Serial.print("📡 Connecting to WiFi: ");
    Serial.println(config.ssid);
    
    WiFi.mode(WIFI_STA);
    
    if (config.use_static_ip) {
      if (strlen(config.static_ip) > 0 && strlen(config.gateway) > 0 && strlen(config.subnet) > 0) {
        
        IPAddress ip, gateway, subnet, dns1, dns2;
        
        if (ip.fromString(config.static_ip) && 
            gateway.fromString(config.gateway) && 
            subnet.fromString(config.subnet)) {
          
          Serial.println("🔧 Configuring STATIC IP:");
          Serial.print("  IP: "); Serial.println(config.static_ip);
          Serial.print("  Gateway: "); Serial.println(config.gateway);
          Serial.print("  Subnet: "); Serial.println(config.subnet);
          
          if (strlen(config.dns1) > 0) {
            dns1.fromString(config.dns1);
            Serial.print("  DNS1: "); Serial.println(config.dns1);
          } else {
            dns1 = gateway; 
            Serial.println("  DNS1: using gateway");
          }
          
          if (strlen(config.dns2) > 0) {
            dns2.fromString(config.dns2);
            Serial.print("  DNS2: "); Serial.println(config.dns2);
          } else {
            dns2 = IPAddress(8,8,8,8); 
            Serial.println("  DNS2: using 8.8.8.8");
          }
          
          if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
            Serial.println("❌ WiFi.config() failed, using DHCP");
            config.use_static_ip = false;
          } else {
            Serial.println("✅ IP configuration with DNS applied");
          }
        } else {
          Serial.println("❌ Error: Invalid IP/Gateway/Subnet, using DHCP");
          config.use_static_ip = false;
        }
      } else {
        Serial.println("❌ Error: Empty IP strings, using DHCP");
        config.use_static_ip = false;
      }
    } else {
      Serial.println("📡 Using DHCP");
    }
    
    WiFi.begin(config.ssid, config.password);    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi Connected!");
      Serial.print("📡 IP: "); Serial.println(WiFi.localIP());
      if (config.use_static_ip) {
        Serial.println("🔒 Mode: STATIC IP");
      } else {
        Serial.println("🌐 Mode: DHCP");
      }
      isAPMode = false;
      
      String hostname = String(config.hostname);
      if (hostname.length() == 0) hostname = "PlaatoKegHub";
      
      if (MDNS.begin(hostname.c_str())) {
        Serial.println("✅ mDNS started: http://" + hostname + ".local");
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.println("❌ Error starting mDNS");
      }
      
    } else {
      Serial.println("\n❌ Error connecting to WiFi");
      startAPMode();
    }
  } else {
    Serial.println("📡 No WiFi configuration - AP Mode");
    startAPMode();
  }
}

bool connectToWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA);
  
  WiFiConfig config;
  loadWiFiConfig(config);
  
  if (config.use_static_ip && strlen(config.static_ip) > 0) {
    IPAddress ip, gateway, subnet, dns1, dns2;
    
    if (ip.fromString(config.static_ip) && 
        gateway.fromString(config.gateway) && 
        subnet.fromString(config.subnet)) {
      
      Serial.println("🔧 Applying STATIC IP for connection:");
      Serial.print("  IP: "); Serial.println(config.static_ip);
      Serial.print("  Gateway: "); Serial.println(config.gateway);
      Serial.print("  Subnet: "); Serial.println(config.subnet);
      
      if (strlen(config.dns1) > 0) {
        dns1.fromString(config.dns1);
        Serial.print("  DNS1: "); Serial.println(config.dns1);
      } else {
        dns1 = gateway;
        Serial.println("  DNS1: using gateway");
      }
      
      if (strlen(config.dns2) > 0) {
        dns2.fromString(config.dns2);
        Serial.print("  DNS2: "); Serial.println(config.dns2);
      } else {
        dns2 = IPAddress(8,8,8,8);
        Serial.println("  DNS2: using 8.8.8.8");
      }
      
      WiFi.config(ip, gateway, subnet, dns1, dns2);
    }
  }
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected!");
    isAPMode = false;
    
    String hostname = String(config.hostname);
    if (hostname.length() == 0) hostname = "PlaatoKegHub";
    
    if (MDNS.begin(hostname.c_str())) {
      Serial.println("✅ mDNS started: http://" + hostname + ".local");
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("❌ Error starting mDNS");
    }
    
    return true;
  }
  
  Serial.println("\n❌ Connection error");
  return false;
}

void startAPMode() {
  isAPMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("PlaatoKegHUB", "diyhomebrewers7");
  Serial.println("📡 AP Mode started");
  Serial.print("📡 AP IP: "); Serial.println(WiFi.softAPIP());
  
  if (MDNS.begin("PlaatoKegHub")) {
    Serial.println("✅ mDNS started in AP mode: http://PlaatoKegHub.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("❌ Error starting mDNS in AP mode");
  }
}

String getConnectionStatus() {
  if (isAPMode) {
    return "AP Mode - SSID: PlaatoKegHUB - http://PlaatoKegHub.local";
  } else if (WiFi.status() == WL_CONNECTED) {
    WiFiConfig config;
    loadWiFiConfig(config);
    String hostname = String(config.hostname);
    if (hostname.length() == 0) hostname = "PlaatoKegHub";
    return "Connected to: " + String(WiFi.SSID()) + " - IP: " + WiFi.localIP().toString() + " - http://" + hostname + ".local";
  } else {
    return "Disconnected";
  }
}

IPAddress getIP() {
  if (isAPMode) {
    return WiFi.softAPIP();
  } else {
    return WiFi.localIP();
  }
}