#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>
#include "DHT.h"

// ================= CONFIGURACIÓN WIFI Y SERVIDOR =================
const char* ssid = "Starlink";
const char* password = "JaimeYPollo";
const char* serverUrl = "https://t7nxm1nr1j.execute-api.us-east-2.amazonaws.com/Prueba/mylambda";

// ================= ASIGNACIÓN DE PINES =================
#define PIN_DHT    18   // DHT22 Data -> D18
#define PIN_TRIG   5    // US-100 Trig -> D5
#define PIN_ECHO   4    // US-100 Echo -> D4 (Jumper trasero quitado)
#define RXD2       16   // GPS TX -> D16 (RX2 en ESP32)
#define TXD2       17   // GPS RX -> D17 (TX2 en ESP32)

// ================= INSTANCIAS =================
#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;

// Temporizador para envíos sin bloquear el procesamiento del GPS
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 10000; // Envío cada 10 segundos

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Puerto Serial 2 para el módulo GPS a 9600 baudios
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // Pines ultrasónico
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // Sensor DHT
  dht.begin();

  // Conexión WiFi
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado exitosamente.");
  Serial.print("IP asignada: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // 1. ALIMENTAR EL PARSER GPS CONTINUAMENTE
  // Se debe leer el búfer de Serial2 constantemente para no perder sentencias
  while (Serial2.available() > 0) {
    gps.encode(Serial2.read());
  }

  // 2. TAREA TEMPORIZADA DE MEDICIÓN Y ENVÍO HTTP (cada 10 segundos)
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();

    // --- Lectura DHT22 ---
    float humedad = dht.readHumidity();
    float temperatura = dht.readTemperature();
    bool dhtOk = !isnan(humedad) && !isnan(temperatura);

    // --- Lectura US-100 ---
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(4);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long duracion = pulseIn(PIN_ECHO, HIGH, 60000); // 60ms timeout
    float distancia = -1.0;

    if (duracion > 0) {
      float velSonido = 0.0343; // cm/us por defecto a 20°C
      if (dhtOk) {
        velSonido = (331.3 + (0.606 * temperatura)) / 10000.0;
      }
      distancia = (duracion * velSonido) / 2.0;
    }

    // --- Lectura procesada del GPS ---
    bool gpsFix = gps.location.isValid();
    double latitud = gpsFix ? gps.location.lat() : 0.0;
    double longitud = gpsFix ? gps.location.lng() : 0.0;
    float altitud = gps.altitude.isValid() ? gps.altitude.meters() : -1.0;
    int satelites = gps.satellites.isValid() ? gps.satellites.value() : 0;

    // --- Imprimir resumen formateado en consola ---
    Serial.println("\n--- [REPORTE DE SENSORES] ---");
    if (dhtOk) {
      Serial.printf("DHT22: Temp = %.1f °C | Hum = %.1f %%\n", temperatura, humedad);
    } else {
      Serial.println("DHT22: Error de lectura");
    }

    if (distancia >= 0) {
      Serial.printf("US-100: Distancia = %.1f cm\n", distancia);
    } else {
      Serial.println("US-100: Fuera de rango");
    }

    if (gpsFix) {
      Serial.printf("GPS: Lat = %.6f | Lng = %.6f | Alt = %.1f m | Sats = %d\n", 
                    latitud, longitud, altitud, satelites);
    } else {
      Serial.printf("GPS: Sin Fix satelital (Sats detectados: %d)\n", satelites);
    }

    // --- Construcción del JSON y envío HTTP POST ---
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");

      StaticJsonDocument<512> doc;
      doc["device_id"] = "ESP32_001";

      // Manejo de valores nulos o lecturas correctas
      if (dhtOk) {
        doc["temperature"] = serialized(String(temperatura, 2));
        doc["humidity"] = serialized(String(humedad, 2));
      } else {
        doc["temperature"] = nullptr;
        doc["humidity"] = nullptr;
      }

      if (distancia >= 0) {
        doc["distance_cm"] = serialized(String(distancia, 2));
      } else {
        doc["distance_cm"] = nullptr;
      }

      if (gpsFix) {
        doc["latitude"] = serialized(String(latitud, 6));
        doc["longitude"] = serialized(String(longitud, 6));
        doc["altitude_m"] = serialized(String(altitud, 1));
        doc["satellites"] = satelites;
      } else {
        doc["latitude"] = nullptr;
        doc["longitude"] = nullptr;
        doc["altitude_m"] = nullptr;
        doc["satellites"] = satelites;
      }

      String requestBody;
      serializeJson(doc, requestBody);

      Serial.print("Payload JSON: ");
      Serial.println(requestBody);

      int httpResponseCode = http.POST(requestBody);
      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("HTTP Code: %d | Servidor: %s\n", httpResponseCode, response.c_str());
      } else {
        Serial.printf("Error HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
      }

      http.end();
    } else {
      Serial.println("WiFi desconectado. Intentando reconectar...");
      WiFi.reconnect();
    }
  }
}