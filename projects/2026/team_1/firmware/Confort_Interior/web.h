// web.h — Wi-Fi, Servidor Web HTTP, WebSocket y API para Confort_Interior
#pragma once
#include "control.h"
#include "sensor.h"

void webIniciar();
void webActualizar();
void webPublicar(const Lectura& local, const Lectura& remoto, const EstadoControl& s);
bool webObtenerRemoto(Lectura& out);

