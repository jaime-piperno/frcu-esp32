#include <Arduino.h>
#include "Morse.h"
#include "Telegram.h"

const int PIN_BOTON_MORSE = 4;  // Modulo de tres pines, activo HIGH.
const int PIN_LED_MORSE = 2;
const int PIN_BUZZER = 18;

const int PIN_BOTON_MODO = 25;
const int PIN_LED_MODO = 26;  // Encendido: texto -> Morse. Apagado: Morse -> texto.
const int FRECUENCIA_TONO = 1000;
const uint32_t INACTIVIDAD_MS = 5000;

enum class Modo { MorseATexto, TextoAMorse };
Modo modo = Modo::MorseATexto;
morse::Boton botonModo;
morse::Boton botonMorse;
morse::Transmisor transmisor(200);
morse::Calibracion calibracion;

uint32_t revisionModo = 0;
bool txDesdeTelegram = false;
bool salidaEncendida = false;
bool esperarSoltarMorse = true;
bool esperandoLetra = false;
uint32_t inicioPulsacion = 0;
uint32_t ultimoPulso = 0;
String patronRecibido;
String textoRecibido;

char lineaSerial[256] = {};
size_t largoLinea = 0;
bool lineaExcedida = false;
bool ignorarLF = false;

void ponerSalida(bool encendida) {
  if (encendida == salidaEncendida) return;
  salidaEncendida = encendida;
  digitalWrite(PIN_LED_MORSE, encendida ? HIGH : LOW);
  if (encendida)
    tone(PIN_BUZZER, FRECUENCIA_TONO);
  else
    noTone(PIN_BUZZER);
}

void limpiarRecepcion() {
  patronRecibido = "";
  textoRecibido = "";
  calibracion.reiniciar();
  esperandoLetra = false;
  esperarSoltarMorse = true;
}

void mostrarModo() {
  if (modo == Modo::MorseATexto) {
    Serial.println("\n[MODO] MORSE -> TEXTO | LED de modo APAGADO");
    Serial.println(
        "CALIBRACION: pulse un punto corto para fijar el ritmo. No forma parte del mensaje.");
    Serial.println(
        "Espere el aviso CALIBRACION LISTA; luego ingrese su mensaje empezando con punto o raya.");
    Serial.println("Tras 5 segundos de silencio se cierra el mensaje.");
  } else {
    Serial.println("\n[MODO] TEXTO -> MORSE | LED de modo ENCENDIDO");
    Serial.println("Escriba una frase y pulse Enter (A-Z, 0-9 y espacios; maximo 80 caracteres).");
    Serial.println("Punto: 200 ms; raya: 600 ms. /cancelar detiene la emision.");
  }
  Serial.println("El pulsador MODO alterna los modos y descarta cualquier mensaje en curso.");
}

void cambiarModo() {
  const bool cancelado = transmisor.activo();
  transmisor.cancelar();
  ponerSalida(false);
  ++revisionModo;
  if (txDesdeTelegram) telegram::responder("Emision cancelada por cambio de modo.");
  txDesdeTelegram = false;
  limpiarRecepcion();
  largoLinea = 0;
  lineaExcedida = false;
  ignorarLF = false;
  // Evita interpretar en el modo nuevo datos que ya estaban en el buffer serie.
  while (Serial.available() > 0) Serial.read();
  modo = modo == Modo::MorseATexto ? Modo::TextoAMorse : Modo::MorseATexto;
  digitalWrite(PIN_LED_MODO, modo == Modo::TextoAMorse ? HIGH : LOW);
  if (cancelado) Serial.println("\n[TX CANCELADA] Cambio de modo; LED Morse y buzzer apagados.");
  mostrarModo();
}

void finalizarRecepcion() {
  textoRecibido.trim();
  Serial.print("\n[MENSAJE FINAL TRADUCIDO] ");
  Serial.println(textoRecibido);
  Serial.println(
      "Listo para otro mensaje. Pulse un punto de calibracion; no se agregara al texto.");
  limpiarRecepcion();
}

