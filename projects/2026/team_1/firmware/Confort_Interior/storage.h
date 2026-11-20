// storage.h — Buffers de historial en RAM y sincronización de hilos.
#pragma once
#include <Arduino.h>
#include "muestras.h"

constexpr size_t N_HIST = 1440;  // 24 h x 1 muestra por minuto
constexpr size_t N_VIVO = 900;   // 15 min x 1 muestra por segundo

extern Anillo<Muestra, N_HIST> historial;
extern Anillo<Muestra, N_VIVO> vivo;
extern uint32_t epochArranque;

void storageIniciar();
uint32_t uptimeS();
void storageAgregar(const Lectura& l, uint32_t ahoraS);
void storageBloquear();
void storageDesbloquear();

