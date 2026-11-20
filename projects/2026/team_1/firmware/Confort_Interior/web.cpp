// web.cpp — Wi-Fi, servidor HTTP, WebSocket y API del Nodo Interior (Confort)
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "web.h"
#include "storage.h"
#include "calibracion.h"
#include "web_assets.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#ifndef AP_SSID
#define AP_SSID "Confort_Interior"
#endif

#ifndef AP_PASS
#define AP_PASS "confort1234"
#endif

enum class ModoWifi { CONECTANDO, STA, AP };
static ModoWifi modo = ModoWifi::CONECTANDO;
static uint32_t inicioWifi = 0, ultimaLimpieza = 0;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static Lectura lecturaLocal;
static Lectura lecturaRemota;       // ya corregida: es la que se muestra y compara
static Lectura lecturaRemotaCruda;  // sin corregir: se usa para calibrar
static uint32_t ultimoRemotoMs = 0;
static EstadoControl ultimoEstado;
static bool hayLecturaLocal = false;

static Correccion correccion;
static AcumuladorCalibracion acumulador;
static bool calibrando = false;
static Preferences prefsCal;

static void calCargar() {
  prefsCal.begin("calib", true);
  Correccion c;
  c.temp = prefsCal.getFloat("temp", 0.0f);
  c.hum = prefsCal.getFloat("hum", 0.0f);
  c.iaq = prefsCal.getFloat("iaq", 0.0f);
  prefsCal.end();
  correccion = c;
}

static void calGuardar() {
  prefsCal.begin("calib", false);
  prefsCal.putFloat("temp", correccion.temp);
  prefsCal.putFloat("hum", correccion.hum);
  prefsCal.putFloat("iaq", correccion.iaq);
  prefsCal.end();
}

static const char* nombreModo() {
  return modo == ModoWifi::STA ? "STA" : modo == ModoWifi::AP ? "AP" : "conectando";
}

static void empaquetarNodoJson(JsonObject obj, const Lectura& l) {
  obj["conectado"] = l.conectado;
  if (l.conectado) {
    obj["iaq"] = l.iaq;
    obj["temp"] = l.temp;
    obj["hum"] = l.hum;
    obj["gas"] = l.gas;
    obj["ldr"] = l.ldr;
    obj["sonido"] = l.sonido;
    obj["pulsos"] = l.pulsosSonido;
    obj["acc"] = l.precision;
  } else {
    obj["iaq"] = (char*)nullptr;
    obj["temp"] = (char*)nullptr;
    obj["hum"] = (char*)nullptr;
    obj["gas"] = (char*)nullptr;
    obj["ldr"] = (char*)nullptr;
    obj["sonido"] = (char*)nullptr;
    obj["pulsos"] = (char*)nullptr;
    obj["acc"] = (char*)nullptr;
  }
}

static void llenarLecturaCompleta(JsonDocument& d, const Lectura& loc, const Lectura& rem,
                                  const Lectura& remCrudo, const EstadoControl& s) {
  d["t"] = uptimeS();
  d["rol"] = "interior";

  JsonObject objInterior = d["aula"].to<JsonObject>();
  JsonObject objExterior = d["pasillo"].to<JsonObject>();
  JsonObject objExteriorCrudo = d["pasillo_crudo"].to<JsonObject>();

  empaquetarNodoJson(objInterior, loc);
  empaquetarNodoJson(objExterior, rem);
  empaquetarNodoJson(objExteriorCrudo, remCrudo);

  JsonObject delta = d["delta"].to<JsonObject>();
  if (rem.conectado) {
    delta["iaq"] = s.deltaIaq;
    delta["temp"] = s.deltaTemp;
    delta["hum"] = s.deltaHum;
    delta["ldr"] = s.deltaLdr;
    delta["sonido"] = s.deltaSonido;
  } else {
    delta["iaq"] = (char*)nullptr;
    delta["temp"] = (char*)nullptr;
    delta["hum"] = (char*)nullptr;
    delta["ldr"] = (char*)nullptr;
    delta["sonido"] = (char*)nullptr;
  }

  // Diferencia calculada con los valores SIN corregir: permite auditar la calibración.
  JsonObject deltaCrudo = d["delta_crudo"].to<JsonObject>();
  if (remCrudo.conectado) {
    deltaCrudo["iaq"] = loc.iaq - remCrudo.iaq;
    deltaCrudo["temp"] = loc.temp - remCrudo.temp;
    deltaCrudo["hum"] = loc.hum - remCrudo.hum;
    deltaCrudo["ldr"] = loc.ldr - remCrudo.ldr;
    deltaCrudo["sonido"] = loc.sonido - remCrudo.sonido;
  } else {
    deltaCrudo["iaq"] = (char*)nullptr;
    deltaCrudo["temp"] = (char*)nullptr;
    deltaCrudo["hum"] = (char*)nullptr;
    deltaCrudo["ldr"] = (char*)nullptr;
    deltaCrudo["sonido"] = (char*)nullptr;
  }

  d["sensor_ok"] = sensorOk();

  JsonObject cal = d["calibracion"].to<JsonObject>();
  cal["temp"] = correccion.temp;
  cal["hum"] = correccion.hum;
  cal["iaq"] = correccion.iaq;
  cal["activa"] = correccion.activa();
  cal["calibrando"] = calibrando;
}