void recibirMorse(morse::Flanco flanco, bool lecturaPulsada, uint32_t ahora) {
  // Al entrar al modo se exige soltar el boton antes de aceptar una pulsacion.
  if (esperarSoltarMorse) {
    if (!lecturaPulsada && !botonMorse.pulsado()) esperarSoltarMorse = false;
    return;
  }

  if (flanco == morse::Flanco::Pulsado) {
    inicioPulsacion = ahora;
    ponerSalida(true);
  } else if (flanco == morse::Flanco::Soltado) {
    ponerSalida(false);
    const uint32_t duracion = ahora - inicioPulsacion;
    char signo = '\0';
    const morse::PulsoRecibido resultado = calibracion.registrar(duracion, signo);
    if (resultado == morse::PulsoRecibido::Calibracion) {
      Serial.print("\n[CALIBRACION LISTA] Unidad: ");
      Serial.print(calibracion.unidad());
      Serial.println(" ms. Punto de referencia descartado. Ingrese ahora el mensaje.");
      return;
    }
    if (resultado == morse::PulsoRecibido::Dato) {
      // Seis signos bastan para representar un patron invalido sin crecer indefinidamente.
      if (patronRecibido.length() < 6) patronRecibido += signo;
      Serial.print(signo);
      esperandoLetra = true;
      ultimoPulso = ahora;
    }
  }

  if (lecturaPulsada || botonMorse.pulsado() || !calibracion.lista()) return;
  const uint32_t pausa = ahora - ultimoPulso;
  const uint32_t finLetra = max(uint32_t(800), calibracion.unidad() * 3);
  const uint32_t finPalabra = max(uint32_t(2000), calibracion.unidad() * 7);

  if (esperandoLetra && pausa >= finLetra) {
    const char letra = morse::decodificar(patronRecibido.c_str());
    textoRecibido += letra;
    patronRecibido = "";
    esperandoLetra = false;
    Serial.print("  ==> Letra: ");
    Serial.print(letra);
    Serial.print(" | TEXTO ACTUAL: ");
    Serial.println(textoRecibido);
    if (textoRecibido.length() >= morse::MAX_TEXTO) {
      Serial.println("[RX] Se alcanzo el limite de 80 caracteres.");
      finalizarRecepcion();
      return;
    }
  }

  if (!esperandoLetra && textoRecibido.length() > 0 && !textoRecibido.endsWith(" ") &&
      pausa >= finPalabra && pausa < INACTIVIDAD_MS) {
    if (textoRecibido.length() < morse::MAX_TEXTO - 1) textoRecibido += ' ';
  }
  if (!esperandoLetra && textoRecibido.length() > 0 && pausa >= INACTIVIDAD_MS) {
    finalizarRecepcion();
  }
}

void mostrarMorse(const char* texto) {
  Serial.print("[MORSE] ");
  for (size_t i = 0; texto[i] != '\0'; ++i) {
    if (texto[i] == ' ')
      Serial.print("/ ");
    else {
      Serial.print(morse::codificar(texto[i]));
      Serial.print(' ');
    }
  }
  Serial.println();
}

bool iniciarTexto(const char* texto, uint32_t ahora, bool desdeTelegram) {
  if (modo != Modo::TextoAMorse) {
    if (desdeTelegram)
      telegram::responder("Cambie a TEXTO -> MORSE con el pulsador MODO. No se emitio.");
    else
      Serial.println("[AVISO] Cambie de modo con el pulsador MODO para enviar texto.");
    return false;
  }
  if (strlen(texto) > morse::MAX_TEXTO) {
    if (desdeTelegram)
      telegram::responder("Limite: 80 caracteres. No se emitio.");
    else
      Serial.println("[ERROR] Limite: 80 caracteres. No se emitio.");
    return false;
  }
  const morse::Resultado resultado = transmisor.iniciar(texto, ahora);
  const char* aviso = nullptr;
  if (resultado == morse::Resultado::Iniciado) {
    txDesdeTelegram = desdeTelegram;
    Serial.print(desdeTelegram ? "\n[TX INICIO TELEGRAM] " : "\n[TX INICIO SERIE] ");
    Serial.println(transmisor.texto());
    mostrarMorse(transmisor.texto());
    if (desdeTelegram) telegram::responder("Mensaje aceptado. Emision iniciada.");
    return true;
  } else if (resultado == morse::Resultado::Ocupado)
    aviso = "Ocupado. Espere el fin o use /cancelar; mensaje descartado.";
  else if (resultado == morse::Resultado::Vacio)
    aviso = "Mensaje vacio; no se emitio.";
  else
    aviso = "Use A-Z, 0-9 y espacios, hasta 80 caracteres; sin tildes ni signos. No se emitio.";
  if (desdeTelegram)
    telegram::responder(aviso);
  else
    Serial.println(aviso);
  return false;
}

void cancelarTexto() {
  const bool activo = transmisor.activo();
  transmisor.cancelar();
  // Cancelar una transmision no debe silenciar una pulsacion en recepcion.
  if (modo == Modo::TextoAMorse) ponerSalida(false);
  if (txDesdeTelegram) telegram::responder("Emision cancelada.");
  txDesdeTelegram = false;
  Serial.println(activo ? "[TX CANCELADA]" : "[TX] No hay emision en curso.");
}

