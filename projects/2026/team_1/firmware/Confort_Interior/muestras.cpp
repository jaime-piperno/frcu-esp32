// muestras.cpp — Serialización y promedios para las variables de confort.
#include "muestras.h"
#include <math.h>
#include <stdio.h>

const char CSV_CABECERA[] = "uptime_s,epoch,iaq,acc,eco2,bvoc,temp,hum,gas,ldr,sonido\n";

static uint16_t u16(double v) { return v <= 0 ? 0 : v >= 65535 ? 65535 : (uint16_t)lround(v); }
static int16_t i16(double v) { return v <= -32768 ? -32768 : v >= 32767 ? 32767 : (int16_t)lround(v); }
static uint8_t u8(double v) { return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)lround(v); }

Muestra empaquetar(const Lectura& l, uint32_t t) {
  Muestra m;
  m.t = t;
  m.gas = l.gas <= 0 ? 0 : (uint32_t)lround(l.gas);
  m.iaq10 = u16((l.iaq > 500 ? 500 : l.iaq) * 10);
  m.eco2 = u16(l.eco2);
  m.bvoc100 = u16(l.bvoc * 100);
  m.temp100 = i16(l.temp * 100);
  m.hum100 = u16(l.hum * 100);
  m.prec = l.precision;
  m.ldr = u8(l.ldr);
  m.sonido = u8(l.sonido);
  m.reservado[0] = 0;
  m.reservado[1] = 0;
  m.reservado[2] = 0;
  return m;
}

size_t formatearFila(char* buf, size_t tam, const Muestra& m, uint32_t epochArranque) {
  char epoch[12] = "";
  if (epochArranque) snprintf(epoch, sizeof epoch, "%lu", (unsigned long)(epochArranque + m.t));
  int n = snprintf(buf, tam, "%lu,%s,%.1f,%u,%u,%.2f,%.2f,%.2f,%lu,%u,%u\n",
                   (unsigned long)m.t, epoch, m.iaq10 / 10.0, m.prec, m.eco2,
                   m.bvoc100 / 100.0, m.temp100 / 100.0, m.hum100 / 100.0,
                   (unsigned long)m.gas, m.ldr, m.sonido);
  return n < 0 ? 0 : (size_t)n < tam ? (size_t)n : tam - 1;
}

void Promedio::sumar(const Lectura& l) {
  const float v[8] = {l.iaq, l.eco2, l.bvoc, l.temp, l.hum, l.gas, l.ldr, l.sonido};
  for (int i = 0; i < 8; i++) suma[i] += v[i];
  if (l.precision < precMin) precMin = l.precision;
  n++;
}

Lectura Promedio::resultado() const {
  Lectura l;
  if (n == 0) return l;
  l.iaq = suma[0] / n;
  l.eco2 = suma[1] / n;
  l.bvoc = suma[2] / n;
  l.temp = suma[3] / n;
  l.hum = suma[4] / n;
  l.gas = suma[5] / n;
  l.ldr = suma[6] / n;
  l.sonido = suma[7] / n;
  l.precision = precMin;
  l.conectado = true;
  return l;
}

