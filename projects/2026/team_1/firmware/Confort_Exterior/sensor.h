// sensor.h — Adquisición de variables para Nodo Exterior (DHT11 + MQ-135 + LDR + KY-037)
#pragma once
#include <stdint.h>

struct LecturaExterior {
  float iaq = 25.0f;          // Estimación IAQ (0–500) a partir de MQ-135
  float temp = 20.0f;         // Temperatura DHT11 [°C]
  float hum = 50.0f;          // Humedad DHT11 [% HR]
  float gas = 100000.0f;      // Resistencia estimada sensor MQ-135 [ohm]
  float ldr = 50.0f;          // Nivel de iluminación LDR [0–100 %]
  float sonido = 10.0f;       // Nivel de ruido KY-037 [0–100 %]
  uint32_t pulsosSonido = 0;  // Frecuencia media de pulsos (Hz)
  uint8_t acc = 3;            // Precisión
  bool conectado = true;
};

bool sensorIniciar();
bool sensorActualizar(LecturaExterior& out);

