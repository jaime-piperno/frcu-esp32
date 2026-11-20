// config.h — Configuración del Nodo Interior (Confort Ambiental)
#pragma once
#include <Arduino.h>

// ---------------- Pines de Sensores (ESP32 DevKit) ----------------
// I2C para sensor Bosch BME680 (Calidad de aire, Temperatura, Humedad)
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t BME680_ADDR = 0x76; // SDO a GND (0x77 si SDO a 3V3)

// Sensor analógico LDR (Iluminación / Confort lumínico) en ADC1
constexpr uint8_t PIN_LDR = 34;       // ADC1_CH6 (solo entrada)

// Sensor de sonido digital KY-037 (Confort acústico)
constexpr uint8_t PIN_SONIDO = 25;    // GPIO 25 con interrupción CHANGE

// ---------------- Calibración y offsets ----------------
constexpr float TEMP_OFFSET_C = 0.0f;

// ---------------- Tiempos y red ----------------
constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
constexpr uint32_t SENSOR_REINTENTO_MS = 10000;
constexpr uint32_t REMOTO_TIMEOUT_MS = 15000;