// ---------- CSV en streaming ----------
struct Flujo {
  bool enVivo;
  uint32_t seq, fin;
  bool cabecera = false;
  char linea[160];
  size_t len = 0, off = 0;
};

static size_t llenar(Flujo& f, uint8_t* buf, size_t max) {
  size_t escrito = 0;
  while (escrito < max) {
    if (f.off == f.len) {
      f.off = 0;
      if (!f.cabecera) {
        f.len = strlcpy(f.linea, CSV_CABECERA, sizeof f.linea);
        f.cabecera = true;
      } else if (f.seq < f.fin) {
        Muestra m;
        storageBloquear();
        m = f.enVivo ? vivo.en(f.seq) : historial.en(f.seq);
        storageDesbloquear();
        f.len = formatearFila(f.linea, sizeof f.linea, m, epochArranque);
        f.seq++;
      } else {
        break;
      }
    }
    size_t disp = max - escrito;
    size_t resto = f.len - f.off;
    size_t chunk = disp < resto ? disp : resto;
    memcpy(buf + escrito, f.linea + f.off, chunk);
    f.off += chunk;
    escrito += chunk;
  }
  return escrito;
}

static void enviarCsv(AsyncWebServerRequest* r, const String& rango, bool descarga) {
  auto f = std::make_shared<Flujo>();
  f->enVivo = rango == "live";
  uint32_t minutos = rango == "1h" ? 60 : rango == "6h" ? 360 : N_HIST;
  storageBloquear();
  f->fin = f->enVivo ? vivo.total : historial.total;
  uint32_t primero = f->enVivo ? vivo.primero() : historial.primero();
  f->seq = f->enVivo || f->fin - primero <= minutos ? primero : f->fin - minutos;
  storageDesbloquear();
  AsyncWebServerResponse* res = r->beginChunkedResponse(
      "text/csv", [f](uint8_t* buf, size_t max, size_t) -> size_t { return llenar(*f, buf, max); });
  if (descarga) res->addHeader("Content-Disposition", "attachment; filename=\"confort_interior_exterior.csv\"");
  r->send(res);
}

