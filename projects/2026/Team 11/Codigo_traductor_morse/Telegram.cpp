#include "Telegram.h"
#include "TelegramPolicy.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <time.h>

#if __has_include("Secretos.h")
#include "Secretos.h"
#endif
#ifndef MORSE_WIFI_SSID
#define MORSE_WIFI_SSID "Wokwi-GUEST"
#endif
#ifndef MORSE_WIFI_PASSWORD
#define MORSE_WIFI_PASSWORD ""
#endif
#ifndef MORSE_TELEGRAM_TOKEN
#define MORSE_TELEGRAM_TOKEN ""
#endif
#ifndef MORSE_TELEGRAM_CHAT
#define MORSE_TELEGRAM_CHAT ""
#endif

namespace telegram {
namespace {
struct Configuracion {
  bool wifi;
  char ssid[33];
  char clave[65];
  char token[128];
  char chat[24];
};
struct Respuesta {
  char texto[224];
};
struct Estado {
  bool modoTexto;
  bool ocupado;
  uint32_t revision;
};
QueueHandle_t configuraciones = nullptr, entradas = nullptr, respuestas = nullptr;
portMUX_TYPE cerrojo = portMUX_INITIALIZER_UNLOCKED;
Estado actual = {};
bool conectado = false, botConfigurado = false, listo = false;
bool tareaIniciada = false;

Estado leerEstado() {
  portENTER_CRITICAL(&cerrojo);
  Estado copia = actual;
  portEXIT_CRITICAL(&cerrojo);
  return copia;
}
void indicarRed(bool wifi, bool bot, bool preparado) {
  portENTER_CRITICAL(&cerrojo);
  conectado = wifi;
  botConfigurado = bot;
  listo = preparado;
  portEXIT_CRITICAL(&cerrojo);
}

cJSON* solicitar(const char* token, const char* metodo, cJSON* parametros) {
  char* cuerpo = cJSON_PrintUnformatted(parametros);
  if (!cuerpo) return nullptr;
  NetworkClientSecure cliente;
  cliente.useBuiltinCACertBundle();
  cliente.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  http.useHTTP10(true);
  String url = String("https://api.telegram.org/bot") + token + "/" + metodo;
  bool abierto = http.begin(cliente, url);
  url = "";  // No imprimir URL: contiene el token.
  cJSON* resultado = nullptr;
  if (abierto) {
    http.addHeader("Content-Type", "application/json");
    int codigo = http.POST(reinterpret_cast<uint8_t*>(cuerpo), strlen(cuerpo));
    if (codigo == 200 && http.getSize() <= 65536) {
      String recibido;
      recibido.reserve(2048);
      auto* flujo = http.getStreamPtr();
      uint32_t ultimo = millis();
      int restante = http.getSize();
      bool completo = restante == 0;
      while (recibido.length() < 65536 && uint32_t(millis() - ultimo) < 5000) {
        int disponibles = flujo->available();
        if (disponibles > 0) {
          char bloque[256];
          size_t cantidad =
              size_t(disponibles) < sizeof(bloque) ? size_t(disponibles) : sizeof(bloque);
          if (cantidad > 65536 - recibido.length()) cantidad = 65536 - recibido.length();
          int leidos = flujo->readBytes(bloque, cantidad);
          recibido.concat(bloque, leidos);
          ultimo = millis();
          if (restante >= 0) {
            restante -= leidos;
            if (restante == 0) {
              completo = true;
              break;
            }
          }
        } else if (!http.connected()) {
          completo = restante < 0;
          break;
        } else
          vTaskDelay(pdMS_TO_TICKS(5));
      }
      if (completo) resultado = cJSON_ParseWithLength(recibido.c_str(), recibido.length());
    }
    // No mostrar cuerpo de errores: algunos servicios pueden incluir datos privados.
    if (codigo != 200) Serial.printf("[TG] Error de transporte/API: %d. Se reintentara.\n", codigo);
    http.end();
  }
  memset(cuerpo, 0, strlen(cuerpo));
  cJSON_free(cuerpo);
  return resultado;
}

bool enviar(const char* token, const char* chat, const char* texto) {
  cJSON* parametros = cJSON_CreateObject();
  if (!parametros) return false;
  cJSON_AddStringToObject(parametros, "chat_id", chat);
  cJSON_AddStringToObject(parametros, "text", texto);
  cJSON* resultado = solicitar(token, "sendMessage", parametros);
  bool ok = resultado && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(resultado, "ok"));
  cJSON_Delete(resultado);
  cJSON_Delete(parametros);
  return ok;
}

cJSON* consultar(const char* token, int64_t offset) {
  cJSON* parametros = cJSON_CreateObject();
  if (!parametros) return nullptr;
  cJSON_AddNumberToObject(parametros, "offset", double(offset));
  cJSON_AddNumberToObject(parametros, "limit", 1);
  cJSON_AddNumberToObject(parametros, "timeout", 0);
  cJSON* tipos = cJSON_AddArrayToObject(parametros, "allowed_updates");
  cJSON_AddItemToArray(tipos, cJSON_CreateString("message"));
  cJSON* resultado = solicitar(token, "getUpdates", parametros);
  cJSON_Delete(parametros);
  return resultado;
}

void procesarMensaje(cJSON* mensaje, const char* chat) {
  cJSON* chatObj = cJSON_GetObjectItemCaseSensitive(mensaje, "chat");
  cJSON* chatId = cJSON_GetObjectItemCaseSensitive(chatObj, "id");
  cJSON* texto = cJSON_GetObjectItemCaseSensitive(mensaje, "text");
  cJSON* fecha = cJSON_GetObjectItemCaseSensitive(mensaje, "date");
  char origen[24] = {};
  if (cJSON_IsNumber(chatId)) snprintf(origen, sizeof(origen), "%.0f", chatId->valuedouble);

  const Estado estado = leerEstado();
  Entrada entrada = {};
  const bool vigente =
      cJSON_IsNumber(fecha) && mensajeVigente(int64_t(fecha->valuedouble), int64_t(time(nullptr)));
  const Decision decision =
      decidir(chat, origen, cJSON_IsString(texto) ? texto->valuestring : nullptr, estado.modoTexto,
              estado.ocupado, entrada.texto, vigente);
  switch (decision) {
    case Decision::Ignorar:
      return;
    case Decision::SinTexto:
      responder("Solo se admiten mensajes de texto.");
      return;
    case Decision::Vencido:
      responder("Mensaje antiguo o sin fecha valida: descartado. Vuelva a enviarlo.");
      return;
    case Decision::Ayuda:
      responder(
          "Seleccione TEXTO -> MORSE con MODO y envie A-Z, 0-9 y espacios (hasta 80 caracteres). "
          "/estado /cancelar");
      return;
    case Decision::Estado:
      responder(estado.modoTexto
                    ? (estado.ocupado ? "TEXTO -> MORSE. Transmitiendo." : "TEXTO -> MORSE. Listo.")
                    : "MORSE -> TEXTO. Cambie con el pulsador MODO para enviar texto.");
      return;
    case Decision::OtroModo:
      responder("Modo MORSE -> TEXTO: cambie de modo con el pulsador fisico.");
      return;
    case Decision::Ocupado:
      responder("Ocupado. Espere el fin de la emision o use /cancelar. Mensaje descartado.");
      return;
    case Decision::Largo:
      responder("Limite: 80 caracteres. No se emitio.");
      return;
    case Decision::Invalido:
      responder("Use A-Z, 0-9 y espacios; sin tildes, signos ni mensajes vacios. No se emitio.");
      return;
    case Decision::Cancelar:
      entrada.tipo = Tipo::Cancelar;
      break;
    case Decision::Texto:
      entrada.tipo = Tipo::Texto;
      break;
  }
  entrada.revision = estado.revision;
  if (xQueueSend(entradas, &entrada, 0) != pdTRUE)
    responder("Equipo ocupado; mensaje descartado, intente nuevamente.");
}

void tarea(void*) {
  char ssid[33], clave[65], token[128], chat[24];
  strlcpy(ssid, MORSE_WIFI_SSID, sizeof(ssid));
  strlcpy(clave, MORSE_WIFI_PASSWORD, sizeof(clave));
  strlcpy(token, MORSE_TELEGRAM_TOKEN, sizeof(token));
  strlcpy(chat, MORSE_TELEGRAM_CHAT, sizeof(chat));
  bool iniciado = false, reloj = false, sincronizado = false, wifiAnterior = false;
  Cursor cursor;
  uint32_t intento = 0, consulta = 0, avisoHora = 0;
  Respuesta pendiente = {};
  bool hayRespuesta = false;
  Reintentos reintentos;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  for (;;) {
    Configuracion config = {};
    while (xQueueReceive(configuraciones, &config, 0) == pdTRUE) {
      if (config.wifi) {
        strlcpy(ssid, config.ssid, sizeof(ssid));
        strlcpy(clave, config.clave, sizeof(clave));
        WiFi.disconnect();
        iniciado = false;
        reloj = false;
      } else {
        strlcpy(token, config.token, sizeof(token));
        strlcpy(chat, config.chat, sizeof(chat));
        sincronizado = false;
        cursor = Cursor();
        hayRespuesta = false;
        reintentos.reiniciar();
        xQueueReset(respuestas);
        xQueueReset(entradas);
      }
      memset(&config, 0, sizeof(config));
    }
    bool wifi = WiFi.status() == WL_CONNECTED;
    bool bot = tokenValido(token) && chatValido(chat);
    if (!wifi) {
      if (wifiAnterior)
        Serial.println("[RED] Wi-Fi desconectado; el control local sigue disponible.");
      wifiAnterior = false;
      indicarRed(false, bot, false);
      if (ssid[0] && (!iniciado || uint32_t(millis() - intento) >= 15000)) {
        WiFi.begin(ssid, clave);
        iniciado = true;
        intento = millis();
        Serial.println("[RED] Conectando Wi-Fi; el control local sigue disponible.");
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!wifiAnterior) Serial.println("[RED] Wi-Fi conectado. Preparando hora y HTTPS.");
    wifiAnterior = true;
    if (!reloj) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      reloj = true;
    }
    if (!bot || time(nullptr) < 1704067200) {
      if (bot && uint32_t(millis() - avisoHora) >= 15000) {
        Serial.println("[RED] Esperando hora NTP para verificar certificados HTTPS.");
        avisoHora = millis();
      }
      indicarRed(true, bot, false);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!sincronizado) {
      // Al configurar/arrancar descartar backlog; no reproducir comandos antiguos.
      cJSON* raiz = consultar(token, -1);
      if (raiz && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raiz, "ok"))) {
        cJSON* elementos = cJSON_GetObjectItemCaseSensitive(raiz, "result");
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, elementos) {
          cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "update_id");
          if (cJSON_IsNumber(id)) cursor.aceptar(int64_t(id->valuedouble));
        }
        sincronizado = true;
        Serial.println("[TG] Listo. Mensajes anteriores descartados; chat autorizado configurado.");
      }
      cJSON_Delete(raiz);
      indicarRed(true, true, sincronizado);
      vTaskDelay(pdMS_TO_TICKS(sincronizado ? 100 : 3000));
      continue;
    }
    indicarRed(true, true, true);
    if (!hayRespuesta) {
      hayRespuesta = xQueueReceive(respuestas, &pendiente, 0) == pdTRUE;
      reintentos.reiniciar();
    }
    if (hayRespuesta && reintentos.disponible(millis())) {
      if (enviar(token, chat, pendiente.texto))
        hayRespuesta = false;
      else {
        reintentos.fallo(millis());
        if (reintentos.agotados()) {
          hayRespuesta = false;
          Serial.println("[TG] Notificacion descartada tras tres intentos.");
        }
      }
      // Incluso si falla sendMessage, continuar con getUpdates.
    }
    if (uint32_t(millis() - consulta) < 1000) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    consulta = millis();
    cJSON* raiz = consultar(token, cursor.siguiente());
    if (!raiz || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raiz, "ok"))) {
      cJSON_Delete(raiz);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    cJSON* elementos = cJSON_GetObjectItemCaseSensitive(raiz, "result");
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, elementos) {
      cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "update_id");
      if (!cJSON_IsNumber(id) || !cursor.aceptar(int64_t(id->valuedouble))) continue;
      procesarMensaje(cJSON_GetObjectItemCaseSensitive(item, "message"), chat);
    }
    cJSON_Delete(raiz);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace

