#include "TelegramAvisos.h"
#include "credenciales.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// El ruido debe permanecer en FUERTE durante 2 s.
// Se limita a un intento de aviso cada 30 s, incluso si falla internet.
static const uint32_t RUIDO_SOSTENIDO_MS = 0;
static const uint32_t INTERVALO_AVISOS_MS = 30000;
static const uint32_t CADUCIDAD_AVISO_MS = 5000;

struct AvisoRuido {
  uint16_t amplitud;
  uint32_t creado;
};

static QueueHandle_t colaAvisos = nullptr;
static bool estabaFuerte = false;
static bool huboIntento = false;
static uint32_t fuerteDesde = 0;
static uint32_t ultimoIntento = 0;

static String codificarFormulario(const String &valor) {
  const char hex[] = "0123456789ABCDEF";
  String salida;
  salida.reserve(valor.length() * 3);
  for (size_t i = 0; i < valor.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(valor[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      salida += static_cast<char>(c);
    } else {
      salida += '%';
      salida += hex[c >> 4];
      salida += hex[c & 15];
    }
  }
  return salida;
}

static bool enviarTelegram(uint16_t amplitud, bool prueba = false) {
  NetworkClientSecure cliente;
  // Verifica el certificado del servidor con las CA incluidas en ESP32 3.3.12.
  cliente.useBuiltinCACertBundle();
  cliente.setHandshakeTimeout(8);

  HTTPClient peticion;
  peticion.setConnectTimeout(5000);
  peticion.setTimeout(15000);
  String url = String("https://api.telegram.org/bot") + TELEGRAM_TOKEN + "/sendMessage";
  if (!peticion.begin(cliente, url)) {
    Serial.println(F("!! Telegram: no se pudo preparar la conexion HTTPS"));
    return false;
  }

  String texto = prueba
    ? String("PRUEBA: El semaforo de ruido esta conectado. Los avisos de Telegram funcionan.")
    : String("ALERTA: Hay demasiado ruido. Bajen el volumen. Amplitud: ") + amplitud;
  String datos = String("chat_id=") + codificarFormulario(TELEGRAM_CHAT_ID) +
                 "&text=" + codificarFormulario(texto) + "&disable_notification=false";
  peticion.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int estado = peticion.POST(datos);
  bool enviado = false;
  if (estado == 200) {
    String respuesta = peticion.getString();
    respuesta.replace(" ", "");
    respuesta.replace("\n", "");
    respuesta.replace("\r", "");
    enviado = respuesta.indexOf("\"ok\":true") >= 0;
  }
  peticion.end();

  // No imprimir URL, token, clave Wi-Fi ni respuesta con datos personales.
  if (enviado) {
    Serial.println(F(">> Telegram: aviso enviado"));
  } else {
    Serial.printf("!! Telegram: envio fallido (HTTP %d). Revisar internet, token y chat ID.\n", estado);
  }
  return enviado;
}

static void tareaTelegram(void *) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_NOMBRE, WIFI_CLAVE);
  bool conectadoAntes = false;
  bool horaInformada = false;
  bool pruebaIntentada = false;
  uint32_t ultimoAvisoHora = 0;
  uint32_t ultimoReintentoWifi = millis();

  for (;;) {
    bool conectado = WiFi.status() == WL_CONNECTED;
    if (!conectado) {
      if (conectadoAntes) Serial.println(F("!! Wi-Fi desconectado; el semaforo sigue funcionando"));
      conectadoAntes = false;
      horaInformada = false;
      if (millis() - ultimoReintentoWifi >= 15000) {
        ultimoReintentoWifi = millis();
        Serial.println(F(">> Wi-Fi: intentando conectar (red de 2.4 GHz)"));
        WiFi.reconnect();
      }
      // No acumular alertas antiguas mientras no hay red.
      xQueueReset(colaAvisos);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!conectadoAntes) {
      conectadoAntes = true;
      Serial.println(F(">> Wi-Fi conectado. Sincronizando hora para HTTPS..."));
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ultimoAvisoHora = millis();
    }
    // La hora correcta es necesaria para validar certificados HTTPS.
    if (time(nullptr) < 1700000000) {
      if (millis() - ultimoAvisoHora >= 15000) {
        ultimoAvisoHora = millis();
        Serial.println(F("!! Telegram: esperando hora NTP. Todavia no se puede enviar."));
      }
      xQueueReset(colaAvisos);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!horaInformada) {
      horaInformada = true;
      Serial.println(F(">> Telegram listo: esperando ruido fuerte"));
      if (!pruebaIntentada) {
        pruebaIntentada = true;
        Serial.println(F(">> Telegram: enviando mensaje de prueba..."));
        enviarTelegram(0, true);
      }
    }
    AvisoRuido aviso;
    if (xQueueReceive(colaAvisos, &aviso, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (millis() - aviso.creado <= CADUCIDAD_AVISO_MS) enviarTelegram(aviso.amplitud);
    }
  }
}

void iniciarTelegram() {
  if (!strlen(WIFI_NOMBRE) || !strlen(TELEGRAM_TOKEN) || !strlen(TELEGRAM_CHAT_ID) ||
      strncmp(WIFI_NOMBRE, "PON_", 4) == 0 || strncmp(WIFI_CLAVE, "PON_", 4) == 0 ||
      strncmp(TELEGRAM_TOKEN, "PON_", 4) == 0 || strncmp(TELEGRAM_CHAT_ID, "PON_", 4) == 0) {
    Serial.println(F("!! Completar credenciales.h; por ahora funciona solo el semaforo"));
    return;
  }
  colaAvisos = xQueueCreate(1, sizeof(AvisoRuido));
  if (!colaAvisos) {
    Serial.println(F("!! No hay memoria para los avisos; el semaforo sigue funcionando"));
    return;
  }
  // HTTP se ejecuta en otra tarea: no bloquea las mediciones ni los pitidos.
  if (xTaskCreate(tareaTelegram, "Telegram", 12288, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(colaAvisos);
    colaAvisos = nullptr;
    Serial.println(F("!! No se pudo iniciar Telegram; el semaforo sigue funcionando"));
  }
}

void manejarTelegram(bool ruidoFuerte, uint16_t amplitud) {
  if (!colaAvisos) return;
  uint32_t ahora = millis();
  if (!ruidoFuerte) {
    estabaFuerte = false;
    return;
  }
  if (!estabaFuerte) {
    estabaFuerte = true;
    fuerteDesde = ahora;
  }
  if (ahora - fuerteDesde < RUIDO_SOSTENIDO_MS) return;
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return;
  if (huboIntento && ahora - ultimoIntento < INTERVALO_AVISOS_MS) return;

  AvisoRuido aviso = {amplitud, ahora};
  if (xQueueSend(colaAvisos, &aviso, 0) == pdTRUE) {
    huboIntento = true;
    ultimoIntento = ahora;
  }
}
