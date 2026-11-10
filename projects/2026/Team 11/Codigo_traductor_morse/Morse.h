#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace morse {

static const size_t MAX_TEXTO = 80;
static const uint32_t ANTIRREBOTE_MS = 50;

struct Codigo {
  char letra;
  const char* patron;
};

static const Codigo TABLA[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},     {'F', "..-."},
    {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
    {'S', "..."},   {'T', "-"},     {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},  {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'0', "-----"}};

inline const char* codificar(char letra) {
  for (const Codigo& codigo : TABLA) {
    if (codigo.letra == letra) return codigo.patron;
  }
  return nullptr;
}

inline char decodificar(const char* patron) {
  for (const Codigo& codigo : TABLA) {
    if (strcmp(codigo.patron, patron) == 0) return codigo.letra;
  }
  return '?';
}

enum class Flanco { Ninguno, Pulsado, Soltado };

enum class PulsoRecibido { Ignorado, Calibracion, Dato };

class Calibracion {
 public:
  void reiniciar() { unidad_ = 0; }
  bool lista() const { return unidad_ != 0; }
  uint32_t unidad() const { return unidad_; }
  PulsoRecibido registrar(uint32_t duracion, char& signo) {
    signo = '\0';
    if (duracion < 60) return PulsoRecibido::Ignorado;
    if (!lista()) {
      unidad_ = duracion;
      return PulsoRecibido::Calibracion;  // Referencia de ritmo, nunca un dato.
    }
    signo = uint64_t(duracion) * 10 < uint64_t(unidad_) * 18 ? '.' : '-';
    return PulsoRecibido::Dato;
  }

 private:
  uint32_t unidad_ = 0;
};

class Boton {
 public:
  void iniciar(bool pulsado, uint32_t ahora) {
    lectura_ = estable_ = pulsado;
    cambio_ = ahora;
  }

  Flanco actualizar(bool pulsado, uint32_t ahora) {
    if (pulsado != lectura_) {
      lectura_ = pulsado;
      cambio_ = ahora;
    }
    if (lectura_ != estable_ && uint32_t(ahora - cambio_) >= ANTIRREBOTE_MS) {
      estable_ = lectura_;
      return estable_ ? Flanco::Pulsado : Flanco::Soltado;
    }
    return Flanco::Ninguno;
  }

  bool pulsado() const { return estable_; }

 private:
  bool lectura_ = false;
  bool estable_ = false;
  uint32_t cambio_ = 0;
};

enum class Resultado { Iniciado, Vacio, NoAdmitido, DemasiadoLargo, Ocupado };

inline Resultado normalizar(const char* entrada, char (&salida)[MAX_TEXTO + 1]) {
  size_t largo = 0;
  bool espacio = false;
  salida[0] = '\0';
  for (size_t i = 0; entrada[i] != '\0'; ++i) {
    char letra = entrada[i];
    if (letra == ' ') {
      espacio = largo > 0;
      continue;
    }
    if (letra >= 'a' && letra <= 'z') letra = char(letra - 'a' + 'A');
    if (codificar(letra) == nullptr) return Resultado::NoAdmitido;
    if (largo + (espacio ? 2 : 1) > MAX_TEXTO) return Resultado::DemasiadoLargo;
    if (espacio) salida[largo++] = ' ';
    salida[largo++] = letra;
    espacio = false;
  }
  salida[largo] = '\0';
  return largo ? Resultado::Iniciado : Resultado::Vacio;
}

class Transmisor {
 public:
  // Un punto dura una unidad; raya 3; pausas entre signos/letras/palabras 1/3/7.
  explicit Transmisor(uint32_t unidad = 200) : unidad_(unidad) {}

  Resultado iniciar(const char* entrada, uint32_t ahora) {
    if (activo_) return Resultado::Ocupado;

    char candidato[MAX_TEXTO + 1] = {};
    const Resultado resultado = normalizar(entrada, candidato);
    if (resultado != Resultado::Iniciado) return resultado;

    memcpy(texto_, candidato, strlen(candidato) + 1);
    letra_ = signo_ = 0;
    patron_ = codificar(texto_[0]);
    activo_ = encendido_ = true;
    inicio_ = ahora;
    duracion_ = duracionSigno();
    return Resultado::Iniciado;
  }

  void actualizar(uint32_t ahora) {
    if (!activo_ || uint32_t(ahora - inicio_) < duracion_) return;
    inicio_ = ahora;

    if (!encendido_) {
      encendido_ = true;
      duracion_ = duracionSigno();
      return;
    }

    encendido_ = false;
    ++signo_;
    if (patron_[signo_] != '\0') {
      duracion_ = unidad_;
      return;
    }

    ++letra_;
    if (texto_[letra_] == '\0') {
      activo_ = false;
      return;
    }
    duracion_ = 3 * unidad_;
    if (texto_[letra_] == ' ') {
      ++letra_;
      duracion_ = 7 * unidad_;
    }
    patron_ = codificar(texto_[letra_]);
    signo_ = 0;
  }

  void cancelar() { activo_ = encendido_ = false; }
  bool activo() const { return activo_; }
  bool encendido() const { return encendido_; }
  const char* texto() const { return texto_; }

 private:
  uint32_t duracionSigno() const { return (patron_[signo_] == '-' ? 3 : 1) * unidad_; }
  char texto_[MAX_TEXTO + 1] = {};
  const char* patron_ = nullptr;
  size_t letra_ = 0;
  size_t signo_ = 0;
  uint32_t unidad_;
  uint32_t inicio_ = 0;
  uint32_t duracion_ = 0;
  bool activo_ = false;
  bool encendido_ = false;
};

}  // namespace morse
