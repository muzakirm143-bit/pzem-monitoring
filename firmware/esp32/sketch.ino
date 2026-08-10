// ============ ESP32 PZEM MQTT DASHBOARD ============
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h> // Untuk menyimpan data

// ============ KONFIGURASI PIN ============
#define TFT_CS     5
#define TFT_RST    4
#define TFT_DC     2
#define TFT_MOSI   23
#define TFT_SCLK   18
#define TFT_BL     15

#define RELAY1_PIN 25
#define RELAY2_PIN 26
#define RELAY3_PIN 27

#define PZEM_RX    16
#define PZEM_TX    17

// ============ KONFIGURASI MQTT ============
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_topic = "pzem/esp32/data";
const char* mqtt_topic_relay = "pzem/esp32/relay";
const char* mqtt_topic_reset = "pzem/esp32/reset";

// ============ TARIF LISTRIK ============
const float TARIF_PER_KWH = 605.00;

// ============ INISIALISASI OBJEK ============
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// ============ VARIABEL GLOBAL ============
float voltage = 0;
float current = 0;
float power = 0;
float energy = 0;
float frequency = 0;
float pf = 0;
float totalEnergy = 0;
float estimatedCost = 0;
float previousEnergy = 0;

unsigned long lastUpdate = 0;
unsigned long lastDisplay = 0;
unsigned long lastPublish = 0;
unsigned long startTime = 0;
unsigned long apStartTime = 0;

const unsigned long UPDATE_INTERVAL = 2000;
const unsigned long DISPLAY_INTERVAL = 500;
const unsigned long PUBLISH_INTERVAL = 3000;
const unsigned long AP_TIMEOUT = 180000;

bool relay1Status = false;
bool relay2Status = false;
bool relay3Status = false;
bool wm_configMode = false;
bool pzemOK = false;
bool resetPending = false;

// ============ FUNGSI SETUP ============
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("Starting ESP32 PZEM MQTT Dashboard...");
  Serial.println("💰 Tarif: Rp " + String(TARIF_PER_KWH, 0) + " per kWh (900 VA Subsidi)");
  Serial.println("========================================\n");
  
  // Load data dari Preferences
  preferences.begin("pzem", false);
  totalEnergy = preferences.getFloat("totalEnergy", 0.0);
  previousEnergy = preferences.getFloat("previousEnergy", 0.0);
  preferences.end();
  
  Serial.printf("📊 Loaded totalEnergy: %.3f kWh\n", totalEnergy);
  
  // Inisialisasi Relay
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);
  
  // Inisialisasi Serial2 untuk PZEM
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);
  delay(500);
  
  // Inisialisasi LCD
  tft.initR(INITR_144GREENTAB);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  displaySplash();
  
  // Setup WiFiManager
  setupWiFiManager();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  startTime = millis();
  
  // Baca PZEM pertama kali
  readPZEMData();
  previousEnergy = energy;
  
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
  
  Serial.println("✅ System Ready!");
  Serial.println("========================================\n");
}

// ============ FUNGSI SETUP WIFIMANAGER ============
void setupWiFiManager() {
  WiFiManager wm;
  wm.setCleanConnect(true);
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(180);
  wm.setMinimumSignalQuality(20);
  wm.setDebugOutput(false);
  
  const char* apName = "PZEM-Config";
  const char* apPassword = "12345678";
  
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(10, 10);
  tft.setTextSize(1);
  tft.print("WiFi Manager");
  tft.setCursor(10, 25);
  tft.print("Connecting to WiFi...");
  
  bool connected = wm.autoConnect(apName, apPassword);
  
  if (!connected) {
    wm_configMode = true;
    apStartTime = millis();
    
    tft.fillScreen(ST7735_BLACK);
    
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(10, 10);
    tft.print("AP MODE ACTIVE");
    tft.drawLine(0, 22, 128, 22, ST7735_YELLOW);
    
    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(10, 32);
    tft.print("SSID:");
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(55, 32);
    tft.print("PZEM-Config");
    
    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(10, 48);
    tft.print("Pass:");
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(55, 48);
    tft.print("12345678");
    
    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(10, 64);
    tft.print("IP:");
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(55, 64);
    tft.print("192.168.4.1");
    
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(10, 80);
    tft.print("Connect & browse");
    
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(10, 96);
    tft.print("Restart in");
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(80, 96);
    tft.print("180s");
    
    while (wm_configMode) {
      wm.process();
      
      if (WiFi.status() == WL_CONNECTED) {
        wm_configMode = false;
        tft.fillScreen(ST7735_BLACK);
        tft.setTextColor(ST7735_GREEN);
        tft.setCursor(10, 20);
        tft.print("✅ WiFi Connected!");
        tft.setCursor(10, 40);
        tft.print("IP: ");
        tft.print(WiFi.localIP());
        delay(2000);
        break;
      }
      
      static unsigned long lastTimerUpdate = 0;
      if (millis() - lastTimerUpdate >= 1000) {
        lastTimerUpdate = millis();
        int remaining = (AP_TIMEOUT - (millis() - apStartTime)) / 1000;
        if (remaining < 0) remaining = 0;
        
        tft.fillRect(80, 96, 40, 12, ST7735_BLACK);
        tft.setTextColor(ST7735_YELLOW);
        tft.setCursor(80, 96);
        tft.print(remaining);
        tft.print("s");
      }
      
      if (millis() - apStartTime > AP_TIMEOUT) {
        Serial.println("⏰ Timeout! Restarting...");
        ESP.restart();
      }
      
      delay(50);
    }
  } else {
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(10, 20);
    tft.print("✅ WiFi Connected!");
    tft.setCursor(10, 40);
    tft.print("IP: ");
    tft.print(WiFi.localIP());
    delay(2000);
  }
}

