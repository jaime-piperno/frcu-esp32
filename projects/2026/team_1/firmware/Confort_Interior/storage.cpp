// storage.cpp — Buffers de historial en RAM y sincronización de hilos.
#include "storage.h"

Anillo<Muestra, N_HIST> historial;
Anillo<Muestra, N_VIVO> vivo;
uint32_t epochArranque = 0;

static Promedio minuto;
static uint32_t inicioMinutoS = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void storageBloquear() { portENTER_CRITICAL(&lock); }
void storageDesbloquear() { portEXIT_CRITICAL(&lock); }

void storageIniciar() {
  // Inicialización de almacenamiento en memoria RAM
}

uint32_t uptimeS() { return (uint32_t)(esp_timer_get_time() / 1000000LL); }

void storageAgregar(const Lectura& l, uint32_t ahoraS) {
  Muestra m = empaquetar(l, ahoraS);
  bool cerrarMinuto = !minuto.vacio() && ahoraS - inicioMinutoS >= 60;
  Muestra prom;
  if (cerrarMinuto) prom = empaquetar(minuto.resultado(), inicioMinutoS);
  storageBloquear();
  vivo.agregar(m);
  if (cerrarMinuto) historial.agregar(prom);
  storageDesbloquear();
  if (cerrarMinuto || minuto.vacio()) {
    if (cerrarMinuto) minuto.reiniciar();
    inicioMinutoS = ahoraS;
  }
  minuto.sumar(l);
}

