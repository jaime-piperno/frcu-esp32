// sensor.cpp — BME680 (Adafruit), Fotorresistor LDR y Micrófono KY-037 (Nodo Interior)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include "config.h"
#include "sensor.h"

// ---------------- Adquisición de Sonido (KY-037) ----------------
static volatile uint32_t pulsosSubVentana = 0;
static uint32_t ultimoMuestreoSonido = 0;

static void IRAM_ATTR isrSonido() {
  pulsosSubVentana++;
}

float sensorLeerLdr() {
  uint32_t suma = 0;
  for (int i = 0; i < 4; i++) {
    suma += analogRead(PIN_LDR);
  }
  float crudo = (float)suma / 4.0f;
  float pct = (crudo / 4095.0f) * 100.0f;
  return pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct;
}

float sensorLeerSonido(uint32_t& pulsosOut) {
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

static Adafruit_BME680 bme;
static Lectura ultima;
static bool ok = false;
static uint32_t ultimoIntento = 0;
static uint32_t ultimoMuestreoCompleto = 0;
static char versionInfo[32] = "Adafruit BME680";
static float gasBase = 150000.0f;

static float calcularIaq(float gasOhm, float humPct) {
  if (gasOhm <= 0.0f) return 25.0f;

  float humScore = 0.0f;
  if (humPct >= 38.0f && humPct <= 42.0f) {
    humScore = 25.0f;
  } else if (humPct < 38.0f) {
    humScore = (humPct / 38.0f) * 25.0f;
  } else {
    humScore = ((100.0f - humPct) / (100.0f - 42.0f)) * 25.0f;
  }
  if (humScore < 0.0f) humScore = 0.0f;

  if (gasOhm > gasBase) gasBase = (0.95f * gasBase) + (0.05f * gasOhm);
  else gasBase = (0.999f * gasBase) + (0.001f * gasOhm);

  float ratio = gasOhm / gasBase;
  if (ratio > 1.0f) ratio = 1.0f;
  float gasScore = ratio * 75.0f;

  float airQualityScore = humScore + gasScore;
  float iaq = (100.0f - airQualityScore) * 5.0f;
  if (iaq < 10.0f) iaq = 10.0f;
  if (iaq > 500.0f) iaq = 500.0f;
  return iaq;
}

bool sensorIniciar() {
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_SONIDO, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_SONIDO), isrSonido, CHANGE);
  ultimoMuestreoSonido = millis();
  ultimoMuestreoCompleto = millis();
  ultimoIntento = millis();

  ultima.ldr = sensorLeerLdr();
  ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
  ultima.iaq = 25.0f;
  ultima.temp = 22.0f;
  ultima.hum = 50.0f;
  ultima.gas = 150000.0f;
  ultima.precision = 3;
  ultima.conectado = true;

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!bme.begin(0x76)) {
    Serial.println(F("# No se encontró BME680 en 0x76. Probando en 0x77..."));
    if (!bme.begin(0x77)) {
      Serial.println(F("# AVISO: BME680 no detectado. Monitoreando LDR y Micrófono mientras se reintenta..."));
      ok = false;
      return false;
    } else {
      Serial.println(F("# BME680 detectado correctamente en 0x77."));
      ok = true;
    }
  } else {
    Serial.println(F("# BME680 detectado correctamente en 0x76."));
    ok = true;
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_NONE);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  return true;
}

bool sensorActualizar(Lectura& out) {
  uint32_t ahora = millis();
  if (ahora - ultimoMuestreoCompleto < 1000) return false;
  ultimoMuestreoCompleto = ahora;

  // 1. Lectura de LDR y Sonido
  ultima.ldr = sensorLeerLdr();
  ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
  ultima.conectado = true;

  // 2. Lectura BME680
  if (!ok) {
    if (ahora - ultimoIntento >= SENSOR_REINTENTO_MS) {
      ultimoIntento = ahora;
      if (bme.begin(0x76) || bme.begin(0x77)) {
        bme.setTemperatureOversampling(BME680_OS_8X);
        bme.setHumidityOversampling(BME680_OS_2X);
        bme.setPressureOversampling(BME680_OS_NONE);
        bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme.setGasHeater(320, 150);
        ok = true;
        Serial.println(F("# BME680 reconectado exitosamente."));
      }
    }
    out = ultima;
    return true;
  }

  if (bme.performReading()) {
    ultima.temp = bme.temperature - TEMP_OFFSET_C;
    ultima.hum = bme.humidity;
    ultima.gas = bme.gas_resistance;
    ultima.iaq = calcularIaq(ultima.gas, ultima.hum);
    ultima.siaq = ultima.iaq * 0.95f;
    ultima.eco2 = 400.0f + 4.5f * (ultima.iaq > 25.0f ? ultima.iaq - 25.0f : 0.0f);
    ultima.bvoc = 0.5f * exp(ultima.iaq / 120.0f);
    ultima.precision = 3;
  }

  out = ultima;
  return true;
}

bool sensorOk() { return ok; }
const char* sensorVersion() { return versionInfo; }

