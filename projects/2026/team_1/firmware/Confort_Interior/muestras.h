// muestras.h — Almacenamiento compacto de variables de confort y serialización CSV.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sensor.h"

struct Muestra {
  uint32_t t;           // segundos desde arranque (4 B)
  uint32_t gas;         // ohm (4 B)
  uint16_t iaq10;       // IAQ x 10 (2 B)
  uint16_t eco2;        // ppm (2 B)
  uint16_t bvoc100;     // ppm x 100 (2 B)
  int16_t temp100;      // °C x 100 (2 B)
  uint16_t hum100;      // % x 100 (2 B)
  uint8_t prec;         // precisión (1 B)
  uint8_t ldr;          // Iluminación % (1 B)
  uint8_t sonido;       // Actividad sonora % (1 B)
  uint8_t reservado[3]; // alineación a 24 bytes (3 B)
};
static_assert(sizeof(Muestra) == 24, "Muestra debe ocupar 24 bytes");

template <typename T, size_t N>
struct Anillo {
  T d[N];
  uint32_t total = 0;
  void agregar(const T& x) { d[total % N] = x; total++; }
  uint32_t primero() const { return total > N ? total - N : 0; }
  const T& en(uint32_t seq) const { return d[seq % N]; }
};

struct Promedio {
  double suma[8] = {};
  uint16_t n = 0;
  uint8_t precMin = 3;
  void sumar(const Lectura& l);
  bool vacio() const { return n == 0; }
  Lectura resultado() const;
  void reiniciar() { *this = Promedio(); }
};

Muestra empaquetar(const Lectura& l, uint32_t t);

extern const char CSV_CABECERA[];
size_t formatearFila(char* buf, size_t tam, const Muestra& m, uint32_t epochArranque);