void iniciar() {
  configuraciones = xQueueCreate(2, sizeof(Configuracion));
  entradas = xQueueCreate(1, sizeof(Entrada));
  respuestas = xQueueCreate(8, sizeof(Respuesta));
  if (!configuraciones || !entradas || !respuestas ||
      xTaskCreatePinnedToCore(tarea, "telegram", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("[TG] Sin recursos para iniciar red; permanece disponible el control local.");
    if (configuraciones) {
      vQueueDelete(configuraciones);
      configuraciones = nullptr;
    }
    if (entradas) {
      vQueueDelete(entradas);
      entradas = nullptr;
    }
    if (respuestas) {
      vQueueDelete(respuestas);
      respuestas = nullptr;
    }
  } else
    tareaIniciada = true;
}
void estado(bool modoTexto, bool ocupado, uint32_t revision) {
  portENTER_CRITICAL(&cerrojo);
  actual = {modoTexto, ocupado, revision};
  portEXIT_CRITICAL(&cerrojo);
}
bool recibir(Entrada& entrada) {
  return entradas && xQueueReceive(entradas, &entrada, 0) == pdTRUE;
}
void responder(const char* texto) {
  Respuesta r = {};
  strlcpy(r.texto, texto, sizeof(r.texto));
  if (tareaIniciada && xQueueSend(respuestas, &r, 0) != pdTRUE)
    Serial.println("[TG] Cola de respuestas llena; notificacion descartada.");
}
bool configurarWifi(const char* ssid, const char* clave) {
  if (strlen(ssid) > 32 || strlen(clave) > 64 || !tareaIniciada) return false;
  Configuracion c = {};
  c.wifi = true;
  strlcpy(c.ssid, ssid, sizeof(c.ssid));
  strlcpy(c.clave, clave, sizeof(c.clave));
  bool ok = xQueueSend(configuraciones, &c, 0) == pdTRUE;
  memset(&c, 0, sizeof(c));
  return ok;
}
bool configurarBot(const char* token, const char* chat) {
  if (!tokenValido(token) || !chatValido(chat) || !tareaIniciada) return false;
  Configuracion c = {};
  strlcpy(c.token, token, sizeof(c.token));
  strlcpy(c.chat, chat, sizeof(c.chat));
  bool ok = xQueueSend(configuraciones, &c, 0) == pdTRUE;
  memset(&c, 0, sizeof(c));
  return ok;
}
void mostrarEstado() {
  portENTER_CRITICAL(&cerrojo);
  bool w = conectado, b = botConfigurado, l = listo;
  portEXIT_CRITICAL(&cerrojo);
  Serial.printf("[RED] Wi-Fi: %s | Bot configurado: %s | Telegram listo: %s\n", w ? "SI" : "NO",
                b ? "SI" : "NO", l ? "SI" : "NO");
}
}  // namespace telegram
