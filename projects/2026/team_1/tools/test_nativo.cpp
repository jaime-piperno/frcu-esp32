// Tests nativos (en PC, sin ESP32) de la lógica pura del firmware (Etapa 2 - Confort).
// Compilar y correr en entornos con GCC/Clang:
//   g++ -std=c++17 -Wall -Wextra -I firmware/Confort_Interior tools/test_nativo.cpp \
//       firmware/Confort_Interior/control.cpp firmware/Confort_Interior/muestras.cpp \
//       firmware/Confort_Interior/calibracion.cpp -o build/test_nativo && ./build/test_nativo
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "calibracion.h"
#include "control.h"
#include "muestras.h"

Config config;

static bool cerca(double a, double b, double tol) { return fabs(a - b) <= tol; }

static void testBandas() {
  assert(bandaBosch(0) == 0 && bandaBosch(50) == 0 && bandaBosch(51) == 1);
  assert(bandaBosch(100) == 1 && bandaBosch(150) == 2 && bandaBosch(200) == 3);
  assert(bandaBosch(250) == 4 && bandaBosch(350) == 5 && bandaBosch(351) == 6);
}

static void testDeltasConfort() {
  Lectura aula, pasillo;
  aula.iaq = 120.0f;
  aula.temp = 24.5f;
  aula.hum = 45.0f;
  aula.ldr = 80.0f;
  aula.sonido = 40.0f;
  aula.conectado = true;

  pasillo.iaq = 60.0f;
  pasillo.temp = 21.0f;
  pasillo.hum = 50.0f;
  pasillo.ldr = 60.0f;
  pasillo.sonido = 25.0f;
  pasillo.conectado = true;

  EstadoControl s;
  calcularDeltasConfort(s, aula, pasillo);

  assert(s.remotoConectado == true);
  assert(cerca(s.deltaIaq, 60.0f, 0.01));
  assert(cerca(s.deltaTemp, 3.5f, 0.01));
  assert(cerca(s.deltaHum, -5.0f, 0.01));
  assert(cerca(s.deltaLdr, 20.0f, 0.01));
  assert(cerca(s.deltaSonido, 15.0f, 0.01));
}

static void testValidacion() {
  Config c;
  assert(validarConfig(c) == nullptr);
  c.silencioMin = 0; assert(validarConfig(c) != nullptr);
  c.silencioMin = 130; assert(validarConfig(c) != nullptr);
}

static void testBoton() {
  EstadoBoton b;
  assert(botonPaso(b, true, 0) == Pulsacion::NINGUNA);
  assert(botonPaso(b, false, 10) == Pulsacion::NINGUNA);
  assert(botonPaso(b, false, 100) == Pulsacion::NINGUNA);

  botonPaso(b, true, 1000);
  assert(botonPaso(b, true, 1040) == Pulsacion::NINGUNA);
  botonPaso(b, false, 1300);
  assert(botonPaso(b, false, 1340) == Pulsacion::CORTA);

  botonPaso(b, true, 5000);
  botonPaso(b, true, 5040);
  assert(botonPaso(b, true, 6539) == Pulsacion::NINGUNA);
  assert(botonPaso(b, true, 6540) == Pulsacion::LARGA);
  assert(botonPaso(b, true, 7000) == Pulsacion::NINGUNA);
  botonPaso(b, false, 7100);
  assert(botonPaso(b, false, 7140) == Pulsacion::NINGUNA);
}

static void testSimular() {
  Lectura aula = simular(0, false);
  assert(aula.iaq >= 0 && aula.iaq <= 500);
  assert(aula.ldr >= 0 && aula.ldr <= 100);
  assert(aula.sonido >= 0 && aula.sonido <= 100);

  Lectura pasillo = simular(0, true);
  assert(pasillo.iaq >= 0 && pasillo.iaq <= 500);
  assert(pasillo.ldr >= 0 && pasillo.ldr <= 100);
  assert(pasillo.sonido >= 0 && pasillo.sonido <= 100);
}

static void testAnillo() {
  Anillo<int, 3> a;
  assert(a.primero() == 0 && a.total == 0);
  a.agregar(10); a.agregar(11);
  assert(a.primero() == 0 && a.total == 2 && a.en(0) == 10 && a.en(1) == 11);
  a.agregar(12); a.agregar(13);
  assert(a.primero() == 1 && a.en(1) == 11 && a.en(3) == 13);
}

