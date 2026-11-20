// sensor.h — Definición de estructuras y lecturas del Nodo Interior
#pragma once
#include <stdint.h>

struct Lectura {
  float iaq = 0;              // Índice de calidad de aire (0–500)
  float siaq = 0;             // IAQ estático
  float eco2 = 0;             // CO2 equivalente estimado [ppm]
  float bvoc = 0;             // VOC equivalente estimado [ppm]
  float temp = 0;             // Temperatura compensada [°C]
  float hum = 0;              // Humedad compensada [%]
  float gas = 0;              // Resistencia del sensor de gas [ohm]
  float ldr = 0;              // Nivel de iluminación [0–100 %]
  float sonido = 0;           // Nivel de actividad sonora / ruido [0–100 %]
  uint32_t pulsosSonido = 0;  // Conteo de pulsos de conmutación por segundo (Hz)
  uint8_t precision = 3;      // Precisión (0–3)
  bool conectado = true;      // Indicador de conexión / validez
};

bool sensorIniciar();
bool sensorActualizar(Lectura& out);
bool sensorOk();
const char* sensorVersion();
float sensorLeerLdr();
float sensorLeerSonido(uint32_t& pulsosOut);

