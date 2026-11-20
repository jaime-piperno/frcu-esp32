// Confort_Exterior.ino — Nodo Exterior: DHT11 + MQ-135 + LDR + KY-037
// Transmisor cliente HTTP hacia el Nodo Interior (Confort_Interior)
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensor.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "Confort_Interior"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "confort1234"
#endif

#ifndef SERVER_URL
#define SERVER_URL "http://192.168.4.1/api/nodo_remoto"
#endif

static uint32_t ultimoEnvioMs = 0;
static uint32_t ultimoIntentoWifi = 0;

static void conectarWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("# Conectando Nodo Exterior a Wi-Fi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n========================================================"));
  Serial.println(F("# SISTEMA DE CONFORT AMBIENTAL — NODO EXTERIOR"));
  Serial.println(F("# Sensores: DHT11 (Temp/Hum) + MQ-135 (Gas) + LDR + KY-037"));
  Serial.println(F("========================================================\n"));

  sensorIniciar();
  conectarWifi();
}

void loop() {
  uint32_t ahora = millis();

  // Reintento periódico de conexión Wi-Fi si se pierde
  if (WiFi.status() != WL_CONNECTED && (ahora - ultimoIntentoWifi >= RECONEXION_WIFI_MS)) {
    ultimoIntentoWifi = ahora;
    conectarWifi();
  }

  LecturaExterior exterior;
  if (sensorActualizar(exterior)) {
    // 1. Mostrar telemetría en Monitor Serie local
    Serial.printf("[EXTERIOR] Temp: %.1f °C | Hum: %.1f %% | IAQ (MQ135): %.0f | Gas: %.0f Ohm | Luz: %.0f %% | Ruido: %.1f %% (%u Hz)\n",
                  exterior.temp, exterior.hum, exterior.iaq, exterior.gas, exterior.ldr, exterior.sonido, exterior.pulsosSonido);

    // 2. Transmitir vía HTTP POST al Nodo Interior
    if (WiFi.status() == WL_CONNECTED && (ahora - ultimoEnvioMs >= INTERVALO_ENVIO_MS)) {
      ultimoEnvioMs = ahora;

      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      JsonDocument doc;
      doc["iaq"] = exterior.iaq;
      doc["temp"] = exterior.temp;
      doc["hum"] = exterior.hum;
      doc["gas"] = exterior.gas;
      doc["ldr"] = exterior.ldr;
      doc["sonido"] = exterior.sonido;
      doc["pulsos"] = exterior.pulsosSonido;
      doc["acc"] = exterior.acc;
      doc["conectado"] = true;

      String payload;
      serializeJson(doc, payload);

      int httpCode = http.POST(payload);
      if (httpCode > 0) {
        if (httpCode != HTTP_CODE_OK) {
          Serial.printf("# Servidor Interior respondió: %d\n", httpCode);
        }
      } else {
        Serial.printf("# Error de envío HTTP: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    }
  }
}

