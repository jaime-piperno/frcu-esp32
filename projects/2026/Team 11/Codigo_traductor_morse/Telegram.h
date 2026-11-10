#pragma once
#include <Arduino.h>

namespace telegram {
enum class Tipo { Texto, Cancelar };
struct Entrada {
  Tipo tipo;
  uint32_t revision;
  char texto[81];
};
void iniciar();
void estado(bool modoTexto, bool ocupado, uint32_t revision);
bool recibir(Entrada& entrada);
void responder(const char* texto);
// No persiste ni imprime credenciales. Configuracion aplicada por tarea de red.
bool configurarWifi(const char* ssid, const char* clave);
bool configurarBot(const char* token, const char* chat);
void mostrarEstado();
}  // namespace telegram