static void testEmpaquetarYCsv() {
  Lectura l;
  l.iaq = 123.46f; l.eco2 = 900; l.bvoc = 1.234f;
  l.temp = -5.5f; l.hum = 45.67f; l.gas = 123456;
  l.ldr = 75.0f; l.sonido = 35.0f; l.precision = 3;
  Muestra m = empaquetar(l, 60);
  assert(m.iaq10 == 1235 && m.temp100 == -550 && m.gas == 123456);
  assert(m.ldr == 75 && m.sonido == 35);

  char fila[200];
  formatearFila(fila, sizeof fila, m, 0);
  assert(strcmp(fila, "60,,123.5,3,900,1.23,-5.50,45.67,123456,75,35\n") == 0);
  formatearFila(fila, sizeof fila, m, 1700000000);
  assert(strcmp(fila, "60,1700000060,123.5,3,900,1.23,-5.50,45.67,123456,75,35\n") == 0);

  int comas = 0;
  for (const char* p = CSV_CABECERA; *p; p++) comas += *p == ',';
  assert(comas == 10); // 11 columnas en CSV Etapa 2 Confort (sin presión ni marcadores)
}

static void testPromedio() {
  Promedio p;
  assert(p.vacio());
  Lectura a; a.iaq = 100; a.temp = 20; a.ldr = 80; a.sonido = 40; a.precision = 3;
  Lectura b; b.iaq = 200; b.temp = 22; b.ldr = 60; b.sonido = 60; b.precision = 1;
  p.sumar(a); p.sumar(b);
  Lectura r = p.resultado();
  assert(cerca(r.iaq, 150, 1e-3) && cerca(r.temp, 21, 1e-3) && cerca(r.ldr, 70, 1e-3) && cerca(r.sonido, 50, 1e-3) && r.precision == 1);
  p.reiniciar();
  assert(p.vacio());
}

static void testCorreccion() {
  Lectura rem;
  rem.temp = 29.0f;
  rem.hum = 56.0f;
  rem.iaq = 300.0f;
  rem.conectado = true;

  Correccion c;
  c.temp = -3.0f;
  c.hum = 6.0f;
  c.iaq = -5.0f;

  Lectura r = correccionAplicar(rem, c);
  assert(cerca(r.temp, 26.0f, 1e-3));
  assert(cerca(r.hum, 62.0f, 1e-3));
  assert(cerca(r.iaq, 295.0f, 1e-3));
  assert(c.activa());

  // Recorte a rangos fisicos
  Correccion extrema;
  extrema.temp = -100.0f;
  extrema.hum = 100.0f;
  extrema.iaq = -1000.0f;
  Lectura r2 = correccionAplicar(rem, extrema);
  assert(cerca(r2.temp, -40.0f, 1e-3));
  assert(cerca(r2.hum, 100.0f, 1e-3));
  assert(cerca(r2.iaq, 0.0f, 1e-3));

  // Sin conexion no se modifica nada
  Lectura off;
  off.conectado = false;
  off.temp = 29.0f;
  Lectura r3 = correccionAplicar(off, c);
  assert(cerca(r3.temp, 29.0f, 1e-3));

  // Correccion neutra no cambia valores
  assert(!Correccion().activa());
}

static void testAcumuladorCalibracion() {
  AcumuladorCalibracion a;
  assert(!a.completo());
  assert(a.progresoPct() == 0);

  Lectura local;
  local.temp = 26.0f;
  local.hum = 62.0f;
  local.iaq = 120.0f;

  Lectura rem;
  rem.temp = 29.0f;
  rem.hum = 56.0f;
  rem.iaq = 150.0f;
  rem.conectado = true;

  a.muestrear(local, rem);
  a.muestrear(local, rem);

  Correccion c = a.resultado();
  assert(cerca(c.temp, -3.0f, 1e-3));
  assert(cerca(c.hum, 6.0f, 1e-3));
  assert(cerca(c.iaq, -30.0f, 1e-3));

  // Lectura fuera de rango fisico: se descarta
  Lectura mala;
  mala.conectado = true;
  mala.temp = 999.0f;
  mala.hum = 50.0f;
  mala.iaq = 100.0f;
  a.muestrear(local, mala);
  assert(a.n == 2);

  // Nodo remoto desconectado: se descarta
  Lectura off;
  off.conectado = false;
  a.muestrear(local, off);
  assert(a.n == 2);

  for (int i = 0; i < (int)AcumuladorCalibracion::OBJETIVO; i++) a.muestrear(local, rem);
  assert(a.completo());
  assert(a.progresoPct() == 100);

  a.reiniciar();
  assert(!a.completo() && a.n == 0);
  Correccion cero = a.resultado();
  assert(cero.temp == 0.0f && cero.hum == 0.0f && cero.iaq == 0.0f);
}

int main() {
  testBandas();
  testDeltasConfort();
  testValidacion();
  testBoton();
  testSimular();
  testAnillo();
  testEmpaquetarYCsv();
  testPromedio();
  testCorreccion();
  testAcumuladorCalibracion();
  puts("OK: Todos los tests nativos de Confort (Etapa 2) pasaron exitosamente!");
  return 0;
}
