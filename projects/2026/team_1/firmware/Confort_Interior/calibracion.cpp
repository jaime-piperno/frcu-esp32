// calibracion.cpp — Corrección cruzada de las lecturas del Nodo Exterior.
#include "calibracion.h"

static float recortar(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

Lectura correccionAplicar(const Lectura& remoto, const Correccion& c) {
  Lectura r = remoto;
  if (!remoto.conectado) return r;

  r.temp = recortar(remoto.temp + c.temp, -40.0f, 80.0f);
  r.hum = recortar(remoto.hum + c.hum, 0.0f, 100.0f);
  r.iaq = recortar(remoto.iaq + c.iaq, 0.0f, 500.0f);
  return r;
}

void AcumuladorCalibracion::reiniciar() {
  sumaTemp = 0.0;
  sumaHum = 0.0;
  sumaIaq = 0.0;
  n = 0;
}

void AcumuladorCalibracion::muestrear(const Lectura& local, const Lectura& remotoCrudo) {
  if (!remotoCrudo.conectado) return;

  // Descarta lecturas fuera de rango físico: un valor así indica fallo del
  // sensor remoto y contaminaría el promedio.
  if (remotoCrudo.temp < -40.0f || remotoCrudo.temp > 80.0f) return;
  if (remotoCrudo.hum < 0.0f || remotoCrudo.hum > 100.0f) return;
  if (remotoCrudo.iaq < 0.0f || remotoCrudo.iaq > 500.0f) return;

  sumaTemp += (double)local.temp - (double)remotoCrudo.temp;
  sumaHum += (double)local.hum - (double)remotoCrudo.hum;
  sumaIaq += (double)local.iaq - (double)remotoCrudo.iaq;
  n++;
}

uint8_t AcumuladorCalibracion::progresoPct() const {
  uint32_t pct = (uint32_t)n * 100u / OBJETIVO;
  return pct > 100u ? 100 : (uint8_t)pct;
}

Correccion AcumuladorCalibracion::resultado() const {
  Correccion c;
  if (n == 0) return c;
  c.temp = (float)(sumaTemp / (double)n);
  c.hum = (float)(sumaHum / (double)n);
  c.iaq = (float)(sumaIaq / (double)n);
  return c;
}
