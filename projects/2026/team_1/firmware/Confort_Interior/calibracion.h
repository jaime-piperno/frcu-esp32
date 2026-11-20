// calibracion.h — Corrección cruzada de las lecturas del Nodo Exterior.
//
// El Nodo Interior mide con un BME680 y el Exterior con un DHT11 y un MQ-135.
// Como no son el mismo sensor, sus lecturas no son directamente comparables:
// el DHT11 puede leer 29 °C donde el BME680 lee 26 °C.
//
// La calibración se hace con los dos nodos midiendo el MISMO ambiente:
// la diferencia media (interior - exterior) se guarda como corrección y se
// suma a cada lectura del Exterior antes de calcular deltas y mostrarla.
//
// Este módulo es puro (sin Arduino) para poder testearlo en la PC.
#pragma once
#include <stdint.h>
#include "sensor.h"

// Corrección aditiva aplicada al Nodo Exterior.
struct Correccion {
  float temp = 0.0f;  // °C
  float hum = 0.0f;   // % HR
  float iaq = 0.0f;   // puntos de IAQ

  bool activa() const { return temp != 0.0f || hum != 0.0f || iaq != 0.0f; }
};

// Aplica la corrección y recorta a rangos físicos.
Lectura correccionAplicar(const Lectura& remoto, const Correccion& c);

// Acumulador de calibración cruzada: promedia (local - remoto) sobre N muestras.
struct AcumuladorCalibracion {
  static constexpr uint16_t OBJETIVO = 30;  // 30 s si el exterior publica cada 1 s

  double sumaTemp = 0.0;
  double sumaHum = 0.0;
  double sumaIaq = 0.0;
  uint16_t n = 0;

  void reiniciar();
  // Registra una muestra. Descarta lecturas inválidas del nodo remoto.
  void muestrear(const Lectura& local, const Lectura& remotoCrudo);
  bool completo() const { return n >= OBJETIVO; }
  uint8_t progresoPct() const;
  // Media de las diferencias. Devuelve ceros si no hay muestras válidas.
  Correccion resultado() const;
};