static void rutas() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse_P(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });

  server.on("/api/current", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    storageBloquear();
    Lectura loc = lecturaLocal;
    Lectura rem = lecturaRemota;
    Lectura remCrudo = lecturaRemotaCruda;
    EstadoControl s = ultimoEstado;
    bool hay = hayLecturaLocal;
    storageDesbloquear();

    if (hay) llenarLecturaCompleta(d, loc, rem, remCrudo, s);
    d["sensor_ok"] = sensorOk();

    JsonObject eq = d["equipo"].to<JsonObject>();
    eq["rol"] = "Interior (Servidor)";
    eq["modo"] = nombreModo();
    eq["ip"] = (modo == ModoWifi::AP ? WiFi.softAPIP() : WiFi.localIP()).toString();
    eq["rssi"] = modo == ModoWifi::STA ? WiFi.RSSI() : 0;
    eq["uptime"] = uptimeS();
    eq["heap"] = ESP.getFreeHeap();
    eq["sensor"] = sensorVersion();

    String out;
    serializeJson(d, out);
    r->send(200, "application/json", out);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* r) {
    String rango = r->hasParam("range") ? r->getParam("range")->value() : "live";
    enviarCsv(r, rango, false);
  });

  server.on("/api/export.csv", HTTP_GET, [](AsyncWebServerRequest* r) {
    enviarCsv(r, "24h", true);
  });

  // Recepción de datos del Nodo Exterior
  auto* postRemoto = new AsyncCallbackJsonWebHandler("/api/nodo_remoto", [](AsyncWebServerRequest* r, JsonVariant& j) {
    Lectura cruda;
    cruda.iaq = j["iaq"].as<float>();
    cruda.temp = j["temp"].as<float>();
    cruda.hum = j["hum"].as<float>();
    cruda.gas = j["gas"].as<float>();
    cruda.ldr = j["ldr"].as<float>();
    cruda.sonido = j["sonido"].as<float>();
    cruda.pulsosSonido = j["pulsos"].as<uint32_t>();
    cruda.precision = j["acc"].as<uint8_t>();
    cruda.conectado = true;

    // Calibración cruzada: se muestrea con los valores SIN corregir.
    if (calibrando && hayLecturaLocal) {
      storageBloquear();
      Lectura local = lecturaLocal;
      storageDesbloquear();
      acumulador.muestrear(local, cruda);
    }

    storageBloquear();
    lecturaRemotaCruda = cruda;
    lecturaRemota = correccionAplicar(cruda, correccion);
    ultimoRemotoMs = millis();
    storageDesbloquear();
    r->send(200, "application/json", "{\"ok\":true}");
  });
  postRemoto->setMethod(HTTP_POST);
  server.addHandler(postRemoto);

  // ---------- Calibración cruzada ----------
  server.on("/api/calibracion", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    d["temp"] = correccion.temp;
    d["hum"] = correccion.hum;
    d["iaq"] = correccion.iaq;
    d["activa"] = correccion.activa();
    d["calibrando"] = calibrando;
    d["muestras"] = calibrando ? acumulador.n : 0;
    d["objetivo"] = AcumuladorCalibracion::OBJETIVO;
    d["progreso"] = calibrando ? acumulador.progresoPct() : 0;
    String out;
    serializeJson(d, out);
    r->send(200, "application/json", out);
  });

  server.on("/api/calibracion/iniciar", HTTP_POST, [](AsyncWebServerRequest* r) {
    acumulador.reiniciar();
    calibrando = true;
    Serial.println(F("# Calibracion iniciada: mantene los dos nodos en el mismo ambiente"));
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/calibracion/aplicar", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!calibrando || acumulador.n == 0) {
      r->send(409, "application/json", "{\"ok\":false,\"error\":\"sin muestras\"}");
      return;
    }
    correccion = acumulador.resultado();
    calibrando = false;
    acumulador.reiniciar();
    calGuardar();

    Serial.printf("# Calibracion aplicada: dT=%.2f C dHum=%.2f %% dIAQ=%.2f\n",
                  correccion.temp, correccion.hum, correccion.iaq);
    JsonDocument d;
    d["ok"] = true;
    d["temp"] = correccion.temp;
    d["hum"] = correccion.hum;
    d["iaq"] = correccion.iaq;
    String out;
    serializeJson(d, out);
    r->send(200, "application/json", out);
  });

  server.on("/api/calibracion/reset", HTTP_POST, [](AsyncWebServerRequest* r) {
    correccion = Correccion();
    calibrando = false;
    acumulador.reiniciar();
    calGuardar();
    Serial.println(F("# Calibracion borrada"));
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "No encontrado"); });

  ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      storageBloquear();
      Lectura loc = lecturaLocal;
      Lectura rem = lecturaRemota;
      Lectura remCrudo = lecturaRemotaCruda;
      EstadoControl st = ultimoEstado;
      bool hay = hayLecturaLocal;
      storageDesbloquear();
      if (hay) {
        JsonDocument d;
        llenarLecturaCompleta(d, loc, rem, remCrudo, st);
        char buf[1400];
        serializeJson(d, buf, sizeof buf);
        c->text(buf);
      }
    }
  });

  server.addHandler(&ws);
}