// ============ FUNGSI MQTT ============
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    // ===== RESET COMMAND =====
    if (String(topic) == mqtt_topic_reset) {
      String command = doc["command"];
      if (command == "reset") {
        // Reset total energi dan biaya
        totalEnergy = 0;
        estimatedCost = 0;
        previousEnergy = energy; // Reset previous energy ke current
        resetPending = true;
        
        // Simpan ke Preferences
        preferences.begin("pzem", false);
        preferences.putFloat("totalEnergy", 0.0);
        preferences.putFloat("previousEnergy", previousEnergy);
        preferences.end();
        
        Serial.println("🔄 ===== ENERGY RESET ===== ");
        Serial.println("💰 Total Energy: 0.000 kWh");
        Serial.println("💰 Cost: Rp 0");
        Serial.println("============================");
        
        // Kirim update setelah reset
        publishData();
        return;
      }
    }
    
    // ===== RELAY COMMAND =====
    int relay = doc["relay"];
    bool status = doc["status"];
    
    if (relay == 1) {
      digitalWrite(RELAY1_PIN, status ? LOW : HIGH);
      relay1Status = status;
      Serial.printf("🔴 Relay 1: %s\n", status ? "ON" : "OFF");
    } else if (relay == 2) {
      digitalWrite(RELAY2_PIN, status ? LOW : HIGH);
      relay2Status = status;
      Serial.printf("🔴 Relay 2: %s\n", status ? "ON" : "OFF");
    } else if (relay == 3) {
      digitalWrite(RELAY3_PIN, status ? LOW : HIGH);
      relay3Status = status;
      Serial.printf("🔴 Relay 3: %s\n", status ? "ON" : "OFF");
    }
  }
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("📡 Connecting to MQTT...");
    if (client.connect("ESP32_PZEM")) {
      Serial.println(" ✅ Connected!");
      client.subscribe(mqtt_topic_relay);
      client.subscribe(mqtt_topic_reset);
      Serial.println("📡 Subscribed to:");
      Serial.println("   - " + String(mqtt_topic_relay));
      Serial.println("   - " + String(mqtt_topic_reset));
    } else {
      Serial.printf(" ❌ Failed (rc=%d)\n", client.state());
      delay(5000);
    }
  }
}

void publishData() {
  if (!client.connected()) {
    connectMQTT();
  }
  
  StaticJsonDocument<512> doc;
  doc["voltage"] = voltage;
  doc["current"] = current;
  doc["power"] = power;
  doc["energy"] = energy;
  doc["frequency"] = frequency;
  doc["pf"] = pf;
  doc["totalEnergy"] = totalEnergy;
  doc["estimatedCost"] = estimatedCost;
  doc["costPerKwh"] = TARIF_PER_KWH;
  doc["relay1"] = relay1Status;
  doc["relay2"] = relay2Status;
  doc["relay3"] = relay3Status;
  doc["timestamp"] = millis() / 1000;
  doc["resetStatus"] = resetPending ? "RESET_DONE" : "NORMAL";
  
  char buffer[512];
  serializeJson(doc, buffer);
  
  if (client.publish(mqtt_topic, buffer)) {
    Serial.println("📤 Data published to MQTT");
    if (resetPending) {
      Serial.println("   ✅ Reset confirmed sent to dashboard");
      resetPending = false;
    }
  }
}

// ============ FUNGSI LOOP ============
void loop() {
  unsigned long currentMillis = millis();
  
  if (wm_configMode) {
    return;
  }
  
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();
  
  if (WiFi.status() == WL_CONNECTED) {
    if (currentMillis - lastUpdate >= UPDATE_INTERVAL) {
      lastUpdate = currentMillis;
      readPZEMData();
      calculateCost();
    }
    
    if (currentMillis - lastDisplay >= DISPLAY_INTERVAL) {
      lastDisplay = currentMillis;
      updateDisplay();
    }
    
    if (currentMillis - lastPublish >= PUBLISH_INTERVAL) {
      lastPublish = currentMillis;
      publishData();
    }
  }
}

