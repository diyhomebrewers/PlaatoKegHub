#include "webserver.h"
#include "wifi_manager.h"
#include "config.h"
#include "keg_data.h"
#include <Arduino.h>
#include <WiFi.h>
#include "token_manager.h"
#include <WiFiClientSecure.h>  
#include <Update.h>
#include <SPIFFS.h>
#include "envios.h"

void sendPage(WiFiClient &client, String page, String content);
String buildMainTemplate(String currentPage, String content);
String getActiveClass(String currentPage, String page);
String urlDecode(String input);
String escapeJsonString(String input);
void handleEnvios(WiFiClient &client);
void handleSaveWebhookConfig(WiFiClient &client, String &request);
void handleSaveEnviosConfig(WiFiClient &client, String &request);
void handleTestWebhook(WiFiClient &client);
void handleWebhookStatus(WiFiClient &client);

extern bool sendCommandToKeg(uint16_t pin, String value);
extern void saveKegConfig(const KegConfig &config);

bool otaInProgress = false;
unsigned long lastOTAAck = 0;

WiFiServer webServer(80);

String escapeJsonString(String input) {
  String output = "";
  for (int i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') {
      output += "\\\"";
    } else if (c == '\\') {
      output += "\\\\";
    } else if (c == '\b') {
      output += "\\b";
    } else if (c == '\f') {
      output += "\\f";
    } else if (c == '\n') {
      output += "\\n";
    } else if (c == '\r') {
      output += "\\r";
    } else if (c == '\t') {
      output += "\\t";
    } else if (c < 32) {
      continue;
    } else {
      output += c;
    }
  }
  return output;
}

