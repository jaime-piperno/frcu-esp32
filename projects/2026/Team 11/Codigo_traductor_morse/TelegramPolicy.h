#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Morse.h"

namespace telegram {
// Notificaciones: tres intentos espaciados, sin impedir consultar mensajes.
class Reintentos {
 public:
  bool disponible(uint32_t ahora) const {
    return intentos_ == 0 || (intentos_ < 3 && uint32_t(ahora - ultimo_) >= 3000);
  }
  void fallo(uint32_t ahora) {
    ++intentos_;
    ultimo_ = ahora;
  }
  bool agotados() const { return intentos_ >= 3; }
  void reiniciar() {
    intentos_ = 0;
    ultimo_ = 0;
  }

 private:
  unsigned intentos_ = 0;
  uint32_t ultimo_ = 0;
};
inline bool chatValido(const char* valor) {
  size_t i = valor[0] == '-' ? 1 : 0;
  if (!valor[i]) return false;
  for (; valor[i]; ++i)
    if (valor[i] < '0' || valor[i] > '9' || i >= 20) return false;
  return strcmp(valor, "0") != 0 && strcmp(valor, "-0") != 0;
}
inline bool tokenValido(const char* valor) {
  size_t largo = strlen(valor);
  if (largo < 25 || largo >= 128) return false;
  bool separador = false;
  for (size_t i = 0; i < largo; ++i) {
    char c = valor[i];
    if (c == ':' && !separador && i > 0 && i < largo - 1) {
      separador = true;
      continue;
    }
    if (!separador) {
      if (c < '0' || c > '9') return false;
    } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '-'))
      return false;
  }
  return separador;
}
// Cursor monotono: una actualizacion repetida no vuelve a ejecutar una salida.
class Cursor {
 public:
  bool aceptar(int64_t id) {
    if (id < siguiente_ || id < 0 || id == INT64_MAX) return false;
    siguiente_ = id + 1;
    return true;
  }
  int64_t siguiente() const { return siguiente_; }

 private:
  int64_t siguiente_ = 0;
};

static const int64_t CADUCIDAD_MENSAJE_S = 120;
inline bool mensajeVigente(int64_t fecha, int64_t ahora) {
  return fecha > 0 && ahora > 0 && fecha <= ahora + 30 && fecha >= ahora - CADUCIDAD_MENSAJE_S;
}
enum class Decision {
  Ignorar,
  Ayuda,
  Estado,
  Cancelar,
  Texto,
  SinTexto,
  OtroModo,
  Ocupado,
  Largo,
  Invalido,
  Vencido
};
inline Decision decidir(const char* autorizado, const char* origen, const char* texto,
                        bool modoTexto, bool ocupado, char (&normalizado)[morse::MAX_TEXTO + 1],
                        bool vigente = true) {
  normalizado[0] = '\0';
  if (!chatValido(autorizado) || strcmp(autorizado, origen) != 0) return Decision::Ignorar;
  if (!texto) return Decision::SinTexto;
  if (strcmp(texto, "/start") == 0 || strcmp(texto, "/ayuda") == 0) return Decision::Ayuda;
  if (strcmp(texto, "/estado") == 0) return Decision::Estado;
  if (!vigente) return Decision::Vencido;
  if (strcmp(texto, "/cancelar") == 0) return Decision::Cancelar;
  if (!modoTexto) return Decision::OtroModo;
  if (ocupado) return Decision::Ocupado;
  if (strlen(texto) > morse::MAX_TEXTO) return Decision::Largo;
  if (morse::normalizar(texto, normalizado) != morse::Resultado::Iniciado) {
    normalizado[0] = '\0';
    return Decision::Invalido;
  }
  return Decision::Texto;
}
}  // namespace telegram