void procesarLinea(uint32_t ahora) {
  lineaSerial[largoLinea] = '\0';
  if (lineaExcedida)
    Serial.println("[ERROR] Linea demasiado larga; descartada.");
  else if (strncmp(lineaSerial, "/telegram ", 10) == 0 || strncmp(lineaSerial, "/wifi ", 6) == 0) {
    const bool wifi = lineaSerial[1] == 'w';
    char* primero = lineaSerial + (wifi ? 6 : 10);
    char* segundo = strchr(primero, '|');
    bool ok = false;
    if (segundo) {
      *segundo++ = '\0';
      ok = wifi ? telegram::configurarWifi(primero, segundo)
                : telegram::configurarBot(primero, segundo);
    }
    Serial.println(ok ? "[CONFIG] Solicitud aceptada. Datos privados no mostrados ni persistidos."
                      : "[CONFIG] Formato invalido o cola ocupada. Use /wifi SSID|CLAVE o "
                        "/telegram TOKEN|CHAT_ID.");
  } else if (strcmp(lineaSerial, "/red") == 0)
    telegram::mostrarEstado();
  else if (strcmp(lineaSerial, "/cancelar") == 0)
    cancelarTexto();
  else if (largoLinea)
    iniciarTexto(lineaSerial, ahora, false);
  memset(lineaSerial, 0, sizeof(lineaSerial));
  largoLinea = 0;
  lineaExcedida = false;
}

void atenderTelegram(uint32_t ahora) {
  telegram::Entrada entrada = {};
  if (!telegram::recibir(entrada)) return;
  if (entrada.revision != revisionModo) {
    telegram::responder("El modo cambio antes de atender el mensaje; descartado.");
    return;
  }
  if (entrada.tipo == telegram::Tipo::Cancelar) {
    bool eraRemoto = txDesdeTelegram;
    bool estabaActivo = transmisor.activo();
    cancelarTexto();
    if (!eraRemoto)
      telegram::responder(estabaActivo ? "Emision cancelada." : "No hay emision en curso.");
  } else
    iniciarTexto(entrada.texto, ahora, true);
}

void leerSerie(uint32_t ahora) {
  // Presupuesto por vuelta para que una entrada larga no retrase los pulsadores.
  for (size_t i = 0; i < 64 && Serial.available() > 0; ++i) {
    const char caracter = char(Serial.read());
    if (caracter == '\n' && ignorarLF) {
      ignorarLF = false;
      continue;
    }
    ignorarLF = false;
    if (caracter == '\r' || caracter == '\n') {
      procesarLinea(ahora);
      ignorarLF = caracter == '\r';
    } else if (!lineaExcedida) {
      if (largoLinea < sizeof(lineaSerial) - 1)
        lineaSerial[largoLinea++] = caracter;
      else
        lineaExcedida = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTON_MORSE, INPUT);
  pinMode(PIN_BOTON_MODO, INPUT_PULLUP);
  pinMode(PIN_LED_MORSE, OUTPUT);
  pinMode(PIN_LED_MODO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_MORSE, LOW);
  digitalWrite(PIN_LED_MODO, LOW);
  noTone(PIN_BUZZER);
  const uint32_t ahora = millis();
  botonModo.iniciar(digitalRead(PIN_BOTON_MODO) == LOW, ahora);
  botonMorse.iniciar(digitalRead(PIN_BOTON_MORSE) == HIGH, ahora);
  patronRecibido.reserve(6);
  textoRecibido.reserve(morse::MAX_TEXTO);
  Serial.println("\nTRADUCTOR MORSE BIDIRECCIONAL + TELEGRAM - GRUPO 12");
  mostrarModo();
  Serial.println(
      "[CONFIG] Wi-Fi por defecto Wokwi-GUEST. /wifi SSID|CLAVE /telegram TOKEN|CHAT_ID /red");
  telegram::iniciar();
}

void loop() {
  const uint32_t ahora = millis();
  const bool lecturaMorse = digitalRead(PIN_BOTON_MORSE) == HIGH;
  const morse::Flanco flancoMorse = botonMorse.actualizar(lecturaMorse, ahora);
  const morse::Flanco flancoModo = botonModo.actualizar(digitalRead(PIN_BOTON_MODO) == LOW, ahora);
  if (flancoModo == morse::Flanco::Pulsado) cambiarModo();

  leerSerie(ahora);
  atenderTelegram(ahora);
  if (modo == Modo::MorseATexto) {
    recibirMorse(flancoMorse, lecturaMorse, ahora);
  } else {
    const bool estabaActivo = transmisor.activo();
    transmisor.actualizar(ahora);
    ponerSalida(transmisor.encendido());
    if (estabaActivo && !transmisor.activo()) {
      Serial.println("[TX FIN] Listo para otra frase.");
      if (txDesdeTelegram) telegram::responder("Emision finalizada.");
      txDesdeTelegram = false;
    }
  }
  telegram::estado(modo == Modo::TextoAMorse, transmisor.activo(), revisionModo);
  delay(1);
}
