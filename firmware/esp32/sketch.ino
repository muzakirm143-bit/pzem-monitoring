// ============ ESP32 PZEM MQTT DASHBOARD ============
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

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

// ============ INISIALISASI OBJEK ============
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);

WiFiClient espClient;
PubSubClient client(espClient);

// ============ VARIABEL GLOBAL ============
float voltage = 0;
float current = 0;
float power = 0;
float energy = 0;
float frequency = 0;
float pf = 0;
float totalEnergy = 0;
float costPerKwh = 1699.00;
float estimatedCost = 0;

unsigned long lastUpdate = 0;
unsigned long lastDisplay = 0;
unsigned long lastPublish = 0;
unsigned long startTime = 0;

const unsigned long UPDATE_INTERVAL = 2000;
const unsigned long DISPLAY_INTERVAL = 500;
const unsigned long PUBLISH_INTERVAL = 5000;

bool relay1Status = false;
bool relay2Status = false;
bool relay3Status = false;
bool wm_configMode = false;

// ============ FUNGSI SETUP ============
void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 PZEM MQTT Dashboard...");
  
  // Inisialisasi Relay
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);
  relay1Status = false;
  relay2Status = false;
  relay3Status = false;
  
  // Inisialisasi Serial2 untuk PZEM
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);
  
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
  
  // Kirim pesan awal
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
}

// ============ FUNGSI SETUP WIFIMANAGER ============
void setupWiFiManager() {
  WiFiManager wm;
  wm.setCleanConnect(true);
  wm.setConnectTimeout(10);
  wm.setConfigPortalTimeout(120);
  wm.setMinimumSignalQuality(20);
  
  const char* apName = "PZEM-Config";
  const char* apPassword = "12345678";
  
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(10, 10);
  tft.setTextSize(1);
  tft.print("WiFi Manager");
  tft.setCursor(10, 25);
  tft.print("Connecting...");
  
  bool connected = wm.autoConnect(apName, apPassword);
  
  if (!connected) {
    wm_configMode = true;
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(10, 10);
    tft.print("AP MODE ACTIVE");
    tft.setCursor(10, 30);
    tft.print("SSID: " + String(apName));
    tft.setCursor(10, 45);
    tft.print("Pass: " + String(apPassword));
    tft.setCursor(10, 60);
    tft.print("IP: 192.168.4.1");
    tft.setCursor(10, 75);
    tft.print("Connect & browse");
    
    unsigned long apStartTime = millis();
    while (wm_configMode) {
      wm.process();
      if (WiFi.status() == WL_CONNECTED) {
        wm_configMode = false;
        tft.fillScreen(ST7735_BLACK);
        tft.setTextColor(ST7735_GREEN);
        tft.setCursor(10, 20);
        tft.print("WiFi Connected!");
        tft.setCursor(10, 40);
        tft.print("IP: ");
        tft.print(WiFi.localIP());
        delay(2000);
        break;
      }
      if (millis() - apStartTime > 120000) {
        ESP.restart();
      }
      delay(100);
    }
  } else {
    tft.fillScreen(ST7735_BLACK);
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(10, 20);
    tft.print("WiFi Connected!");
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
  
  Serial.print("MQTT Message: ");
  Serial.println(message);
  
  // Parse JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    int relay = doc["relay"];
    bool status = doc["status"];
    
    if (relay == 1) {
      digitalWrite(RELAY1_PIN, status ? LOW : HIGH);
      relay1Status = status;
    } else if (relay == 2) {
      digitalWrite(RELAY2_PIN, status ? LOW : HIGH);
      relay2Status = status;
    } else if (relay == 3) {
      digitalWrite(RELAY3_PIN, status ? LOW : HIGH);
      relay3Status = status;
    }
  }
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32Client")) {
      Serial.println("MQTT Connected!");
      client.subscribe(mqtt_topic_relay);
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
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
  doc["costPerKwh"] = costPerKwh;
  doc["relay1"] = relay1Status;
  doc["relay2"] = relay2Status;
  doc["relay3"] = relay3Status;
  doc["timestamp"] = millis() / 1000;
  
  char buffer[512];
  serializeJson(doc, buffer);
  
  client.publish(mqtt_topic, buffer);
  Serial.println("Data published to MQTT");
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
  voltage = pzem.voltage();
  current = pzem.current();
  power = pzem.power();
  energy = pzem.energy();
  frequency = pzem.frequency();
  pf = pzem.pf();
  
  if (isnan(voltage) || isnan(current) || isnan(power)) {
    Serial.println("Error reading PZEM data!");
    voltage = 0;
    current = 0;
    power = 0;
    energy = 0;
    frequency = 0;
    pf = 0;
  }
}

// ============ FUNGSI HITUNG BIAYA ============
void calculateCost() {
  totalEnergy += (power / 1000.0) * (UPDATE_INTERVAL / 3600.0);
  estimatedCost = totalEnergy * costPerKwh;
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
  tft.print("Biaya: Rp");
  tft.setTextColor(ST7735_WHITE);
  tft.print(estimatedCost, 0);
  
  tft.setCursor(2, 80);
  tft.setTextColor(ST7735_CYAN);
  tft.print("Relay: ");
  
  tft.setTextColor(relay1Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay1Status ? "ON" : "OFF");
  tft.print(" ");
  
  tft.setTextColor(relay2Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay2Status ? "ON" : "OFF");
  tft.print(" ");
  
  tft.setTextColor(relay3Status ? ST7735_GREEN : ST7735_RED);
  tft.print(relay3Status ? "ON" : "OFF");
  
  tft.setCursor(2, 100);
  tft.setTextColor(ST7735_WHITE);
  tft.print("MQTT: ");
  tft.setTextColor(client.connected() ? ST7735_GREEN : ST7735_RED);
  tft.print(client.connected() ? "ON" : "OFF");
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(100, 2);
    tft.setTextColor(ST7735_GREEN);
    tft.print("W");
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
  tft.print("Loading...");
  delay(2000);
}