// ============ FUNGSI BACA PZEM ============
void readPZEMData() {
  float v = pzem.voltage();
  float c = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();
  float f = pzem.frequency();
  float pfVal = pzem.pf();
  
  if (!isnan(v) && !isnan(c) && !isnan(p)) {
    voltage = v;
    current = c;
    power = p;
    energy = e;
    frequency = f;
    pf = pfVal;
    pzemOK = true;
    Serial.printf("📊 PZEM: V=%.1fV, A=%.2fA, W=%.1fW, kWh=%.3f, PF=%.2f\n", 
                  voltage, current, power, energy, pf);
  } else {
    if (!pzemOK) {
      Serial.println("⚠️ PZEM read error - using default values");
      voltage = 0;
      current = 0;
      power = 0;
      energy = 0;
      frequency = 0;
      pf = 0;
    } else {
      Serial.println("⚠️ PZEM read error - using last valid values");
    }
  }
}

// ============ FUNGSI HITUNG BIAYA ============
void calculateCost() {
  // Hanya tambahkan jika PZEM OK dan ada daya
  if (pzemOK && power > 0) {
    // Hitung delta energi dari PZEM
    if (energy >= previousEnergy) {
      float deltaEnergy = energy - previousEnergy;
      totalEnergy += deltaEnergy;
    } else {
      // Jika energy reset, gunakan nilai saat ini
      float deltaEnergy = energy;
      totalEnergy += deltaEnergy;
    }
    previousEnergy = energy;
    
    // Simpan ke Preferences setiap 10 detik
    static unsigned long lastSave = 0;
    if (millis() - lastSave >= 10000) {
      lastSave = millis();
      preferences.begin("pzem", false);
      preferences.putFloat("totalEnergy", totalEnergy);
      preferences.putFloat("previousEnergy", previousEnergy);
      preferences.end();
    }
  }
  
  estimatedCost = totalEnergy * TARIF_PER_KWH;
}

// ============ FUNGSI UPDATE DISPLAY ============
void updateDisplay() {
  tft.fillScreen(ST7735_BLACK);
  
  tft.setCursor(2, 2);
  tft.setTextColor(ST7735_CYAN);
  tft.setTextSize(1);
  tft.print("PZEM Monitor");
  tft.drawLine(0, 12, 128, 12, ST7735_WHITE);
  
  tft.setCursor(2, 16);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("V:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(voltage, 1);
  tft.print("V");
  
  tft.setCursor(70, 16);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("A:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(current, 2);
  tft.print("A");
  
  tft.setCursor(2, 32);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("W:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(power, 1);
  tft.print("W");
  
  tft.setCursor(70, 32);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("kWh:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(energy, 3);
  
  tft.setCursor(2, 48);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("PF:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(pf, 2);
  
  tft.setCursor(70, 48);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("Hz:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(frequency, 1);
  
  tft.setCursor(2, 64);
  tft.setTextColor(ST7735_GREEN);
  tft.print("Total:");
  tft.setTextColor(ST7735_WHITE);
  tft.print(totalEnergy, 2);
  tft.print(" kWh");
  
  tft.setCursor(2, 80);
  tft.setTextColor(ST7735_GREEN);
  tft.print("Biaya: Rp");
  tft.setTextColor(ST7735_WHITE);
  tft.print(estimatedCost, 0);
  
  tft.setCursor(2, 96);
  tft.setTextColor(ST7735_CYAN);
  tft.print("Tarif: Rp");
  tft.setTextColor(ST7735_WHITE);
  tft.print(TARIF_PER_KWH, 0);
  tft.print("/kWh");
  
  tft.setCursor(2, 112);
  tft.setTextColor(ST7735_CYAN);
  tft.print("R:");
  
  tft.setTextColor(relay1Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay1Status ? "1ON" : "1OFF");
  tft.print(" ");
  
  tft.setTextColor(relay2Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay2Status ? "2ON" : "2OFF");
  tft.print(" ");
  
  tft.setTextColor(relay3Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay3Status ? "3ON" : "3OFF");
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(100, 2);
    tft.setTextColor(ST7735_GREEN);
    tft.print("W");
  }
  
  if (pzemOK) {
    tft.setCursor(115, 2);
    tft.setTextColor(ST7735_GREEN);
    tft.print("P");
  } else {
    tft.setCursor(115, 2);
    tft.setTextColor(ST7735_RED);
    tft.print("P");
  }
}

// ============ FUNGSI SPLASH SCREEN ============
void displaySplash() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.print("ESP32");
  tft.setTextSize(1);
  tft.setCursor(10, 45);
  tft.print("PZEM MQTT");
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(15, 65);
  tft.print("900 VA Subsidi");
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(15, 80);
  tft.print("Rp 605/kWh");
  delay(2000);
}
