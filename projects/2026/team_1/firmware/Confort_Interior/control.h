// control.h — Cálculo comparativo de deltas de confort ambiental
#pragma once
#include "sensor.h"

struct EstadoControl {
  float deltaIaq = 0;
  float deltaTemp = 0;
  float deltaHum = 0;
  float deltaLdr = 0;
  float deltaSonido = 0;
  bool remotoConectado = false;
};

void calcularDeltasConfort(EstadoControl& s, const Lectura& interior, const Lectura& exterior);

