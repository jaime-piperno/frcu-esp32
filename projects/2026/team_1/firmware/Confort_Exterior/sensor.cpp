// sensor.cpp — Lectura de sensores del Nodo Exterior: DHT11 + MQ-135 + LDR + KY-037
#include <Arduino.h>
#include "config.h"
#include "dht11.h"
#include "sensor.h"

static DHT11 dht(PIN_DHT);
static volatile uint32_t pulsosSubVentana = 0;
static uint32_t ultimoMuestreoSonido = 0;
static uint32_t ultimoMuestreoCompleto = 0;
static uint32_t avisoDhtMs = 0;
static LecturaExterior ultima;

static void IRAM_ATTR isrSonido() {
  pulsosSubVentana++;
}

static float leerLdr() {
  uint32_t suma = 0;
  for (int i = 0; i < 4; i++) {
    suma += analogRead(PIN_LDR);
  }
  float crudo = (float)suma / 4.0f;
  float pct = (crudo / 4095.0f) * 100.0f;
  return pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct;
}

static float leerSonido(uint32_t& pulsosOut) {
  uint32_t ahora = millis();
  uint32_t dt = ahora - ultimoMuestreoSonido;
  if (dt == 0) dt = 1;

  noInterrupts();
  uint32_t conteoPulsos = pulsosSubVentana;
  pulsosSubVentana = 0;
  interrupts();
  ultimoMuestreoSonido = ahora;

  float frecuenciaHz = (conteoPulsos * 1000.0f) / dt;
  pulsosOut = (uint32_t)frecuenciaHz;

  float nivelRuidoPct = (frecuenciaHz / 250.0f) * 100.0f;
  if (nivelRuidoPct > 100.0f) nivelRuidoPct = 100.0f;

  return nivelRuidoPct;
}

static void leerMq135(float& iaqOut, float& gasOut) {
  uint32_t suma = 0;
  for (int i = 0; i < 4; i++) {
    suma += analogRead(PIN_MQ135);
  }
  float rawMQ = (float)suma / 4.0f;

  // Línea de base en aire limpio exterior (~600 sobre 4095 en ESP32 ADC)
  static float rawBase = 600.0f;
  if (rawMQ < rawBase) rawBase = (0.95f * rawBase) + (0.05f * rawMQ);
  else rawBase = (0.999f * rawBase) + (0.001f * rawMQ);

  float ratio = rawMQ / (rawBase > 0 ? rawBase : 600.0f);
  float iaq = ratio <= 1.0f ? 25.0f : 25.0f + (ratio - 1.0f) * 110.0f;
  if (iaq > 500.0f) iaq = 500.0f;

  float gasEquivOhm = (4095.0f - rawMQ) > 0.0f ? (rawMQ * 50.0f) : 150000.0f;

  iaqOut = iaq;
  gasOut = gasEquivOhm;
}

bool sensorIniciar() {
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_MQ135, INPUT);
  pinMode(PIN_SONIDO, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_SONIDO), isrSonido, CHANGE);
  dht.begin();

  ultimoMuestreoSonido = millis();
  ultimoMuestreoCompleto = millis();

  ultima.ldr = leerLdr();
  ultima.sonido = leerSonido(ultima.pulsosSonido);
  leerMq135(ultima.iaq, ultima.gas);
  ultima.temp = 20.0f;
  ultima.hum = 50.0f;
  ultima.acc = 3;
  ultima.conectado = true;

  Serial.println(F("# Sensores del Nodo Exterior inicializados (DHT11, MQ-135, LDR, KY-037)"));
  return true;
}

bool sensorActualizar(LecturaExterior& out) {
  uint32_t ahora = millis();
  if (ahora - ultimoMuestreoCompleto < 1000) return false;
  ultimoMuestreoCompleto = ahora;

  // 1. Muestreo de LDR y Sonido
  ultima.ldr = leerLdr();
  ultima.sonido = leerSonido(ultima.pulsosSonido);

  // 2. Muestreo de Gases con MQ-135
  leerMq135(ultima.iaq, ultima.gas);

  // 3. Muestreo de Temperatura y Humedad con DHT11
  float t = 0.0f, h = 0.0f;
  if (dht.leer(t, h)) {
    ultima.temp = t;
    ultima.hum = h;

    static bool modeloInformado = false;
    if (!modeloInformado) {
      modeloInformado = true;
      Serial.println(F("# DHT11 activo: resolucion 1 C / 1 % HR"));
    }
    avisoDhtMs = 0;
  } else {
    // Aviso periodico: antes el fallo era silencioso y el valor quedaba
    // congelado en el default sin ninguna pista en el monitor serie.
    if (avisoDhtMs == 0) avisoDhtMs = ahora;
    if (ahora - avisoDhtMs >= 10000) {
      avisoDhtMs = ahora;
      Serial.println(F("# DHT: lectura invalida (sin respuesta o checksum). Se mantiene el ultimo valor."));
    }
  }

  ultima.conectado = true;
  out = ultima;
  return true;
}