static void iniciarServicios() {
  if (MDNS.begin("confort")) MDNS.addService("http", "tcp", 80);
  IPAddress ip = (modo == ModoWifi::AP) ? WiFi.softAPIP() : WiFi.localIP();
  if (modo == ModoWifi::AP) {
    Serial.println(F("\n========================================================"));
    Serial.println(F(">>> PUNTO DE ACCESO WI-FI: CONFORT INTERIOR <<<"));
    Serial.println(F("1. Conectate desde tu celular o PC a la red Wi-Fi:"));
    Serial.println(F("   - Nombre (SSID): Confort_Interior"));
    Serial.println(F("   - Contraseña:    confort1234"));
    Serial.printf( "2. Abrí tu navegador y entrá a: http://%s\n", ip.toString().c_str());
    Serial.println(F("========================================================\n"));
  } else {
    Serial.printf("# Wi-Fi Conectado (STA). Abrí http://%s o http://confort.local\n", ip.toString().c_str());
  }
}

void webIniciar() {
  calCargar();

  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "nombre-de-tu-red") == 0) {
    Serial.println(F("# Sin red Wi-Fi configurada: Iniciando Punto de Acceso propio..."));
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);
    modo = ModoWifi::AP;
    rutas();
    server.begin();
    iniciarServicios();
    return;
  }

  Serial.printf("# Intentando conectar a Wi-Fi '%s' (espera máx %d s)...\n", WIFI_SSID, (int)(WIFI_TIMEOUT_MS / 1000));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  inicioWifi = millis();
  rutas();
  server.begin();
}

void webActualizar() {
  uint32_t ahora = millis();
  if (modo == ModoWifi::CONECTANDO) {
    static uint32_t ultimoPunto = 0;
    if (ahora - ultimoPunto >= 1000) {
      ultimoPunto = ahora;
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      modo = ModoWifi::STA;
      configTime(-3 * 3600, 0, "pool.ntp.org");
      iniciarServicios();
    } else if (ahora - inicioWifi > WIFI_TIMEOUT_MS) {
      Serial.println(F("\n# Tiempo agotado en Wi-Fi: Iniciando Punto de Acceso (AP)..."));
      WiFi.disconnect(true);
      delay(200);
      WiFi.mode(WIFI_AP);
      delay(100);
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
      bool okAP = WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);
      modo = ModoWifi::AP;
      Serial.printf("# Punto de acceso '%s': %s\n", AP_SSID, okAP ? "CREADO EXITOSAMENTE" : "FALLÓ");
      iniciarServicios();
    }
  }

  if (lecturaRemota.conectado && (ahora - ultimoRemotoMs > REMOTO_TIMEOUT_MS)) {
    storageBloquear();
    lecturaRemota.conectado = false;
    storageDesbloquear();
    Serial.println(F("# Nodo Exterior desconectado por timeout"));
  }

  if (ahora - ultimaLimpieza > 1000) {
    ultimaLimpieza = ahora;
    ws.cleanupClients();
  }
}

void webPublicar(const Lectura& local, const Lectura& remoto, const EstadoControl& s) {
  Lectura remCrudo;
  storageBloquear();
  lecturaLocal = local;
  ultimoEstado = s;
  hayLecturaLocal = true;
  remCrudo = lecturaRemotaCruda;
  storageDesbloquear();

  if (ws.count() == 0) return;
  JsonDocument d;
  llenarLecturaCompleta(d, local, remoto, remCrudo, s);
  char buf[1400];
  serializeJson(d, buf, sizeof buf);
  ws.textAll(buf);
}

bool webObtenerRemoto(Lectura& out) {
  storageBloquear();
  out = lecturaRemota;
  bool ok = lecturaRemota.conectado;
  storageDesbloquear();
  return ok;
}
