// control.cpp — Procesamiento de deltas de confort entre Interior y Exterior
#include "control.h"

void calcularDeltasConfort(EstadoControl& s, const Lectura& interior, const Lectura& exterior) {
  s.remotoConectado = exterior.conectado;
  if (!exterior.conectado) {
    s.deltaIaq = 0.0f;
    s.deltaTemp = 0.0f;
    s.deltaHum = 0.0f;
    s.deltaLdr = 0.0f;
    s.deltaSonido = 0.0f;
    return;
  }
  s.deltaIaq = interior.iaq - exterior.iaq;
  s.deltaTemp = interior.temp - exterior.temp;
  s.deltaHum = interior.hum - exterior.hum;
  s.deltaLdr = interior.ldr - exterior.ldr;
  s.deltaSonido = interior.sonido - exterior.sonido;
}