String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  for (int i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      temp[2] = input[i + 1];
      temp[3] = input[i + 2];
      decoded += char(strtol(temp, NULL, 16));
      i += 2;
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

String getActiveClass(String currentPage, String page) {
  return (currentPage == page) ? "active" : "";
}

String maskApiKey(String key) {
  if (key.length() <= 8) return "****";
  return key.substring(0, 4) + "..." + key.substring(key.length() - 4);
}

String formatTimestamp(unsigned long ts) {
  if (ts == 0) return "Never";
  
  unsigned long now = millis() / 1000;
  unsigned long seconds_ago = now - ts;
  
  if (seconds_ago < 60) {
    return String(seconds_ago) + " seconds ago";
  } else if (seconds_ago < 3600) {
    return String(seconds_ago / 60) + " minutes ago";
  } else {
    return String(seconds_ago / 3600) + " hours ago";
  }
}

String getNextSendTime(unsigned long last_send) {
  if (last_send == 0) return "Now (pending)";
  
  unsigned long now = millis() / 1000;
  unsigned long next = last_send + 300;
  
  if (now >= next) {
    return "Now (pending)";
  } else {
    unsigned long remaining = next - now;
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    return "In " + String(minutes) + "m " + String(seconds) + "s";
  }
}

String formatVolume(float value, bool isMetric, bool isVolumeMode) {
    if (!isVolumeMode) return "-";
    if (isMetric) {
        return String(value, 2);
    } else {
        return String(value * 0.264172, 2);
    }
}

String formatWeight(float value, bool isMetric) {
    if (isMetric) {
        return String(value, 2);
    } else {
        return String(value * 2.20462, 2);
    }
}

String formatTemp(float value, bool isMetric) {
    if (isMetric) {
        return String(value, 1);
    } else {
        return String(value * 1.8 + 32, 1);
    }
}

// main
const char MAIN_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Plaato Keg</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Arial, sans-serif; background: #f5f5f5; }
        .navbar { background: #2c3e50; color: white; padding: 1rem; }
        .navbar h1 { margin: 0; font-size: 1.5rem; }
        .nav-menu { background: #34495e; padding: 0.5rem; display: flex; flex-wrap: wrap; }
        .nav-menu a { color: white; text-decoration: none; padding: 0.5rem 1rem; margin: 0.2rem; border-radius: 4px; }
        .nav-menu a:hover { background: #2c3e50; }
        .nav-menu a.active { background: #e67e22; }
        .container { max-width: 1200px; margin: 2rem auto; padding: 0 1rem; }
        .card { background: white; padding: 2rem; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .info-box { background: #e8f4fd; padding: 1rem; border-radius: 4px; margin: 1rem 0; }
        .form-group { margin: 1rem 0; }
        .form-group label { display: block; margin-bottom: 0.5rem; font-weight: bold; }
        .form-group input, .form-group select { width: 100%; padding: 0.75rem; border: 1px solid #ddd; border-radius: 4px; }
        .checkbox-group { display: flex; align-items: center; gap: 10px; }
        .checkbox-group input { width: auto; }
        .btn { background: #e67e22; color: white; padding: 0.75rem 1.5rem; border: none; border-radius: 4px; cursor: pointer; }
        .btn:hover { background: #d35400; }
        .btn-small { background: #3498db; color: white; padding: 0.5rem 1rem; border: none; border-radius: 4px; cursor: pointer; margin: 0.5rem 0; }
        .btn-small:hover { background: #2980b9; }
        .btn-danger { background: #e74c3c; }
        .btn-danger:hover { background: #c0392b; }
        .status { padding: 0.5rem; background: #27ae60; color: white; border-radius: 4px; display: inline-block; }
        .ip-address { font-size: 1.2rem; background: #ecf0f1; padding: 0.5rem; border-radius: 4px; margin: 1rem 0; }
        .data-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 1rem; margin: 1rem 0; }
        .data-item { background: #f8f9fa; padding: 1rem; border-radius: 4px; border-left: 4px solid #e67e22; }
        .data-label { font-size: 0.9rem; color: #7f8c8d; }
        .data-value { font-size: 1.5rem; font-weight: bold; color: #2c3e50; }
        .data-unit { font-size: 0.9rem; color: #95a5a6; }
        .footer { text-align: center; margin-top: 2rem; color: #7f8c8d; }
        .refresh-info { display: flex; justify-content: space-between; align-items: center; margin: 1rem 0; }
        .last-update { color: #7f8c8d; font-size: 0.9rem; }
        .pouring-yes { color: #e74c3c; font-weight: bold; animation: blink 1s infinite; }
        .pouring-no { color: #27ae60; }
        @keyframes blink { 0%{opacity:1;} 50%{opacity:0.5;} 100%{opacity:1;} }
        .token-display { font-family: monospace; font-size: 1.2rem; background: #2c3e50; color: #fff; padding: 0.5rem; border-radius: 4px; word-break: break-all; }
        .warning { background: #fff3cd; color: #856404; padding: 0.75rem; border-radius: 4px; border-left: 4px solid #ffc107; margin: 1rem 0; }
        .success { background: #d4edda; color: #155724; padding: 0.75rem; border-radius: 4px; border-left: 4px solid #28a745; margin: 1rem 0; }
        .last-send { font-family: monospace; background: #f8f9fa; padding: 0.5rem; border-radius: 4px; margin: 0.5rem 0; }
    </style>
    <script>
        let autoRefresh = true;
        
        function toggleAutoRefresh() {
            autoRefresh = !autoRefresh;
            document.getElementById('autoRefreshBtn').innerText = autoRefresh ? '⏸️ Pause' : '▶️ Resume';
            if (autoRefresh) {
                refreshData();
            }
        }
        
        function refreshData() {
            fetch('/api/keg-data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('amount_left').innerText = data.amount_left;
                    document.getElementById('percent_left').innerText = data.percent_left;
                    document.getElementById('keg_temp').innerText = data.keg_temp;
                    document.getElementById('chip_temp').innerText = data.chip_temp;
                    document.getElementById('is_pouring').innerHTML = data.is_pouring ? 
                        '<span class="pouring-yes">🔴 POURING</span>' : 
                        '<span class="pouring-no">⚪ IDLE</span>';
                    document.getElementById('wifi_signal').innerText = data.wifi_signal;
                    document.getElementById('last_pour').innerText = data.last_pour;
                    document.getElementById('pressure').innerText = data.pressure;
                    document.getElementById('rssi').innerText = data.rssi;
                    document.getElementById('firmware').innerText = data.firmware;
                    
                    document.getElementById('volume_unit').innerText = data.volume_unit;
                    document.getElementById('weight_unit').innerText = data.weight_unit;
                    document.getElementById('temp_unit').innerText = data.temp_unit;
                    
                    if (data.v63) document.getElementById('v63').innerText = data.v63;
                    if (data.v69) document.getElementById('v69').innerText = data.v69;
                    if (data.v76) document.getElementById('v76').innerText = data.v76;
                    if (data.v87) document.getElementById('v87').innerText = data.v87;
                    
                    document.getElementById('lastUpdate').innerText = new Date().toLocaleTimeString();
                });
            
            if (autoRefresh) {
                setTimeout(refreshData, 2000);
            }
        }
        
        window.onload = function() {
            refreshData();
        };
    </script>
</head>
<body>
    <div class="navbar">
        <h1>🍩 Plaato-Keg-Hub</h1>
    </div>
    <div class="nav-menu">
        <a href="/" class="{CLASS_HOME}">Home</a>
        <a href="/wifi" class="{CLASS_WIFI}">WiFi</a>
        <a href="/token" class="{CLASS_TOKEN}">🔑 Token</a>
        <a href="/ajustes" class="{CLASS_AJUSTES}">Settings</a>
        <a href="/envios" class="{CLASS_ENVIOS}">📡 Sends</a>
        <a href="/acerca" class="{CLASS_ACERCA}">About</a>
    </div>
    <div class="container">
        <div class="card">
            <div class="ip-address">
                📡 IP: {IP_ADDRESS}
            </div>  
            {CONTENT}
        </div>
    </div>
        <div class="footer">
            <a href="https://diyhomebrewers.com/" target="_blank" style="color:#7f8c8d; text-decoration:none;">Plaato Keg Hub v1.1</a>
        </div>
</body>
</html>
)rawliteral";

// Keg data
const char HOME_CONTENT[] PROGMEM = R"rawliteral(
<div class="data-grid">
    <div class="data-item">
        <div class="data-label">🍺 {VOLUME_LABEL}</div>
        <div><span class="data-value" id="amount_left">{AMOUNT_LEFT}</span> <span class="data-unit" id="volume_unit">{VOLUME_UNIT}</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">📊 Percentage</div>
        <div><span class="data-value" id="percent_left">{PERCENT_LEFT}</span> <span class="data-unit">%</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">🌡️ Keg Temperature</div>
        <div><span class="data-value" id="keg_temp">{KEG_TEMP}</span> <span class="data-unit" id="temp_unit">{TEMP_UNIT}</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">🔥 Chip Temperature</div>
        <div><span class="data-value" id="chip_temp">{CHIP_TEMP}</span> <span class="data-unit">{TEMP_UNIT}</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">📶 WiFi Signal</div>
        <div><span class="data-value" id="wifi_signal">{WIFI_SIGNAL}</span> <span class="data-unit">%</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">📻 RSSI</div>
        <div><span class="data-value" id="rssi">{RSSI}</span> <span class="data-unit">dBm</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">🍻 Status</div>
        <div id="is_pouring">{IS_POURING}</div>
    </div>
    <div class="data-item">
        <div class="data-label">💧 Last Pour</div>
        <div><span class="data-value" id="last_pour">{LAST_POUR}</span> <span class="data-unit">{VOLUME_UNIT}</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">⚙️ Pressure</div>
        <div><span class="data-value" id="pressure">{PRESSURE}</span> <span class="data-unit">?</span></div>
    </div>
    <div class="data-item">
        <div class="data-label">📱 Firmware</div>
        <div><span class="data-value" id="firmware">{FIRMWARE}</span></div>
    </div>
</div>

<div class="data-grid" id="extra-data" style="display: {EXTRA_VISIBLE}">
    <div class="data-item" style="display: {V63_VISIBLE}">
        <div class="data-label">V63</div>
        <div><span class="data-value" id="v63">{V63}</span></div>
    </div>
    <div class="data-item" style="display: {V69_VISIBLE}">
        <div class="data-label">V69</div>
        <div><span class="data-value" id="v69">{V69}</span></div>
    </div>
    <div class="data-item" style="display: {V76_VISIBLE}">
        <div class="data-label">V76 (Max Volume)</div>
        <div><span class="data-value" id="v76">{V76}</span> <span class="data-unit">{VOLUME_UNIT}</span></div>
    </div>
    <div class="data-item" style="display: {V87_VISIBLE}">
        <div class="data-label">V87</div>
        <div><span class="data-value" id="v87">{V87}</span></div>
    </div>
</div>

<div class="info-box">
    <h3>System Information</h3>
    <p><strong>Mode:</strong> {MODE}</p>
    <p><strong>System:</strong> {UNIT_SYSTEM}</p>
    <p><strong>SSID:</strong> {SSID}</p>
    <p><strong>Keg connected:</strong> {KEG_CONNECTED}</p>
    <p><strong>Webhook Status:</strong> {WEBHOOK_STATUS}</p>
    <p><strong>Last Send:</strong> {LAST_SEND}</p>
</div>
)rawliteral";

// WiFi
const char WIFI_CONTENT[] PROGMEM = R"rawliteral(
<h2>WiFi Configuration</h2>
<div class="info-box" id="modeInfo">{MODE_INFO}</div>

<form action="/save-wifi" method="post" accept-charset="UTF-8">
    <div class="form-group">
        <label for="hostname">Device hostname:</label>
        <input type="text" id="hostname" name="hostname" placeholder="Ex: PlaatoKegHub" value="{HOSTNAME}">
    </div>
    <div class="form-group">
        <label for="ssid">WiFi SSID:</label>
        <input type="text" id="ssid" name="ssid" placeholder="Your WiFi name" value="{SSID}">
    </div>
    <div class="form-group">
        <label for="password">Password:</label>
        <input type="password" id="password" name="password" placeholder="Password" value="{PASSWORD}">
    </div>
    
    <div class="info-box" style="background: #e8f4fd; margin-top: 20px;">
        <h3>🔧 Static IP Configuration (optional)</h3>
        <div class="checkbox-group">
            <input type="checkbox" id="use_static_ip" name="use_static_ip" {USE_STATIC_IP_CHECKED}>
            <label for="use_static_ip">Use static IP (if unchecked, DHCP will be used)</label>
        </div>
        
        <div id="static_ip_fields" style="display: {STATIC_IP_DISPLAY}; margin-top: 15px;">
            <div class="form-group">
                <label for="static_ip">Static IP address:</label>
                <input type="text" id="static_ip" name="static_ip" placeholder="Ex: 192.168.1.100" value="{STATIC_IP}" pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$">
                <small>IP address for this device on your network</small>
            </div>
            <div class="form-group">
                <label for="gateway">Gateway:</label>
                <input type="text" id="gateway" name="gateway" placeholder="Ex: 192.168.1.1" value="{GATEWAY}" pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$">
                <small>Usually your router's IP</small>
            </div>
            <div class="form-group">
                <label for="subnet">Subnet mask:</label>
                <input type="text" id="subnet" name="subnet" placeholder="Ex: 255.255.255.0" value="{SUBNET}" pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$">
            </div>
            <div class="form-group">
                <label for="dns1">Primary DNS (optional):</label>
                <input type="text" id="dns1" name="dns1" placeholder="Ex: 8.8.8.8" value="{DNS1}" pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$">
            </div>
            <div class="form-group">
                <label for="dns2">Secondary DNS (optional):</label>
                <input type="text" id="dns2" name="dns2" placeholder="Ex: 8.8.4.4" value="{DNS2}" pattern="^([0-9]{1,3}\.){3}[0-9]{1,3}$">
            </div>
        </div>
    </div>
    
    <button type="submit" class="btn">Save and Connect</button>
</form>

<script>
function toggleStaticIpFields() {
    const checkbox = document.getElementById('use_static_ip');
    const fields = document.getElementById('static_ip_fields');
    fields.style.display = checkbox.checked ? 'block' : 'none';
}

document.getElementById('use_static_ip').addEventListener('change', toggleStaticIpFields);
window.onload = function() {
    toggleStaticIpFields();
};
</script>
)rawliteral";

// Token
const char TOKEN_CONTENT[] PROGMEM = R"rawliteral(
<h2>🔑 Plaato Token Configuration</h2>

<div class="info-box">
    <h3>What is this?</h3>
    <p>The token is a 16-byte key (32 hexadecimal characters) that identifies your Plaato Keg device. 
    Only devices with this token will be able to connect to this hub.</p>
    <p class="warning"><strong>⚠️ Important:</strong> If you change the token, your current Plaato Keg will stop connecting unless it has the new token.</p>
</div>

<div class="info-box">
    <h3>📋 Current Token</h3>
    <p class="token-display">{CURRENT_TOKEN}</p>
    <p><small>Format: 32 hexadecimal characters</small></p>
</div>

<form action="/save-token" method="post" accept-charset="UTF-8">
    <div class="form-group">
        <label for="token">New Token (32 hex characters):</label>
        <input type="text" id="token" name="token" pattern="[A-Fa-f0-9]{32}" maxlength="32" 
               placeholder="1A2B3C4D5E6F7A8B9C0D1E2F3A4B5C6D" value="{CURRENT_TOKEN}" required>
        <small>Only hexadecimal characters (0-9, A-F), exactly 32 characters</small>
    </div>
    
    <div class="form-group">
        <label for="confirm_token">Confirm Token:</label>
        <input type="text" id="confirm_token" name="confirm_token" pattern="[A-Fa-f0-9]{32}" maxlength="32" 
               placeholder="Repeat the token" required>
    </div>
    
    <button type="submit" class="btn">Save Token</button>
</form>

<form action="/reset-token" method="post" style="margin-top: 20px;">
    <button type="submit" class="btn btn-danger" onclick="return confirm('⚠️ Restore default token? The current Keg will stop working if it doesn\'t use that token.')">Restore Default Token</button>
</form>

<div class="info-box">
    <h3>🔧 Tools</h3>
    <p><strong>Default token:</strong> 1A2B3C4D5E6F7A8B9C0D1E2F3A4B5C6D</p>
</div>

<script>
document.querySelector('form[action="/save-token"]').onsubmit = function(e) {
    const token = document.getElementById('token').value;
    const confirm = document.getElementById('confirm_token').value;
    
    if (token !== confirm) {
        alert('❌ Tokens do not match');
        return false;
    }
    
    if (!/^[0-9A-Fa-f]{32}$/.test(token)) {
        alert('❌ Token must have exactly 32 hexadecimal characters');
        return false;
    }
    
    return confirm('⚠️ Change the token? Make sure your Plaato Keg has the new token.');
};
</script>
)rawliteral";

// SENDS ==========
const char ENVIOS_CONTENT[] PROGMEM = R"rawliteral(
<h2>📤 Send Configuration</h2>

<!-- YOUR ORIGINAL DH CLOUD SECTION - INTACT -->
<div class="info-box">
    <h3>☁️ DIYHOMEBREWERS CLOUD</h3>
    <p><strong>Status:</strong> <span id="webhookStatus">{STATUS_BADGE}</span></p>
    <p><strong>Device Name:</strong> <span id="currentDeviceName">{CURRENT_DEVICE_NAME}</span></p>
    <p><strong>API Key:</strong> <span class="token-display" style="font-size: 0.7rem;">{CURRENT_API_KEY}</span></p>
    <p><strong>Last Send:</strong> <span id="lastSendTime">{LAST_SEND}</span></p>
    <p><strong>Next Send:</strong> <span id="nextSendTime">{NEXT_SEND}</span></p>
    
    <form id="webhookForm" onsubmit="saveWebhookConfig(event)">
        <div class="checkbox-group">
            <input type="checkbox" id="enabled" name="enabled" {ENABLED_CHECKED}>
            <label for="enabled">Enable automatic sending</label>
        </div>
        
        <div class="form-group">
            <label for="device_name">Device Name:</label>
            <input type="text" id="device_name" name="device_name" placeholder="Ex: MainKeg" value="{DEVICE_NAME}" required>
            <small>This name must exactly match the name of the API key in DiyHomebrewers CLOUD</small>
        </div>
        
        <div class="form-group">
            <label for="api_key">API Key:</label>
            <input type="text" id="api_key" name="api_key" placeholder="Your WordPress API key" value="{API_KEY}" required>
            <small>The API key that appears in the URL (after ?api_key=)</small>
        </div>
        
        <button type="submit" class="btn-small">Save Cloud</button>
        <button type="button" onclick="testWebhook()" class="btn-small">Test Send</button>
    </form>
</div>

<!-- NEW DESTINATIONS - WITH CORRECTED NAMES -->
<div class="info-box">
    <h3>📡 MQTT</h3>
    <form id="mqttForm" onsubmit="saveMQTTConfig(event)">
        <div class="checkbox-group">
            <input type="checkbox" id="mqtt_enabled" name="mqtt_enabled" {MQTT_ENABLED}>
            <label for="mqtt_enabled">Enable MQTT sending</label>
        </div>
        
        <div class="form-group">
            <label for="mqtt_server">MQTT Server:</label>
            <input type="text" id="mqtt_server" name="mqtt_server" value="{MQTT_SERVER}">
        </div>
        
        <div class="form-group">
            <label for="mqtt_port">Port:</label>
            <input type="number" id="mqtt_port" name="mqtt_port" value="{MQTT_PORT}">
        </div>
        
        <div class="form-group">
            <label for="mqtt_topic">Topic:</label>
            <input type="text" id="mqtt_topic" name="mqtt_topic" value="{MQTT_TOPIC}">
        </div>
        
        <div class="form-group">
            <label for="mqtt_username">MQTT Username</label>
            <input type="text" id="mqtt_username" name="mqtt_username" value="{MQTT_USER}">
        </div>
        
        <div class="form-group">
            <label for="mqtt_password">MQTT Password</label>
            <input type="password" id="mqtt_password" name="mqtt_password" value="{MQTT_PASS}">
        </div>
        
        <button type="submit" class="btn-small">Save MQTT</button>
    </form>
</div>

<div class="info-box">
    <h3>🍺 Brewfather</h3>
    <form id="brewfatherForm" onsubmit="saveBrewfatherConfig(event)">
        <div class="checkbox-group">
            <input type="checkbox" id="brewfather_enabled" name="brewfather_enabled" {BREWFATHER_ENABLED}>
            <label for="brewfather_enabled">Enable Brewfather sending</label>
        </div>
        
        <div class="form-group">
            <label for="brewfather_device">Device Name:</label>
            <input type="text" id="brewfather_device" name="brewfather_device" value="{BREWFATHER_DEVICE}">
        </div>
        
        <div class="form-group">
            <label for="brewfather_id">Personal ID:</label>
            <input type="text" id="brewfather_id" name="brewfather_id" value="{BREWFATHER_ID}">
            <small>Find it at: https://web.brewfather.app/tabs/settings/stream</small>
        </div>
        
        <button type="submit" class="btn-small">Save Brewfather</button>
    </form>
</div>

<div class="info-box">
    <h3>🍻 Taplist.io</h3>
    <form id="taplistForm" onsubmit="saveTaplistConfig(event)">
        <div class="checkbox-group">
            <input type="checkbox" id="taplist_enabled" name="taplist_enabled" {TAPLIST_ENABLED}>
            <label for="taplist_enabled">Enable Taplist.io sending</label>
        </div>
        
        <div class="form-group">
            <label for="taplist_venue">Venue ID:</label>
            <input type="text" id="taplist_venue" name="taplist_venue" value="{TAPLIST_VENUE}">
        </div>
        
        <div class="form-group">
            <label for="taplist_tap">Tap Number:</label>
            <input type="text" id="taplist_tap" name="taplist_tap" value="{TAPLIST_TAP}">
        </div>
        
        <div class="form-group">
            <label for="taplist_token">API Token:</label>
            <input type="text" id="taplist_token" name="taplist_token" value="{TAPLIST_TOKEN}">
        </div>
        
        <button type="submit" class="btn-small">Save Taplist.io</button>
    </form>
</div>

<div class="info-box">
    <h3>🌐 Generic HTTP</h3>
    <form id="httpForm" onsubmit="saveHTTPConfig(event)">
        <div class="checkbox-group">
            <input type="checkbox" id="http_enabled" name="http_enabled" {HTTP_ENABLED}>
            <label for="http_enabled">Enable HTTP sending</label>
        </div>
        
        <div class="form-group">
            <label for="http_url">Full URL:</label>
            <input type="text" id="http_url" name="http_url" value="{HTTP_URL}">
            <small>Ex: http://192.168.1.100:8080/api/data or https://example.com/api</small>
        </div>
        
        <div class="form-group">
            <label for="http_apikey">API Key (optional):</label>
            <input type="text" id="http_apikey" name="http_apikey" value="{HTTP_KEY}">
        </div>
        
        <button type="submit" class="btn-small">Save HTTP</button>
    </form>
</div>

<script>
// YOUR ORIGINAL WEBHOOK FUNCTION
function saveWebhookConfig(event) {
    event.preventDefault();
    
    const config = {
        enabled: document.getElementById('enabled').checked,
        device_name: document.getElementById('device_name').value,
        api_key: document.getElementById('api_key').value
    };
    
    fetch('/api/save-webhook-config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            alert('✅ Configuration saved successfully');
            location.reload();
        } else {
            alert('❌ Error: ' + data.message);
        }
    });
}

function testWebhook() {
    fetch('/api/test-webhook')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                alert('✅ Test successful');
            } else {
                alert('❌ Error: ' + data.message);
            }
        });
}

// CORRECTED FUNCTIONS FOR NEW DESTINATIONS
function saveMQTTConfig(event) {
    event.preventDefault();
    const config = {
        type: 'mqtt',
        enabled: document.getElementById('mqtt_enabled').checked,
        server: document.getElementById('mqtt_server').value,
        port: parseInt(document.getElementById('mqtt_port').value),
        topic: document.getElementById('mqtt_topic').value,
        user: document.getElementById('mqtt_username').value,
        pass: document.getElementById('mqtt_password').value
    };
    saveEnviosConfig(config);
}

function saveBrewfatherConfig(event) {
    event.preventDefault();
    const config = {
        type: 'brewfather',
        enabled: document.getElementById('brewfather_enabled').checked,
        device_name: document.getElementById('brewfather_device').value,
        personal_id: document.getElementById('brewfather_id').value
    };
    saveEnviosConfig(config);
}

function saveTaplistConfig(event) {
    event.preventDefault();
    const config = {
        type: 'taplist',
        enabled: document.getElementById('taplist_enabled').checked,
        venue_id: document.getElementById('taplist_venue').value,
        tap_number: document.getElementById('taplist_tap').value,
        api_token: document.getElementById('taplist_token').value
    };
    saveEnviosConfig(config);
}

function saveHTTPConfig(event) {
    event.preventDefault();
    const config = {
        type: 'http',
        enabled: document.getElementById('http_enabled').checked,
        url: document.getElementById('http_url').value,
        api_key: document.getElementById('http_apikey').value
    };
    saveEnviosConfig(config);
}

function saveEnviosConfig(config) {
    fetch('/api/save-envios-config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            alert('✅ Configuration saved');
        } else {
            alert('❌ Error: ' + data.message);
        }
    });
}

// Update times every minute
setInterval(() => {
    fetch('/api/webhook-status')
        .then(response => response.json())
        .then(data => {
            if (data.enabled) {
                document.getElementById('webhookStatus').innerHTML = '<span class="status">✅ ACTIVE</span>';
            } else {
                document.getElementById('webhookStatus').innerHTML = '<span class="status" style="background:#95a5a6;">⭕ INACTIVE</span>';
            }
            document.getElementById('lastSendTime').innerText = data.last_send_formatted;
            document.getElementById('nextSendTime').innerText = data.next_send_formatted;
        });
}, 60000);
</script>
)rawliteral";

// Settings
const char AJUSTES_CONTENT[] PROGMEM = R"rawliteral(
<h2>Keg Settings</h2>

<div class="info-box">
    <h3>📊 Current Configuration</h3>
    <p><strong>Tare (Empty weight):</strong> <span id="current_tara">{CURRENT_TARA}</span> <span id="current_weight_unit">{WEIGHT_UNIT}</span></p>
    <p><strong>Maximum Volume:</strong> <span id="current_max_volume">{CURRENT_MAX_VOLUME}</span> <span id="current_volume_unit">{VOLUME_UNIT}</span></p>
    <p><strong>Calibration Factor:</strong> <span id="current_calibration">{CURRENT_CALIBRATION}</span></p>
    <p><strong>Temperature Offset:</strong> <span id="current_temp_offset">{CURRENT_TEMP_OFFSET}</span> <span id="current_temp_unit">{TEMP_UNIT}</span></p>
    <p><strong>Sensitivity:</strong> <span id="current_sensitivity">{CURRENT_SENSITIVITY}</span> (0-20)</p>
    <p><strong>Unit System:</strong> <span id="current_units">{CURRENT_UNITS}</span></p>
    <p><strong>Mode:</strong> <span id="current_mode">{CURRENT_MODE}</span></p>
    <p><strong>Min Temp Alarm:</strong> <span id="current_min_alarm">{CURRENT_MIN_ALARM}</span> <span id="current_temp_unit2">{TEMP_UNIT}</span></p>
    <p><strong>Max Temp Alarm:</strong> <span id="current_max_alarm">{CURRENT_MAX_ALARM}</span> <span id="current_temp_unit3">{TEMP_UNIT}</span></p>
</div>

<div class="info-box">
    <h3>📝 Send Configuration to Keg</h3>
    <p class="note">Changes will be sent immediately to the Keg</p>
    
    <form id="kegConfigForm">
        <div class="form-group">
            <label for="tara">Tare (Empty weight) - V51:</label>
            <input type="number" step="0.1" id="tara" name="tara" placeholder="Ex: 2.5" value="{CURRENT_TARA}">
            <small>Empty keg weight in <span id="weight_unit_small">{WEIGHT_UNIT}</span></small>
        </div>
        
        <div class="form-group">
            <label for="max_volume">Maximum Volume - V76:</label>
            <input type="number" step="0.1" id="max_volume" name="max_volume" placeholder="Ex: 19.8" value="{CURRENT_MAX_VOLUME}">
            <small>Total capacity in <span id="volume_unit_small">{VOLUME_UNIT}</span></small>
        </div>
        
        <div class="form-group">
            <label for="calibration">Calibration Factor:</label>
            <input type="number" step="0.01" id="calibration" name="calibration" placeholder="Ex: 1.0" value="{CURRENT_CALIBRATION}">
            <small>Precision adjustment</small>
        </div>
        
        <div class="form-group">
            <label for="temp_offset">Temperature Offset:</label>
            <input type="number" step="0.1" id="temp_offset" name="temp_offset" placeholder="Ex: 0.0" value="{CURRENT_TEMP_OFFSET}">
            <small>Temperature correction in <span id="temp_unit_small">{TEMP_UNIT}</span></small>
        </div>
        
        <div class="form-group">
            <label for="sensitivity">Sensitivity:</label>
            <input type="number" min="0" max="20" id="sensitivity" name="sensitivity" placeholder="Ex: 10" value="{CURRENT_SENSITIVITY}">
            <small>Pour detection level (0-20)</small>
        </div>
        
        <div class="form-group">
            <label for="unit_system">Unit System:</label>
            <select id="unit_system" name="unit_system" onchange="updateUnitLabels()">
                <option value="1" {UNIT_METRIC_SELECTED}>Metric (L, kg, °C)</option>
                <option value="0" {UNIT_IMPERIAL_SELECTED}>Imperial (gal, lb, °F)</option>
            </select>
        </div>
        
        <div class="form-group">
            <label for="display_mode">Display Mode:</label>
            <select id="display_mode" name="display_mode">
                <option value="2" {MODE_VOLUME_SELECTED}>Volume</option>
                <option value="1" {MODE_WEIGHT_SELECTED}>Weight</option>
            </select>
        </div>
        
        <div class="form-group">
            <label for="min_alarm">Minimum Temperature Alarm:</label>
            <input type="number" step="0.1" id="min_alarm" name="min_alarm" placeholder="Ex: 0.0" value="{CURRENT_MIN_ALARM}">
        </div>
        
        <div class="form-group">
            <label for="max_alarm">Maximum Temperature Alarm:</label>
            <input type="number" step="0.1" id="max_alarm" name="max_alarm" placeholder="Ex: 30.0" value="{CURRENT_MAX_ALARM}">
        </div>
        
        <button type="button" onclick="sendKegConfig()" class="btn">Send Configuration to Keg</button>
    </form>
    
    <div id="configResult" style="margin-top: 1rem; padding: 1rem; border-radius: 4px; display: none;"></div>
</div>

<script>
let currentUnitSystem = {UNIT_SYSTEM_NUM};
let currentDisplayMode = {DISPLAY_MODE_NUM};

function updateUnitLabels() {
    const unitSystem = document.getElementById('unit_system').value;
    const isMetric = (unitSystem == '1');
    
    const weightUnit = isMetric ? 'kg' : 'lb';
    const volumeUnit = isMetric ? 'L' : 'gal';
    const tempUnit = isMetric ? '°C' : '°F';
    
    const weightSpans = document.querySelectorAll('#weight_unit_small, #current_weight_unit');
    weightSpans.forEach(span => { if(span) span.innerText = weightUnit; });
    
    const volumeSpans = document.querySelectorAll('#volume_unit_small, #current_volume_unit');
    volumeSpans.forEach(span => { if(span) span.innerText = volumeUnit; });
    
    const tempSpans = document.querySelectorAll('#temp_unit_small, #current_temp_unit, #current_temp_unit2, #current_temp_unit3');
    tempSpans.forEach(span => { if(span) span.innerText = tempUnit; });
}

function sendKegConfig() {
    let tara = document.getElementById('tara').value;
    let max_volume = document.getElementById('max_volume').value;
    let calibration = document.getElementById('calibration').value;
    let temp_offset = document.getElementById('temp_offset').value;
    let sensitivity = document.getElementById('sensitivity').value;
    let unit_system = document.getElementById('unit_system').value;
    let display_mode = document.getElementById('display_mode').value;
    let min_alarm = document.getElementById('min_alarm').value;
    let max_alarm = document.getElementById('max_alarm').value;
    
    if (parseFloat(tara) <= 0) {
        alert('❌ Tare must be greater than 0');
        return;
    }
    if (parseFloat(max_volume) <= 0) {
        alert('❌ Maximum volume must be greater than 0');
        return;
    }
    
    const config = {
        tara: tara,
        max_volume: max_volume,
        calibration: calibration,
        temp_offset: temp_offset,
        sensitivity: sensitivity,
        unit_system: unit_system,
        display_mode: display_mode,
        min_alarm: min_alarm,
        max_alarm: max_alarm
    };
    
    fetch('/api/send-keg-config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    })
    .then(response => response.json())
    .then(data => {
        const resultDiv = document.getElementById('configResult');
        resultDiv.style.display = 'block';
        if (data.success) {
            resultDiv.className = 'info-box';
            resultDiv.style.background = '#d4edda';
            resultDiv.style.color = '#155724';
            resultDiv.innerHTML = '✅ Configuration sent successfully';
            
            setTimeout(() => {
                fetch('/api/keg-config-status')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('current_tara').innerText = data.empty_keg_weight;
                        document.getElementById('current_max_volume').innerText = data.max_keg_volume;
                        document.getElementById('current_calibration').innerText = data.calibration_factor;
                        document.getElementById('current_temp_offset').innerText = data.temperature_offset;
                        document.getElementById('current_sensitivity').innerText = data.sensitivity;
                        document.getElementById('current_units').innerText = data.unit_system == 1 ? 'Metric' : 'Imperial';
                        document.getElementById('current_mode').innerText = data.display_mode == 2 ? 'Volume' : 'Weight';
                        document.getElementById('current_min_alarm').innerText = data.min_temperature_alarm;
                        document.getElementById('current_max_alarm').innerText = data.max_temperature_alarm;
                    });
            }, 1000);
        } else {
            resultDiv.className = 'info-box';
            resultDiv.style.background = '#f8d7da';
            resultDiv.style.color = '#721c24';
            resultDiv.innerHTML = '❌ Error: ' + data.message;
        }
        setTimeout(() => { resultDiv.style.display = 'none'; }, 3000);
    });
}

setInterval(() => {
    fetch('/api/keg-config-status')
        .then(response => response.json())
        .then(data => {
            document.getElementById('current_tara').innerText = data.empty_keg_weight;
            document.getElementById('current_max_volume').innerText = data.max_keg_volume;
            document.getElementById('current_calibration').innerText = data.calibration_factor;
            document.getElementById('current_temp_offset').innerText = data.temperature_offset;
            document.getElementById('current_sensitivity').innerText = data.sensitivity;
            document.getElementById('current_units').innerText = data.unit_system == 1 ? 'Metric' : 'Imperial';
            document.getElementById('current_mode').innerText = data.display_mode == 2 ? 'Volume' : 'Weight';
            document.getElementById('current_min_alarm').innerText = data.min_temperature_alarm;
            document.getElementById('current_max_alarm').innerText = data.max_temperature_alarm;
        });
}, 5000);
</script>
)rawliteral";

// About + OTA
const char ACERCA_CONTENT[] PROGMEM = R"rawliteral(
<h2>About</h2>
<div class="info-box">
    <p><strong>Project:</strong> Plaato Keg Hub</p>
    <p><strong>Version:</strong> 1.1</p>
    <p><strong>Description:</strong> Hub for Plaato Keg with ESP32</p>
    <p><strong>Author:</strong> DIY Homebrewers</p>
    <p><strong>Website:</strong> <a href="https://diyhomebrewers.com/" target="_blank">https://diyhomebrewers.com/</a></p>  
    <p><strong>Chip:</strong> ESP32-C3</p>
</div>

<div class="info-box" style="background: #fff3cd; border-left: 4px solid #ffc107;">
    <h3>🔄 Firmware Update (OTA)</h3>
    <div class="form-group">
        <input type="file" id="fwFile" accept=".bin"
               style="padding: 10px; border: 2px dashed #ccc; border-radius: 5px; width: 100%;">
    </div>
    <button onclick="uploadFW()" class="btn" style="background: #28a745; width: 100%;">
        📤 Upload and Update
    </button>
    <div id="status" style="margin-top: 15px; font-weight: bold; text-align: center;"></div>
</div>

<script>
function uploadFW() {
    const file = document.getElementById('fwFile').files[0];
    if (!file) { alert('Select a .bin file'); return; }
    
    const status = document.getElementById('status');
    status.innerHTML = '<span style="color:orange;">⏳ Uploading... DO NOT CLOSE THE PAGE</span>';
    
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update', true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Filename', file.name);
    
    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            const pct = Math.round(e.loaded * 100 / e.total);
            status.innerHTML = '<span style="color:orange;">⏳ ' + pct + '% uploaded...</span>';
        }
    };
    
    xhr.onload = function() {
        if (xhr.status === 200) {
            document.open(); document.write(xhr.responseText); document.close();
        } else {
            status.innerHTML = '<span style="color:red;">❌ Error: ' + xhr.status + '</span>';
        }
    };
    
    xhr.onerror = function() {
        status.innerHTML = '<span style="color:red;">❌ Connection error</span>';
    };
    
    xhr.send(file);
}
</script>
)rawliteral";

// WiFi result
const char RESULT_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Configuring...</title>
    <style>
        body { font-family: Arial; margin:20px; background:#f0f0f0; text-align:center; padding-top:50px; }
        .card { background:white; padding:20px; border-radius:10px; max-width:400px; margin:auto; }
        h1 { color:green; }
        .info { text-align: left; margin-top: 20px; }
    </style>
    <meta http-equiv="refresh" content="15">
</head>
<body>
    <div class="card">
        <h1>✅ Configuration saved</h1>
        <p>Restarting...</p>
        <div class="info">
            <p><strong>Hostname:</strong> {HOSTNAME}</p>
            <p><strong>SSID:</strong> {SSID}</p>
            {EXTRA_INFO}
        </div>
    </div>
</body>
</html>
)rawliteral";

// token result
const char TOKEN_RESULT_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Token Saved</title>
    <style>
        body { font-family: Arial; margin:20px; background:#f0f0f0; text-align:center; padding-top:50px; }
        .card { background:white; padding:20px; border-radius:10px; max-width:500px; margin:auto; }
        h1 { color:green; }
        .token { font-family: monospace; font-size:1.2rem; background:#f0f0f0; padding:10px; border-radius:4px; }
    </style>
    <meta http-equiv="refresh" content="10">
</head>
<body>
    <div class="card">
        <h1>✅ Token saved</h1>
        <p>New token:</p>
        <p class="token">{TOKEN}</p>
        <p>Restarting in 10 seconds...</p>
    </div>
</body>
</html>
)rawliteral";

String buildMainTemplate(String currentPage, String content) {
  String html = String(MAIN_TEMPLATE);
  
  html.replace("{CLASS_HOME}", getActiveClass(currentPage, "home"));
  html.replace("{CLASS_WIFI}", getActiveClass(currentPage, "wifi"));
  html.replace("{CLASS_TOKEN}", getActiveClass(currentPage, "token"));
  html.replace("{CLASS_AJUSTES}", getActiveClass(currentPage, "ajustes"));
  html.replace("{CLASS_ENVIOS}", getActiveClass(currentPage, "envios"));
  html.replace("{CLASS_ACERCA}", getActiveClass(currentPage, "acerca"));
  
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  html.replace("{IP_ADDRESS}", ip);
  
  html.replace("{CONTENT}", content);
  
  return html;
}

void sendPage(WiFiClient &client, String page, String content) {
  String html = buildMainTemplate(page, content);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(html);
}

void handleEnvios(WiFiClient &client) {
    EnviosConfig envios;
    loadEnviosConfig(envios);
    
    String content = String(ENVIOS_CONTENT);
    
    String deviceName = String(keg.webhook.device_name);
    deviceName.replace("\"", "");
    deviceName.trim();
    
    String apiKey = String(keg.webhook.api_key);
    apiKey.replace("\"", "");
    apiKey.trim();
    
    String statusBadge = keg.webhook.enabled ? 
        "<span class='status'>✅ ACTIVE</span>" : 
        "<span class='status' style='background:#95a5a6;'>⭕ INACTIVE</span>";
    
    String enabledChecked = keg.webhook.enabled ? "checked" : "";
    String lastSendFormatted = formatTimestamp(keg.webhook.last_send);
    String nextSendFormatted = getNextSendTime(keg.webhook.last_send);
    
    content.replace("{STATUS_BADGE}", statusBadge);
    content.replace("{CURRENT_DEVICE_NAME}", deviceName);
    content.replace("{CURRENT_API_KEY}", apiKey);
    content.replace("{LAST_SEND}", lastSendFormatted);
    content.replace("{NEXT_SEND}", nextSendFormatted);
    content.replace("{ENABLED_CHECKED}", enabledChecked);
    content.replace("{DEVICE_NAME}", deviceName);
    content.replace("{API_KEY}", apiKey);
    content.replace("{MQTT_ENABLED}", envios.mqtt.enabled ? "checked" : "");
    content.replace("{MQTT_SERVER}", String(envios.mqtt.server));
    content.replace("{MQTT_PORT}", String(envios.mqtt.port));
    content.replace("{MQTT_TOPIC}", String(envios.mqtt.topic));
    content.replace("{MQTT_USER}", String(envios.mqtt.user));
    content.replace("{MQTT_PASS}", String(envios.mqtt.pass));
    content.replace("{BREWFATHER_ENABLED}", envios.brewfather.enabled ? "checked" : "");
    content.replace("{BREWFATHER_DEVICE}", String(envios.brewfather.device_name));
    content.replace("{BREWFATHER_ID}", String(envios.brewfather.personal_id));
    content.replace("{TAPLIST_ENABLED}", envios.taplist.enabled ? "checked" : "");
    content.replace("{TAPLIST_VENUE}", String(envios.taplist.venue_id));
    content.replace("{TAPLIST_TAP}", String(envios.taplist.tap_number));
    content.replace("{TAPLIST_TOKEN}", String(envios.taplist.api_token));
    content.replace("{HTTP_ENABLED}", envios.http_generic.enabled ? "checked" : "");
    content.replace("{HTTP_URL}", String(envios.http_generic.url));
    content.replace("{HTTP_KEY}", String(envios.http_generic.api_key));
    sendPage(client, "envios", content);
}

void handleSaveWebhookConfig(WiFiClient &client, String &request) {
  Serial.println("=== SAVING WEBHOOK CONFIGURATION ===");
  String jsonResponse = "{\"success\":false,\"message\":\"Error\"}";
  
  int bodyStart = request.indexOf("\r\n\r\n") + 4;
  if (bodyStart > 4) {
    String body = request.substring(bodyStart);
    Serial.print("📦 JSON received: ");
    Serial.println(body);
    
    bool enabled = false;
    String device_name = "";
    String api_key = "";
    
    if (body.indexOf("\"enabled\":true") >= 0) {
      enabled = true;
    }
    
    int namePos = body.indexOf("\"device_name\":\"");
    if (namePos >= 0) {
      int nameStart = namePos + 15;
      int nameEnd = body.indexOf("\"", nameStart);
      if (nameEnd > nameStart) {
        device_name = body.substring(nameStart, nameEnd);
        device_name.replace("\\\"", "");
        device_name.replace("\"", "");
        device_name.trim();
      }
    }
    
    int keyPos = body.indexOf("\"api_key\":\"");
    if (keyPos >= 0) {
      int keyStart = keyPos + 11;
      int keyEnd = body.indexOf("\"", keyStart);
      if (keyEnd > keyStart) {
        api_key = body.substring(keyStart, keyEnd);
        api_key.replace("\\\"", "");
        api_key.replace("\"", "");
        api_key.trim();
      }
    }
    
    keg.webhook.enabled = enabled;
    keg.webhook.last_send = 0;
    
    memset(keg.webhook.device_name, 0, sizeof(keg.webhook.device_name));
    memset(keg.webhook.api_key, 0, sizeof(keg.webhook.api_key));
    
    if (device_name.length() > 0) {
      strncpy(keg.webhook.device_name, device_name.c_str(), 32);
      keg.webhook.device_name[32] = '\0';
    }
    
    if (api_key.length() > 0) {
      strncpy(keg.webhook.api_key, api_key.c_str(), 64);
      keg.webhook.api_key[64] = '\0';
    }
    
    saveWebhookConfig(keg.webhook);
    
    jsonResponse = "{\"success\":true,\"message\":\"Configuration saved\"}";
  }
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonResponse);
}

void handleTestWebhook(WiFiClient &client) {
  Serial.println("=== TEST WEBHOOK ===");
  String jsonResponse;
  
  if (!keg.webhook.enabled) {
    jsonResponse = "{\"success\":false,\"message\":\"Webhook is not enabled\"}";
  } else if (strlen(keg.webhook.api_key) == 0 || strlen(keg.webhook.device_name) == 0) {
    jsonResponse = "{\"success\":false,\"message\":\"Missing API Key or Device Name\"}";
  } else {
    String deviceName = String(keg.webhook.device_name);
    deviceName.replace("\"", "");
    deviceName.trim();
    
    String apiKey = String(keg.webhook.api_key);
    apiKey.replace("\"", "");
    apiKey.trim();
    
    String url = "https://diyhomebrewers.com/wp-json/trk/v1/barrel/dh?api_key=" + apiKey;
    
    String payload = "{";
    payload += "\"name\":\"" + escapeJsonString(deviceName) + "\",";
    payload += "\"temperature\":" + String(keg.keg_temperature, 1) + ",";
    payload += "\"volume\":" + String(keg.amount_left, 2);
    payload += "}";
    
    WiFiClientSecure clientSecure;
    clientSecure.setInsecure();
    
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
          clientSecure.stop();
          jsonResponse = "{\"success\":false,\"message\":\"Timeout waiting for response\"}";
          goto send_response;
        }
      }
      
      String response = "";
      while (clientSecure.available()) {
        response += clientSecure.readString();
      }
      
      if (response.indexOf("200 OK") > 0 || response.indexOf("200") > 0) {
        jsonResponse = "{\"success\":true,\"message\":\"Test send successful\"}";
        keg.webhook.last_send = millis() / 1000;
        saveWebhookConfig(keg.webhook);
      } else {
        String escapedResponse = escapeJsonString(response.substring(0, 100));
        jsonResponse = "{\"success\":false,\"message\":\"Server error\",\"details\":\"" + escapedResponse + "\"}";
      }
      
      clientSecure.stop();
    } else {
      jsonResponse = "{\"success\":false,\"message\":\"Could not connect to server\"}";
    }
  }
  
send_response:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonResponse);
}

void handleWebhookStatus(WiFiClient &client) {
  String lastSendFormatted = formatTimestamp(keg.webhook.last_send);
  String nextSendFormatted = getNextSendTime(keg.webhook.last_send);
  
  String deviceNameEscaped = escapeJsonString(String(keg.webhook.device_name));
  String apiKeyMasked = maskApiKey(String(keg.webhook.api_key));
  String apiKeyEscaped = escapeJsonString(apiKeyMasked);
  
  String json = "{";
  json += "\"enabled\":" + String(keg.webhook.enabled ? "true" : "false") + ",";
  json += "\"device_name\":\"" + deviceNameEscaped + "\",";
  json += "\"api_key_masked\":\"" + apiKeyEscaped + "\",";
  json += "\"last_send\":" + String(keg.webhook.last_send) + ",";
  json += "\"last_send_formatted\":\"" + escapeJsonString(lastSendFormatted) + "\",";
  json += "\"next_send_formatted\":\"" + escapeJsonString(nextSendFormatted) + "\"";
  json += "}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

void handleSaveEnviosConfig(WiFiClient &client, String &request) {
    Serial.println("=== SAVING SEND CONFIGURATION ===");
    String jsonResponse = "{\"success\":false,\"message\":\"Error\"}";
    
    int bodyStart = request.indexOf("\r\n\r\n") + 4;
        Serial.print("bodyStart: "); Serial.println(bodyStart);  
    if (bodyStart > 4) {
        String body = request.substring(bodyStart);
        Serial.print("📦 JSON received: ");
        Serial.println(body);
        
        EnviosConfig envios;
        loadEnviosConfig(envios); 
        
        if (body.indexOf("\"type\":\"mqtt\"") >= 0) {
            envios.mqtt.enabled = (body.indexOf("\"enabled\":true") >= 0);
            
            int serverPos = body.indexOf("\"server\":\"");
            if (serverPos >= 0) {
                int serverEnd = body.indexOf("\"", serverPos + 10);
                String server = body.substring(serverPos + 10, serverEnd);
                strlcpy(envios.mqtt.server, server.c_str(), sizeof(envios.mqtt.server));
            }
            
            int portPos = body.indexOf("\"port\":");
            if (portPos >= 0) {
                int portEnd = body.indexOf(",", portPos);
                if (portEnd < 0) portEnd = body.indexOf("}", portPos);
                envios.mqtt.port = body.substring(portPos + 7, portEnd).toInt();
            }
            
            int topicPos = body.indexOf("\"topic\":\"");
            if (topicPos >= 0) {
                int topicEnd = body.indexOf("\"", topicPos + 9);
                String topic = body.substring(topicPos + 9, topicEnd);
                strlcpy(envios.mqtt.topic, topic.c_str(), sizeof(envios.mqtt.topic));
            }
            
            int userPos = body.indexOf("\"user\":\"");
            if (userPos >= 0) {
                int userEnd = body.indexOf("\"", userPos + 8);
                String user = body.substring(userPos + 8, userEnd);
                strlcpy(envios.mqtt.user, user.c_str(), sizeof(envios.mqtt.user));
            }
            
            int passPos = body.indexOf("\"pass\":\"");
            if (passPos >= 0) {
                int passEnd = body.indexOf("\"", passPos + 8);
                String pass = body.substring(passPos + 8, passEnd);
                strlcpy(envios.mqtt.pass, pass.c_str(), sizeof(envios.mqtt.pass));
            }
        }
        else if (body.indexOf("\"type\":\"brewfather\"") >= 0) {
            envios.brewfather.enabled = (body.indexOf("\"enabled\":true") >= 0);
            
            int devicePos = body.indexOf("\"device_name\":\"");
            if (devicePos >= 0) {
                int deviceEnd = body.indexOf("\"", devicePos + 15);
                String device = body.substring(devicePos + 15, deviceEnd);
                strlcpy(envios.brewfather.device_name, device.c_str(), sizeof(envios.brewfather.device_name));
            }
            
            int idPos = body.indexOf("\"personal_id\":\"");
            if (idPos >= 0) {
                int idEnd = body.indexOf("\"", idPos + 15);
                String id = body.substring(idPos + 15, idEnd);
                strlcpy(envios.brewfather.personal_id, id.c_str(), sizeof(envios.brewfather.personal_id));
            }
        }
        else if (body.indexOf("\"type\":\"taplist\"") >= 0) {
            envios.taplist.enabled = (body.indexOf("\"enabled\":true") >= 0);
            
            int venuePos = body.indexOf("\"venue_id\":\"");
            if (venuePos >= 0) {
                int venueEnd = body.indexOf("\"", venuePos + 12);
                String venue = body.substring(venuePos + 12, venueEnd);
                strlcpy(envios.taplist.venue_id, venue.c_str(), sizeof(envios.taplist.venue_id));
            }

            int tapPos = body.indexOf("\"tap_number\":\"");
            if (tapPos >= 0) {
                int tapEnd = body.indexOf("\"", tapPos + 14);
                String tap = body.substring(tapPos + 14, tapEnd);
                strlcpy(envios.taplist.tap_number, tap.c_str(), sizeof(envios.taplist.tap_number));
            }

            int tokenPos = body.indexOf("\"api_token\":\"");
            if (tokenPos >= 0) {
                int tokenEnd = body.indexOf("\"", tokenPos + 13);
                String token = body.substring(tokenPos + 13, tokenEnd);
                strlcpy(envios.taplist.api_token, token.c_str(), sizeof(envios.taplist.api_token));
            }
        }
        else if (body.indexOf("\"type\":\"http\"") >= 0) {
            envios.http_generic.enabled = (body.indexOf("\"enabled\":true") >= 0);
            
            int urlPos = body.indexOf("\"url\":\"");
            if (urlPos >= 0) {
                int urlEnd = body.indexOf("\"", urlPos + 7);
                String url = body.substring(urlPos + 7, urlEnd);
                strlcpy(envios.http_generic.url, url.c_str(), sizeof(envios.http_generic.url));
            }
            
            int keyPos = body.indexOf("\"api_key\":\"");
            if (keyPos >= 0) {
                int keyEnd = body.indexOf("\"", keyPos + 11);
                String key = body.substring(keyPos + 11, keyEnd);
                strlcpy(envios.http_generic.api_key, key.c_str(), sizeof(envios.http_generic.api_key));
            }
        }
        
        saveEnviosConfig(envios);
        
        jsonResponse = "{\"success\":true,\"message\":\"Configuration saved\"}";
    }
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(jsonResponse);
}
void handleHome(WiFiClient &client) {
  String mode = (WiFi.status() == WL_CONNECTED) ? "WiFi Client" : "Access Point";
  String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "PlaatoKegHUB";
  String kegConnected = (client && client.connected()) ? "✅ Connected" : "❌ Disconnected";
  
  bool isMetric = (keg.config.unit_system == 1);
  bool isVolumeMode = (keg.config.display_mode == 2);
  
  String webhookStatus = keg.webhook.enabled ? "✅ ACTIVE" : "⭕ INACTIVE";
  String lastSendFormatted = formatTimestamp(keg.webhook.last_send);
  
  String content = String(HOME_CONTENT);
  
  content.replace("{VOLUME_LABEL}", isVolumeMode ? "🍺 Remaining Volume" : "⚖️ Remaining Weight");
  
  String volumeUnit = isMetric ? "L" : "gal";
  String weightUnit = isMetric ? "kg" : "lb";
  String tempUnit = isMetric ? "°C" : "°F";
  
  content.replace("{VOLUME_UNIT}", volumeUnit);
  content.replace("{WEIGHT_UNIT}", weightUnit);
  content.replace("{TEMP_UNIT}", tempUnit);
  
  float amountLeft = keg.amount_left;
  float lastPour = keg.last_pour_value;
  float kegTemp = keg.keg_temperature;
  float chipTemp = keg.chip_temperature;
  
  if (!isMetric) {
    amountLeft = amountLeft * 0.264172;
    lastPour = lastPour * 0.264172;
    kegTemp = kegTemp * 1.8 + 32;
    chipTemp = chipTemp * 1.8 + 32;
  }
  
  content.replace("{AMOUNT_LEFT}", String(amountLeft, 2));
  content.replace("{PERCENT_LEFT}", String(keg.percent_of_beer_left, 1));
  content.replace("{KEG_TEMP}", String(kegTemp, 1));
  content.replace("{CHIP_TEMP}", String(chipTemp, 1));
  content.replace("{WIFI_SIGNAL}", String(keg.wifi_signal_strength));
  content.replace("{RSSI}", String(keg.rssi));
  content.replace("{IS_POURING}", keg.is_pouring ? 
    "<span class='pouring-yes'>🔴 POURING</span>" : 
    "<span class='pouring-no'>⚪ IDLE</span>");
  content.replace("{LAST_POUR}", String(lastPour, 2));
  content.replace("{PRESSURE}", String(keg.pressure, 2));
  content.replace("{FIRMWARE}", keg.firmware_version);
  
  content.replace("{V63}", String(keg.v63, 3));
  content.replace("{V69}", String(keg.v69, 3));
  content.replace("{V76}", String(keg.v76, 3));
  content.replace("{V87}", String(keg.v87, 3));
  
  content.replace("{EXTRA_VISIBLE}", (keg.v63 != 0 || keg.v69 != 0 || keg.v76 != 0 || keg.v87 != 0) ? "grid" : "none");
  content.replace("{V63_VISIBLE}", (keg.v63 != 0) ? "block" : "none");
  content.replace("{V69_VISIBLE}", (keg.v69 != 0) ? "block" : "none");
  content.replace("{V76_VISIBLE}", (keg.v76 != 0) ? "block" : "none");
  content.replace("{V87_VISIBLE}", (keg.v87 != 0) ? "block" : "none");
  
  content.replace("{MODE}", mode);
  content.replace("{UNIT_SYSTEM}", isMetric ? "Metric" : "Imperial");
  content.replace("{SSID}", ssid);
  content.replace("{KEG_CONNECTED}", kegConnected);
  content.replace("{WEBHOOK_STATUS}", webhookStatus);
  content.replace("{LAST_SEND}", lastSendFormatted);
  
  sendPage(client, "home", content);
}

void handleTokenPage(WiFiClient &client) {
  PlaatoTokenConfig tokenConfig;
  loadPlaatoToken(tokenConfig);
  
  String content = String(TOKEN_CONTENT);
  content.replace("{CURRENT_TOKEN}", String(tokenConfig.token));
  
  sendPage(client, "token", content);
}

void handleSaveToken(WiFiClient &client, String &request) {
  Serial.println("=== SAVING PLAATO TOKEN ===");
  
  String token = "";
  String confirm = "";
  
  int bodyStart = request.indexOf("\r\n\r\n") + 4;
  if (bodyStart > 4) {
    String body = request.substring(bodyStart);
    
    int tokenPos = body.indexOf("token=");
    if (tokenPos >= 0) {
      int tokenEnd = body.indexOf("&", tokenPos);
      if (tokenEnd < 0) tokenEnd = body.length();
      token = body.substring(tokenPos + 6, tokenEnd);
      token = urlDecode(token);
      token.toUpperCase();
    }
    
    int confirmPos = body.indexOf("confirm_token=");
    if (confirmPos >= 0) {
      int confirmEnd = body.indexOf("&", confirmPos);
      if (confirmEnd < 0) confirmEnd = body.length();
      confirm = body.substring(confirmPos + 14, confirmEnd);
      confirm = urlDecode(confirm);
      confirm.toUpperCase();
    }
  }
  
  bool valid = true;
  String errorMsg = "";
  
  if (token.length() != 32) {
    valid = false;
    errorMsg = "Token must be 32 characters long";
  } else if (token != confirm) {
    valid = false;
    errorMsg = "Tokens do not match";
  } else {
    for (int i = 0; i < 32; i++) {
      char c = token[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
        valid = false;
        errorMsg = "Only hexadecimal characters allowed";
        break;
      }
    }
  }
  
  if (valid && token.length() == 32) {
    PlaatoTokenConfig tokenConfig;
    tokenConfig.magic = TOKEN_MAGIC;
    strlcpy(tokenConfig.token, token.c_str(), sizeof(tokenConfig.token));
    
    for (int i = 0; i < 16; i++) {
      char hex_byte[3] = { token[i*2], token[i*2+1], 0 };
      tokenConfig.raw_token[i] = (uint8_t)strtol(hex_byte, NULL, 16);
    }
    
    savePlaatoToken(tokenConfig);
    
    Serial.println("✅ Token saved successfully:");
    Serial.println("  Token: " + token);
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    
    String result = String(TOKEN_RESULT_HTML);
    result.replace("{TOKEN}", token);
    client.println(result);
    client.stop();
    
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("❌ Error validating token: " + errorMsg);
    
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/html");
    client.println();
    client.println("<html><body>");
    client.println("<h1>❌ Error: " + errorMsg + "</h1>");
    client.println("<a href='/token'>Go back</a>");
    client.println("</body></html>");
  }
}

void handleResetToken(WiFiClient &client, String &request) {
  Serial.println("=== RESTORING DEFAULT TOKEN ===");
  
  PlaatoTokenConfig defaultToken;
  setDefaultPlaatoToken(defaultToken);
  savePlaatoToken(defaultToken);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  String result = String(TOKEN_RESULT_HTML);
  result.replace("{TOKEN}", String(defaultToken.token));
  client.println(result);
  client.stop();
  
  delay(1000);
  ESP.restart();
}

void handleWifi(WiFiClient &client) {
  WiFiConfig config;
  bool hasConfig = loadWiFiConfig(config);
  
  String modeInfo = (WiFi.status() == WL_CONNECTED) ? 
    "✅ Connected to: " + String(WiFi.SSID()) : 
    "📡 AP Mode - Configure your WiFi";
  
  String hostname = String(config.hostname);
  if (hostname.length() == 0) hostname = "PlaatoKegHub";
  
  String ssid = String(config.ssid);
  String password = String(config.password);
  String use_static_ip_checked = config.use_static_ip ? "checked" : "";
  String static_ip_display = config.use_static_ip ? "block" : "none";
  String static_ip = String(config.static_ip);
  String gateway = String(config.gateway);
  String subnet = String(config.subnet);
  String dns1 = String(config.dns1);
  String dns2 = String(config.dns2);
  
  String content = String(WIFI_CONTENT);
  content.replace("{MODE_INFO}", modeInfo);
  content.replace("{HOSTNAME}", hostname);
  content.replace("{SSID}", ssid);
  content.replace("{PASSWORD}", password);
  content.replace("{USE_STATIC_IP_CHECKED}", use_static_ip_checked);
  content.replace("{STATIC_IP_DISPLAY}", static_ip_display);
  content.replace("{STATIC_IP}", static_ip);
  content.replace("{GATEWAY}", gateway);
  content.replace("{SUBNET}", subnet);
  content.replace("{DNS1}", dns1);
  content.replace("{DNS2}", dns2);
  
  sendPage(client, "wifi", content);
}

void handleSaveWifi(WiFiClient &client, String &request) {
  Serial.println("\n=== SAVING WIFI CONFIGURATION ===");
  
  WiFiConfig config;
  memset(&config, 0, sizeof(WiFiConfig));
  strlcpy(config.hostname, "PlaatoKegHub", sizeof(config.hostname));
  config.use_static_ip = false;
  
  int bodyStart = request.indexOf("\r\n\r\n") + 4;
  if (bodyStart > 4) {
    String body = request.substring(bodyStart);
    
    int start = 0;
    while (start < body.length()) {
      int end = body.indexOf('&', start);
      if (end < 0) end = body.length();
      
      String pair = body.substring(start, end);
      int eqPos = pair.indexOf('=');
      
      if (eqPos > 0) {
        String key = pair.substring(0, eqPos);
        String value = (eqPos < pair.length() - 1) ? pair.substring(eqPos + 1) : "";
        
        value.replace('+', ' ');
        value = urlDecode(value);
        value.replace("\r", "");
        value.replace("\n", "");
        value.trim();
        
        if (key == "hostname") {
          if (value.length() > 0) strlcpy(config.hostname, value.c_str(), sizeof(config.hostname));
        }
        else if (key == "ssid") {
          strlcpy(config.ssid, value.c_str(), sizeof(config.ssid));
        }
        else if (key == "password") {
          strlcpy(config.password, value.c_str(), sizeof(config.password));
        }
        else if (key == "use_static_ip") {
          config.use_static_ip = (value == "on" || value == "1" || value == "true");
        }
        else if (key == "static_ip") {
          strlcpy(config.static_ip, value.c_str(), sizeof(config.static_ip));
        }
        else if (key == "gateway") {
          strlcpy(config.gateway, value.c_str(), sizeof(config.gateway));
        }
        else if (key == "subnet") {
          strlcpy(config.subnet, value.c_str(), sizeof(config.subnet));
        }
        else if (key == "dns1") {
          strlcpy(config.dns1, value.c_str(), sizeof(config.dns1));
        }
        else if (key == "dns2") {
          strlcpy(config.dns2, value.c_str(), sizeof(config.dns2));
        }
      }
      
      start = end + 1;
      if (end == body.length()) break;
    }
  }
  
  saveWiFiConfig(config);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  String result = "<html><head><meta http-equiv='refresh' content='5;url=/wifi'></head>";
  result += "<body style='font-family:Arial;text-align:center;padding:50px;'>";
  result += "<h1 style='color:green;'>✅ Configuration saved</h1>";
  
  if (config.use_static_ip) {
    result += "<p><strong>Static IP:</strong> " + String(config.static_ip) + "</p>";
    if (strlen(config.dns1) > 0) {
      result += "<p><strong>DNS1:</strong> " + String(config.dns1) + "</p>";
    }
  } else {
    result += "<p><strong>Mode:</strong> DHCP</p>";
  }
  
  result += "<p>Restarting in 5 seconds...</p>";
  result += "</body></html>";
  
  client.println(result);
  client.stop();
  
  delay(1000);
  ESP.restart();
}

void handleAjustes(WiFiClient &client) {
  String content = String(AJUSTES_CONTENT);
  
  bool isMetric = (keg.config.unit_system == 1);
  
  float tara = keg.config.empty_keg_weight;
  float maxVol = keg.config.max_keg_volume;
  float minAlarm = keg.config.min_temperature_alarm;
  float maxAlarm = keg.config.max_temperature_alarm;
  float tempOffset = keg.config.temperature_offset;
  
  if (!isMetric) {
    tara = tara * 2.20462;
    maxVol = maxVol * 0.264172;
    minAlarm = minAlarm * 1.8 + 32;
    maxAlarm = maxAlarm * 1.8 + 32;
    tempOffset = tempOffset * 1.8;
  }
  
  content.replace("{CURRENT_TARA}", String(tara, 1));
  content.replace("{CURRENT_MAX_VOLUME}", String(maxVol, 1));
  content.replace("{CURRENT_CALIBRATION}", String(keg.config.calibration_factor, 2));
  content.replace("{CURRENT_TEMP_OFFSET}", String(tempOffset, 1));
  content.replace("{CURRENT_SENSITIVITY}", String(keg.config.sensitivity));
  content.replace("{CURRENT_UNITS}", keg.config.unit_system == 1 ? "Metric" : "Imperial");
  content.replace("{CURRENT_MODE}", keg.config.display_mode == 2 ? "Volume" : "Weight");
  content.replace("{CURRENT_MIN_ALARM}", String(minAlarm, 1));
  content.replace("{CURRENT_MAX_ALARM}", String(maxAlarm, 1));
  
  content.replace("{WEIGHT_UNIT}", isMetric ? "kg" : "lb");
  content.replace("{VOLUME_UNIT}", isMetric ? "L" : "gal");
  content.replace("{TEMP_UNIT}", isMetric ? "°C" : "°F");
  
  content.replace("{UNIT_SYSTEM_NUM}", String(keg.config.unit_system));
  content.replace("{DISPLAY_MODE_NUM}", String(keg.config.display_mode));
  
  content.replace("{UNIT_METRIC_SELECTED}", keg.config.unit_system == 1 ? "selected" : "");
  content.replace("{UNIT_IMPERIAL_SELECTED}", keg.config.unit_system == 0 ? "selected" : "");
  content.replace("{MODE_VOLUME_SELECTED}", keg.config.display_mode == 2 ? "selected" : "");
  content.replace("{MODE_WEIGHT_SELECTED}", keg.config.display_mode == 1 ? "selected" : "");
  
  sendPage(client, "ajustes", content);
}

void handleAcerca(WiFiClient &client) {
  sendPage(client, "acerca", String(ACERCA_CONTENT));
}

void handleApiKegData(WiFiClient &client) {
  bool isMetric = (keg.config.unit_system == 1);
  bool isVolumeMode = (keg.config.display_mode == 2);
  
  float amountLeft = keg.amount_left;
  float lastPour = keg.last_pour_value;
  float kegTemp = keg.keg_temperature;
  float chipTemp = keg.chip_temperature;
  float maxVolume = keg.config.max_keg_volume;
  
  if (!isMetric) {
    amountLeft = amountLeft * 0.264172;
    lastPour = lastPour * 0.264172;
    kegTemp = kegTemp * 1.8 + 32;
    chipTemp = chipTemp * 1.8 + 32;
    maxVolume = maxVolume * 0.264172;
  }
  
  String json = "{";
  json += "\"amount_left\":\"" + String(amountLeft, 2) + "\",";
  json += "\"percent_left\":" + String(keg.percent_of_beer_left, 1) + ",";
  json += "\"keg_temp\":\"" + String(kegTemp, 1) + "\",";
  json += "\"chip_temp\":\"" + String(chipTemp, 1) + "\",";
  json += "\"wifi_signal\":" + String(keg.wifi_signal_strength) + ",";
  json += "\"rssi\":" + String(keg.rssi) + ",";
  json += "\"is_pouring\":" + String(keg.is_pouring ? "true" : "false") + ",";
  json += "\"last_pour\":\"" + String(lastPour, 2) + "\",";
  json += "\"pressure\":" + String(keg.pressure, 2) + ",";
  json += "\"firmware\":\"" + escapeJsonString(keg.firmware_version) + "\",";
  json += "\"volume_unit\":\"" + String(isMetric ? "L" : "gal") + "\",";
  json += "\"weight_unit\":\"" + String(isMetric ? "kg" : "lb") + "\",";
  json += "\"temp_unit\":\"" + String(isMetric ? "°C" : "°F") + "\"";
  
  if (keg.v63 != 0) json += ",\"v63\":" + String(keg.v63, 3);
  if (keg.v69 != 0) json += ",\"v69\":" + String(keg.v69, 3);
  if (keg.v76 != 0) json += ",\"v76\":\"" + String(isMetric ? keg.v76 : keg.v76 * 0.264172, 2) + "\"";
  if (keg.v87 != 0) json += ",\"v87\":" + String(keg.v87, 3);
  
  json += "}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

void handleApiKegConfigStatus(WiFiClient &client) {
  bool isMetric = (keg.config.unit_system == 1);
  
  float tara = keg.config.empty_keg_weight;
  float maxVol = keg.config.max_keg_volume;
  float minAlarm = keg.config.min_temperature_alarm;
  float maxAlarm = keg.config.max_temperature_alarm;
  float tempOffset = keg.config.temperature_offset;
  
  if (!isMetric) {
    tara = tara * 2.20462;
    maxVol = maxVol * 0.264172;
    minAlarm = minAlarm * 1.8 + 32;
    maxAlarm = maxAlarm * 1.8 + 32;
    tempOffset = tempOffset * 1.8;
  }
  
  String json = "{";
  json += "\"empty_keg_weight\":\"" + String(tara, 1) + "\",";
  json += "\"max_keg_volume\":\"" + String(maxVol, 1) + "\",";
  json += "\"calibration_factor\":" + String(keg.config.calibration_factor, 2) + ",";
  json += "\"temperature_offset\":\"" + String(tempOffset, 1) + "\",";
  json += "\"sensitivity\":" + String(keg.config.sensitivity) + ",";
  json += "\"unit_system\":" + String(keg.config.unit_system) + ",";
  json += "\"display_mode\":" + String(keg.config.display_mode) + ",";
  json += "\"min_temperature_alarm\":\"" + String(minAlarm, 1) + "\",";
  json += "\"max_temperature_alarm\":\"" + String(maxAlarm, 1) + "\"";
  json += "}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

void handleApiSendKegConfig(WiFiClient &client, String &request) {
  Serial.println("\n=== RECEIVED CONFIGURATION ===");
  String jsonResponse = "{\"success\":false,\"message\":\"Error\"}";
  
  int bodyStart = request.indexOf("\r\n\r\n") + 4;
  if (bodyStart > 4) {
    String body = request.substring(bodyStart);
    
    float tara = 2.5;
    float maxVol = 19.8;
    float calib = 1.0;
    float tempOffset = 0.0;
    int sens = 10;
    int units = 1;
    int mode = 2;
    float minAlarm = 0.0;
    float maxAlarm = 30.0;
    
    int taraPos = body.indexOf("\"tara\":");
    if (taraPos > 0) {
      int taraEnd = body.indexOf(",", taraPos);
      if (taraEnd < 0) taraEnd = body.indexOf("}", taraPos);
      String taraStr = body.substring(taraPos + 7, taraEnd);
      taraStr.replace("\"", "");
      taraStr.trim();
      tara = taraStr.toFloat();
    }
    
    int maxVolPos = body.indexOf("\"max_volume\":");
    if (maxVolPos > 0) {
      int maxVolEnd = body.indexOf(",", maxVolPos);
      if (maxVolEnd < 0) maxVolEnd = body.indexOf("}", maxVolPos);
      String maxVolStr = body.substring(maxVolPos + 13, maxVolEnd);
      maxVolStr.replace("\"", "");
      maxVolStr.trim();
      maxVol = maxVolStr.toFloat();
    }
    
    int calibPos = body.indexOf("\"calibration\":");
    if (calibPos > 0) {
      int calibEnd = body.indexOf(",", calibPos);
      if (calibEnd < 0) calibEnd = body.indexOf("}", calibPos);
      String calibStr = body.substring(calibPos + 13, calibEnd);
      calibStr.replace("\"", "");
      calibStr.trim();
      calib = calibStr.toFloat();
    }
    
    int tempOffsetPos = body.indexOf("\"temp_offset\":");
    if (tempOffsetPos > 0) {
      int tempOffsetEnd = body.indexOf(",", tempOffsetPos);
      if (tempOffsetEnd < 0) tempOffsetEnd = body.indexOf("}", tempOffsetPos);
      String tempOffsetStr = body.substring(tempOffsetPos + 14, tempOffsetEnd);
      tempOffsetStr.replace("\"", "");
      tempOffsetStr.trim();
      tempOffset = tempOffsetStr.toFloat();
    }
    
    int sensPos = body.indexOf("\"sensitivity\":");
    if (sensPos > 0) {
      int sensEnd = body.indexOf(",", sensPos);
      if (sensEnd < 0) sensEnd = body.indexOf("}", sensPos);
      String sensStr = body.substring(sensPos + 13, sensEnd);
      sensStr.replace("\"", "");
      sensStr.trim();
      sens = sensStr.toInt();
    }
    
    int unitsPos = body.indexOf("\"unit_system\":");
    if (unitsPos > 0) {
      int unitsEnd = body.indexOf(",", unitsPos);
      if (unitsEnd < 0) unitsEnd = body.indexOf("}", unitsPos);
      String unitsStr = body.substring(unitsPos + 14, unitsEnd);
      unitsStr.replace("\"", "");
      unitsStr.trim();
      units = unitsStr.toInt();
    }
    
    int modePos = body.indexOf("\"display_mode\":");
    if (modePos > 0) {
      int modeEnd = body.indexOf(",", modePos);
      if (modeEnd < 0) modeEnd = body.indexOf("}", modePos);
      String modeStr = body.substring(modePos + 15, modeEnd);
      modeStr.replace("\"", "");
      modeStr.trim();
      mode = modeStr.toInt();
    }
    
    int minAlarmPos = body.indexOf("\"min_alarm\":");
    if (minAlarmPos > 0) {
      int minAlarmEnd = body.indexOf(",", minAlarmPos);
      if (minAlarmEnd < 0) minAlarmEnd = body.indexOf("}", minAlarmPos);
      String minAlarmStr = body.substring(minAlarmPos + 12, minAlarmEnd);
      minAlarmStr.replace("\"", "");
      minAlarmStr.trim();
      minAlarm = minAlarmStr.toFloat();
    }
    
    int maxAlarmPos = body.indexOf("\"max_alarm\":");
    if (maxAlarmPos > 0) {
      int maxAlarmEnd = body.indexOf(",", maxAlarmPos);
      if (maxAlarmEnd < 0) maxAlarmEnd = body.indexOf("}", maxAlarmPos);
      String maxAlarmStr = body.substring(maxAlarmPos + 12, maxAlarmEnd);
      maxAlarmStr.replace("\"", "");
      maxAlarmStr.trim();
      maxAlarm = maxAlarmStr.toFloat();
    }
    
    if (units == 0) {
      tara = tara / 2.20462;
      maxVol = maxVol / 0.264172;
      minAlarm = (minAlarm - 32) / 1.8;
      maxAlarm = (maxAlarm - 32) / 1.8;
      tempOffset = tempOffset / 1.8;
    }
    
    keg.config.empty_keg_weight = tara;
    keg.config.max_keg_volume = maxVol;
    keg.config.calibration_factor = calib;
    keg.config.temperature_offset = tempOffset;
    keg.config.sensitivity = sens;
    keg.config.unit_system = units;
    keg.config.display_mode = mode;
    keg.config.min_temperature_alarm = minAlarm;
    keg.config.max_temperature_alarm = maxAlarm;
    
    if (tara > 0) sendCommandToKeg(51, String(tara, 1));
    delay(50);
    if (maxVol > 0) sendCommandToKeg(76, String(maxVol, 1));
    delay(50);
    sendCommandToKeg(88, String(mode));
    delay(50);
    
    saveKegConfig(keg.config);
    
    jsonResponse = "{\"success\":true,\"message\":\"✅ Configuration saved permanently\"}";
  }
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonResponse);
}

void handleSystemInfo(WiFiClient &client) {
    String json = "{";
    json += "\"flashSize\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\"";
    json += "}";
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(json);
}

void handleStatus(WiFiClient &client) {
  String status = getConnectionStatus();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println(status);
}

void sendOTAError(WiFiClient &client, String message) {
    Serial.print("❌ OTA Error: "); Serial.println(message);
    
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<html><body style='font-family:Arial;text-align:center;padding:50px;'>");
    client.println("<h1 style='color:red;'>❌ Error</h1>");
    client.println("<p>" + message + "</p>");
    client.println("<p><a href='/acerca'>Go back</a></p>");
    client.println("</body></html>");
    client.stop();
}

void handleOTAUpload(WiFiClient &client, String &header, uint8_t* data, size_t dataLen) {
    Serial.printf("\n=== STARTING OTA ===\n");
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());   

    size_t fileStart = 0;
    bool found = false;
    
    for (size_t i = 0; i < min(dataLen, (size_t)4096); i++) {
        if (data[i] == 0xE9) {
            fileStart = i;
            found = true;
            break;
        }
    }
    
    if (!found) {
        sendOTAError(client, "No valid firmware found");
        return;
    }
    
    size_t fileEnd = dataLen;
    for (size_t i = dataLen - 100; i > fileStart && i < dataLen; i--) {
        if (data[i] == '-' && data[i+1] == '-') {
            fileEnd = i;
            while (fileEnd > fileStart && (data[fileEnd-1] == '\n' || data[fileEnd-1] == '\r')) {
                fileEnd--;
            }
            break;
        }
    }
    
    size_t fileSize = fileEnd - fileStart;
    Serial.print("Firmware size: "); Serial.print(fileSize); Serial.println(" bytes");
    
    if (fileSize < 10000 || fileSize > 1900000) {
        sendOTAError(client, "Invalid size: " + String(fileSize));
        return;
    }
    
    if (SPIFFS.exists("/update.bin")) {
        SPIFFS.remove("/update.bin");
    }
    
    File file = SPIFFS.open("/update.bin", FILE_WRITE);
    if (!file) {
        sendOTAError(client, "Could not create file in SPIFFS");
        return;
    }
    
    size_t written = file.write(&data[fileStart], fileSize);
    file.close();
    
    if (written != fileSize) {
        SPIFFS.remove("/update.bin");
        sendOTAError(client, "Error writing to SPIFFS");
        return;
    }
    
    file = SPIFFS.open("/update.bin", FILE_READ);
    if (!file) {
        SPIFFS.remove("/update.bin");
        sendOTAError(client, "Could not open file for verification");
        return;
    }
    
    uint8_t magicCheck;
    file.read(&magicCheck, 1);
    file.close();
    
    if (magicCheck != 0xE9) {
        SPIFFS.remove("/update.bin");
        sendOTAError(client, "Corrupted file: magic byte mismatch");
        return;
    }
    
    file = SPIFFS.open("/update.bin", FILE_READ);
    if (!file) {
        sendOTAError(client, "Could not open file for reading");
        return;
    }
    
    size_t updateSize = file.size();
    Serial.printf("Update size: %d bytes\n", updateSize);
    
    if (!Update.begin(updateSize, U_FLASH)) {
        file.close();
        SPIFFS.remove("/update.bin");
        sendOTAError(client, "Could not start update: " + String(Update.errorString()));
        return;
    }
    
    const size_t chunkSize = 4096;
    uint8_t buffer[chunkSize];
    size_t totalWritten = 0;
    size_t lastProgress = 0;
    
    while (file.available()) {
        size_t bytesRead = file.read(buffer, chunkSize);
        if (bytesRead > 0) {
            size_t bytesWritten = Update.write(buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                file.close();
                Update.end();
                SPIFFS.remove("/update.bin");
                sendOTAError(client, "Error writing to flash");
                return;
            }
            totalWritten += bytesWritten;
            
            if (totalWritten - lastProgress > 50000) {
                Serial.printf("Progress: %d/%d bytes (%d%%)\n", 
                    totalWritten, updateSize, (totalWritten * 100) / updateSize);
                lastProgress = totalWritten;
            }
        }
        yield();
    }
    
    file.close();
    
    if (totalWritten != updateSize) {
        SPIFFS.remove("/update.bin");
        Update.end();
        sendOTAError(client, "Incorrect size: " + String(totalWritten) + " vs " + String(updateSize));
        return;
    }
    
    if (Update.end(true)) {
        SPIFFS.remove("/update.bin");
        Serial.println("✅ OTA SUCCESSFUL - Restarting in 2 seconds");
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();
        client.println("<!DOCTYPE html><html>");
        client.println("<head><meta charset='UTF-8'><meta http-equiv='refresh' content='15;url=/'></head>");
        client.println("<body style='font-family:Arial;text-align:center;padding:50px;background:#f0f0f0;'>");
        client.println("<div style='background:white;padding:30px;border-radius:10px;max-width:500px;margin:auto;'>");
        client.println("<h1 style='color:green;'>✅ Update Successful</h1>");
        client.println("<p>The device is restarting...</p>");
        client.println("<p>The page will reload in 15 seconds.</p>");
        client.println("</div></body></html>");
        client.stop();
        
        delay(2000);
        ESP.restart();
    } else {
        SPIFFS.remove("/update.bin");
        String errorStr = Update.errorString();
        Serial.printf("❌ Update.end() failed: %s\n", errorStr.c_str());
        sendOTAError(client, "Error finalizing: " + errorStr);
    }
}

void setupWebServer() {
  webServer.begin();
  Serial.println("✅ Web server started on port 80");
}

void handleWebServer() {
  WiFiClient client = webServer.available();
  if (!client) return;

  client.setTimeout(300);
  client.setNoDelay(true);
  String request = "";
  unsigned long timeout = millis() + 5000;

  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
    yield();
  }

  int contentLength = 0;
  int clPos = request.indexOf("Content-Length: ");
  if (clPos > 0) {
    int clEnd = request.indexOf("\r\n", clPos);
    contentLength = request.substring(clPos + 16, clEnd).toInt();
  }

  bool isOTA = request.indexOf("POST /update ") >= 0;

  if (isOTA && contentLength > 0) {
    Serial.printf("\n=== OTA STARTED ===\n");
    Serial.printf("Size: %d bytes\n", contentLength);

    uint8_t header[16];
    size_t headerRead = 0;
    unsigned long t = millis() + 5000;
    while (headerRead < 16 && client.connected() && millis() < t) {
      if (client.available()) header[headerRead++] = client.read();
      else yield();
    }

    Serial.printf("Magic: 0x%02X, ChipID: 0x%02X\n", header[0], header[12]);

    if (headerRead < 16 || header[0] != 0xE9) {
      sendOTAError(client, "Invalid file: magic=0x" + String(header[0], HEX));
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      sendOTAError(client, "Error starting Update: " + String(Update.errorString()));
      return;
    }

    Update.write(header, 16);
    size_t written = 16;
    size_t totalReceived = 16;
    unsigned long lastData = millis();
    unsigned long lastPrint = millis();
    uint8_t buff[512];

    while (client.connected() && (millis() - lastData < 15000)) {
      if (!client.available()) {
        yield();
        if (totalReceived >= (size_t)(contentLength - 10)) break;
        continue;
      }
      int n = client.available();
      if (n > 512) n = 512;
      int r = client.read(buff, n);
      if (r <= 0) continue;
      lastData = millis();
      totalReceived += r;
      Update.write(buff, r);
      written += r;
      if (millis() - lastPrint > 3000) {
        Serial.printf("📥 %.1f%%  flash:%d\n", totalReceived*100.0/contentLength, written);
        lastPrint = millis();
      }
      yield();
    }

    Serial.printf("📊 Total:%d  Flash:%d\n", totalReceived, written);

    if (Update.end(true)) {
      Serial.println("🎉 OTA SUCCESSFUL");
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      client.println("<html><head><meta http-equiv='refresh' content='15;url=/'></head>");
      client.println("<body style='text-align:center;padding:50px;font-family:Arial'>");
      client.println("<h1 style='color:green'>✅ Update Successful</h1>");
      client.println("<p>Restarting... will reload in 15s</p></body></html>");
      client.stop();
      delay(2000);
      ESP.restart();
    } else {
      sendOTAError(client, "Error finalizing: " + String(Update.errorString()));
    }
    return;
  }

  String body = "";
  if (contentLength > 0 && !isOTA) {
    while ((int)body.length() < contentLength && client.connected() && millis() < timeout) {
      if (client.available()) body += (char)client.read();
    }
  }

  String fullRequest = request + body;

  if      (request.indexOf("GET / ") >= 0)                         handleHome(client);
  else if (request.indexOf("GET /api/keg-data ") >= 0)             handleApiKegData(client);
  else if (request.indexOf("GET /wifi ") >= 0)                     handleWifi(client);
  else if (request.indexOf("GET /token ") >= 0)                    handleTokenPage(client);
  else if (request.indexOf("POST /save-token ") >= 0)              handleSaveToken(client, fullRequest);
  else if (request.indexOf("POST /reset-token ") >= 0)             handleResetToken(client, fullRequest);
  else if (request.indexOf("GET /ajustes ") >= 0)                  handleAjustes(client);
  else if (request.indexOf("GET /envios ") >= 0)                   handleEnvios(client);
  else if (request.indexOf("POST /api/save-webhook-config ") >= 0) handleSaveWebhookConfig(client, fullRequest);
  else if (request.indexOf("GET /api/test-webhook ") >= 0)         handleTestWebhook(client);
  else if (request.indexOf("GET /api/webhook-status ") >= 0)       handleWebhookStatus(client);
  else if (request.indexOf("POST /api/save-envios-config ") >= 0)  handleSaveEnviosConfig(client, fullRequest);
  else if (request.indexOf("GET /acerca ") >= 0)                   handleAcerca(client);
  else if (request.indexOf("GET /status ") >= 0)                   handleStatus(client);
  else if (request.indexOf("POST /save-wifi ") >= 0)               handleSaveWifi(client, fullRequest);
  else if (request.indexOf("GET /api/keg-config-status ") >= 0)    handleApiKegConfigStatus(client);
  else if (request.indexOf("POST /api/send-keg-config ") >= 0)     handleApiSendKegConfig(client, fullRequest);
  else if (request.indexOf("GET /api/system-info ") >= 0)          handleSystemInfo(client);
  else                                                              handleHome(client);

  client.stop();
}