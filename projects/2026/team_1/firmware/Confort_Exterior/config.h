// config.h — Configuración del Nodo Exterior (Confort Ambiental)
#pragma once
#include <Arduino.h>

// ---------------- Pines de Sensores (ESP32 DevKit) ----------------
// Sensor DHT11 (Temperatura y Humedad)
constexpr uint8_t PIN_DHT = 14;       // Pin digital con pullup

// Sensor de gases MQ-135 (Calidad de aire / CO2 / gases) en ADC1
// IMPORTANTE: Se conecta a ADC1 (GPIO 32–39) porque ADC2 se desactiva con Wi-Fi
constexpr uint8_t PIN_MQ135 = 35;     // ADC1_CH7 (solo entrada analógica)

// Sensor analógico LDR (Iluminación) en ADC1
constexpr uint8_t PIN_LDR = 34;       // ADC1_CH6 (solo entrada analógica)

// Sensor de sonido digital KY-037 (Confort acústico)
constexpr uint8_t PIN_SONIDO = 25;    // GPIO 25 con interrupción CHANGE

// ---------------- Parámetros de Envío y Red ----------------
constexpr uint32_t INTERVALO_ENVIO_MS = 1000;    // Envío cada 1 segundo
constexpr uint32_t RECONEXION_WIFI_MS = 5000;    // Reintento Wi-Fi cada 5 s

