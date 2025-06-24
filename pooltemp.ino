#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid = "N8MDG";
const char* password = "mattg123";

// MQTT Broker settings
const char* mqtt_server = "192.168.1.71";
const int mqtt_port = 1883;
const char* mqtt_user = "dryer"; // leave empty if not needed
const char* mqtt_pass = "test"; // leave empty if not needed

// Sensor pins
#define ONE_WIRE_BUS 4  // GPIO where DS18B20 is connected
#define DHTPIN 5        // GPIO where DHT22 is connected
#define DHTTYPE DHT22   // Type of DHT Sensor

// Web server
AsyncWebServer server(80);

// DS18B20 (Dallas) setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// DHT22 setup
DHT dht(DHTPIN, DHTTYPE);

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// Global sensor data
float dsTemp = 0.0;
float dhtTemp = 0.0;
float dhtHum = 0.0;
float waterTempF = 0.0;
float airTempF = 0.0;

// MQTT Topics
const char* topic_ds18b20 = "home/pooltemp/ds18b20";
const char* topic_dht22_temp = "home/pooltemp/dht22/temperature";
const char* topic_dht22_hum = "home/pooltemp/dht22/humidity";

// HTML Page
String htmlPage() {
  String page = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Pool Temperature Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background: linear-gradient(120deg, #5ee7df 0%, #b490ca 100%);
        font-family: 'Segoe UI', sans-serif;
        color: #222;
        margin: 0; padding: 0;
      }
      .container {
        max-width: 620px;
        margin: 60px auto;
        background: rgba(255,255,255,0.95);
        border-radius: 20px;
        box-shadow: 0 6px 24px rgba(90,90,110,0.12);
        padding: 40px 30px;
        text-align: center;
      }
      h1 {
        color: #5e60ce;
        margin-bottom: 30px;
      }
      .sensor-card {
        margin: 22px auto;
        padding: 28px 18px;
        background: #e7f3ff;
        border-radius: 16px;
        box-shadow: 0 2px 10px rgba(94,96,206,0.07);
        display: inline-block;
        min-width: 220px;
      }
      .sensor-label {
        font-size: 1.3rem;
        font-weight: 500;
        margin-bottom: 12px;
      }
      .sensor-value {
        font-size: 2.5rem;
        color: #168aad;
        margin-bottom: 8px;
        font-weight: 700;
      }
      .sensor-unit {
        font-size: 1.1rem;
        color: #444;
      }
      @keyframes pulse {
        0% { box-shadow: 0 0 0 0 #5e60ce55; }
        70% { box-shadow: 0 0 0 20px rgba(94,96,206,0); }
        100% { box-shadow: 0 0 0 0 rgba(94,96,206,0); }
      }
      .sensor-card {
        animation: pulse 2s infinite;
      }
      .footer {
        margin-top: 38px;
        color: #888;
        font-size: 0.95rem;
      }
    </style>
    <meta http-equiv="refresh" content="5">
  </head>
  <body>
    <div class="container">
      <h1>Pool Temperature Monitor</h1>
      <div class="sensor-card">
        <div class="sensor-label">Water Temp</div>
        <div class="sensor-value">%DS18B20_TEMP%</div>
        <span class="sensor-unit">F</span>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Air Temp</div>
        <div class="sensor-value">%DHT22_TEMP%</div>
        <span class="sensor-unit">F</span>
      </div>
      <div class="sensor-card">
        <div class="sensor-label">Humidity</div>
        <div class="sensor-value">%DHT22_HUM%</div>
        <span class="sensor-unit">%</span>
      </div>
      <div class="footer">
        Updated every 5 seconds &mdash; Copyright 2025 - <a hred="https://github.com/C0deirl"> c0deirl</a>
      </div>
    </div>
  </body>
  </html>
  )rawliteral";

  // Replace placeholders with sensor values
  page.replace("%DS18B20_TEMP%", String(waterTempF, 2));
  page.replace("%DHT22_TEMP%", String(airTempF, 2));
  page.replace("%DHT22_HUM%", String(dhtHum, 2));
  return page;
}

// WiFi Connection
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(600);
    Serial.print(".");
  }
  Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
}

// MQTT
void reconnectMQTT() {
  // Loop until connected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32PoolTemp-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

void publishMQTT() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  // Publish sensor values
  client.publish(topic_ds18b20, String(waterTempF, 2).c_str(), true);
  client.publish(topic_dht22_temp, String(airTempF, 2).c_str(), true);
  client.publish(topic_dht22_hum, String(dhtHum, 2).c_str(), true);
}

void readSensors() {
  sensors.requestTemperatures();
  dsTemp = sensors.getTempCByIndex(0);

  dhtTemp = dht.readTemperature();
  dhtHum = dht.readHumidity();
  // Basic error handling
  if (isnan(dsTemp)) dsTemp = -127.0;
  if (isnan(dhtTemp)) dhtTemp = -127.0;
  if (isnan(dhtHum)) dhtHum = -1.0;

  // Farenheit Conversion
    waterTempF = ((dsTemp * 1.8) + 32);
    airTempF = ((dhtTemp * 1.8) + 32);
}

void setup() {
  Serial.begin(115200);
  sensors.begin();
  dht.begin();

  setupWiFi();

  client.setServer(mqtt_server, mqtt_port);

  // Initial sensor read
  readSensors();

  // Web server route
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    readSensors();
    request->send(200, "text/html", htmlPage());
  });

  // API endpoint for sensor values (JSON)
  server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request){
    readSensors();
    String json = "{";
    json += "\"ds18b20\":" + String(dsTemp, 2) + ",";
    json += "\"dht22_temp\":" + String(dhtTemp, 2) + ",";
    json += "\"dht22_hum\":" + String(dhtHum, 2);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.begin();
}

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 6000; // 6 seconds

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastSensorRead > sensorInterval) {
    lastSensorRead = now;
    readSensors();
    publishMQTT();
    Serial.printf("DS18B20: %.2f°C, DHT22: %.2f°C, %.2f%%\n", dsTemp, dhtTemp, dhtHum);
  }
}