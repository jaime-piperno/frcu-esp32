/**
 * PROYECTO: RecuerdaMed — Firmware de integracion MQTT (plan v2)
 * MATERIA: Tecnologias para la Automatizacion (ISI - 2026)
 * ARQUITECTURA: Server-centric. El backend decide cuando alertar y empuja el
 *   estado por MQTT; el device NO pollea y NUNCA re-alerta por su cuenta.
 *   - Recibe el snapshot completo en recuerdamed/{DEVICE_ID}/state (RETAINED, QoS 1)
 *   - Publica eventos de usuario en recuerdamed/{DEVICE_ID}/events
 * LIBRERIAS: PubSubClient (knolleary) + ArduinoJson (bblanchon, v7)
 * REFERENCIA: sketch Fase 1 validado (Documents/Arduino/RecuerdaMed). Aca la
 *   fuente de verdad deja de ser el reloj local y pasa a ser el state del server.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "BluetoothSpeaker.h"
#include "LocalConfig.h"

// ---------------------------------------------------------------------------
// Configuracion de red y MQTT (credenciales en LocalConfig.h)
// ---------------------------------------------------------------------------
// Canal 0 = auto-scan (Wokwi fijaba 6).
const uint8_t WIFI_CANAL = 0; // 0 = el ESP32 escanea y elige el canal del router

// Broker MQTT local: el Mosquitto del docker-compose corre en la maquina del
// backend (IP de la red local en LocalConfig.h), puerto 1883.
const uint16_t MQTT_PORT = 1883;
const uint16_t API_PORT = 8080; // voz generada por el backend en la misma maquina

// GUID del device tal como esta dado de alta en el backend (seed dev ESP32-01).
const char* DEVICE_ID = "e4a3d7ac-d839-40dc-890b-0fa47284511f";

const unsigned long TIEMPO_MAX_WIFI_MS = 35000UL; // timeout de la conexion WiFi (scan lento en redes con muchos APs)
const unsigned long RECONEXION_MQTT_MS = 3000UL;  // reintento MQTT cada 3s si cayo

// ---------------------------------------------------------------------------
// Pines (identicos a la Fase 1)
// ---------------------------------------------------------------------------
// OJO: fisicamente hay UN LED RGB. Mapeo verificado con test de pines (30/09):
//   D18 = canal VERDE, D19 = canal ROJO (cableado cruzado vs diagrama Wokwi),
//   D23 = canal AZUL. Catodo comun (HIGH = enciende).
const uint8_t PIN_LED_ROJO     = 19; // LED RGB: canal ROJO (cableado real)
const uint8_t PIN_LED_VERDE    = 18; // LED RGB: canal VERDE (cableado real)
const uint8_t PIN_LED_AZUL     = 23; // LED RGB: canal azul (verificado)
const uint8_t PIN_BUZZER       = 21; // fisico: D21 (buzzer pasivo)
const uint8_t PIN_BTN_SNOOZE   = 16; // fisico: D16 (INPUT_PULLUP: LOW = presionado)
const uint8_t PIN_BTN_CONFIRMA = 5;  // fisico: pin 5 (INPUT_PULLUP: LOW = presionado)
const uint8_t PIN_LDR          = 34; // ADC1_CH6

// LCD 16x2 por I2C (pines D4 y D22 elegidos)
const uint8_t PIN_I2C_SDA = 4;   // D4 (SDA)
const uint8_t PIN_I2C_SCL = 22;  // D22 (SCL)
const uint8_t LCD_ADDR    = 0x27; // PCF8574 (si no muestra texto, probar 0x3F)
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Snooze: el device manda los segundos y el backend los respeta tal cual.
// PRUEBAS: 60 = 1 min. PRODUCCION: 300 = 5 min (default server-side).
const int SNOOZE_SEGUNDOS = 60;

const int UMBRAL_NOCHE = 1500; // modulo LDR de Wokwi: oscuridad = lectura ALTA (~4000), dia = baja

const unsigned long INTERVALO_PARPADEO = 250; // mismo ritmo que el feedback verde de confirmacion
const unsigned long DEBOUNCE_MS = 250; // antirrebote por marca de tiempo, sin delay()

// ---------------------------------------------------------------------------
// FSM gobernada por el state que llega por MQTT
// ---------------------------------------------------------------------------
enum EstadoDispositivo { IDLE, ALERTANDO, SNOOZE };
EstadoDispositivo estado = IDLE;

// Medicamento activo: se toma del PRIMER alert del state. El backend manda la
// lista completa; el device solo necesita el primero para alertar y confirmar.
String medicationIdActivo = "";
String nombreActivo = "";
String dosajeActivo = "";
String scheduleIdActivo = "";
String firmaAlertas = "";
bool vozSolicitada = false;

// ---------------------------------------------------------------------------
// Melodia ascendente Do-Mi-Sol-Do (misma maquina de pasos no bloqueante de F1)
// ---------------------------------------------------------------------------
const int NOTA_1 = 523;  // Do5
const int NOTA_2 = 659;  // Mi5
const int NOTA_3 = 784;  // Sol5
const int NOTA_4 = 1047; // Do6
const int MELODIA[4] = { NOTA_1, NOTA_2, NOTA_3, NOTA_4 };
const int TOTAL_NOTAS = 4;

const unsigned long DURACION_NOTA = 130;       // ms que suena cada nota
const unsigned long PAUSA_ENTRE_NOTAS = 80;    // ms de silencio entre notas
const unsigned long PAUSA_ENTRE_CICLOS = 1300; // ms antes de repetir la melodia completa

// pasoMelodia: 0..(TOTAL_NOTAS*2 - 1) alterna nota/pausa; el ultimo paso es la pausa larga de ciclo
int pasoMelodia = 0;
unsigned long tUltimoPasoMelodia = 0;
bool notaIniciada = false; // evita inundar el timer PWM con tone() repetido
bool buzzerSonando = false;

// ---------------------------------------------------------------------------
// Estado global del loop
// ---------------------------------------------------------------------------
WiFiClient wifiClient;
PubSubClient client(wifiClient);

String topicState;  // recuerdamed/{DEVICE_ID}/state
String topicEvents; // recuerdamed/{DEVICE_ID}/events
String topicBluetoothConfig; // seleccion persistente del parlante
String topicBluetoothScan;   // orden de busqueda desde el dashboard
String topicBluetoothReport; // resultados y estado del ESP32
String clientId;    // unico por boot (para no chocar con un broker con sesion vieja)

unsigned long tUltimoParpadeo = 0;
bool estadoLedRojo = false;

// Feedback de confirmacion: 3 parpadeos VERDES tras "taken" (no bloqueante)
const unsigned long DURACION_PARPADEO_FEEDBACK = 250; // ms encendido/apagado por parpadeo
const int TOTAL_PARPADEOS_FEEDBACK = 3;
bool feedbackVerdeActivo = false;
int parpadeosFeedbackRestantes = 0;
unsigned long tUltimoPasoFeedback = 0;
bool estadoFeedbackVerde = false;

unsigned long tUltimoBtnSnooze = 0;
unsigned long tUltimoBtnConfirma = 0;

// Reconexion WiFi no bloqueante
bool conectandoWifi = false;
unsigned long tInicioWifi = 0;
unsigned long tUltimoIntentoMqtt = 0;

// LCD: estado de la pantalla
bool lcdEncendida = false;
bool lcdMostrandoConfirmacion = false;
unsigned long tConfirmacionLcd = 0;
const unsigned long DURACION_CONFIRMACION_LCD = 2500; // 2.5 segundos visible antes de apagar

// ---------------------------------------------------------------------------
// Melodia no bloqueante (identica a la Fase 1)
// ---------------------------------------------------------------------------
void actualizarMelodia(unsigned long tActual) {
  unsigned long tTranscurrido = tActual - tUltimoPasoMelodia;
  int totalPasos = TOTAL_NOTAS * 2; // cada nota tiene su paso de sonido + su paso de pausa corta

  if (pasoMelodia < totalPasos) {
    bool esPasoDeNota = (pasoMelodia % 2 == 0);
    int indiceNota = pasoMelodia / 2;

    if (esPasoDeNota) {
      // Disparar el sonido SOLO si no se ha iniciado en este paso
      if (!notaIniciada) {
        tone(PIN_BUZZER, MELODIA[indiceNota]);
        notaIniciada = true;
      }

      // Esperar a que transcurra el tiempo para apagarlo
      if (tTranscurrido >= DURACION_NOTA) {
        noTone(PIN_BUZZER);
        pasoMelodia++;
        tUltimoPasoMelodia = tActual;
        notaIniciada = false;
      }
    } else {
      // pausa corta entre notas (silencio)
      if (tTranscurrido >= PAUSA_ENTRE_NOTAS) {
        pasoMelodia++;
        tUltimoPasoMelodia = tActual;
        notaIniciada = false;
      }
    }
  } else {
    // pausa larga antes de repetir el ciclo completo
    if (tTranscurrido >= PAUSA_ENTRE_CICLOS) {
      pasoMelodia = 0;
      tUltimoPasoMelodia = tActual;
      notaIniciada = false;
    }
  }
}

void detenerMelodia() {
  noTone(PIN_BUZZER);
  buzzerSonando = false;
  pasoMelodia = 0;
  notaIniciada = false;
}

// ---------------------------------------------------------------------------
// LCD 16x2 — control de backlight y contenido
// ---------------------------------------------------------------------------
void lcdEncender() {
  if (!lcdEncendida) {
    lcd.backlight();
    lcdEncendida = true;
  }
}

void lcdApagar() {
  lcd.clear();
  lcd.noBacklight();
  lcdEncendida = false;
  lcdMostrandoConfirmacion = false;
}

// Limpia los ceros decimales innecesarios (ej: "500.000 mg" -> "500 mg", "1.500 g" -> "1.5 g")
String limpiarDecimalesDosis(const String& str) {
  int dotIndex = str.indexOf('.');
  if (dotIndex == -1) return str;

  int endIndex = dotIndex + 1;
  while (endIndex < str.length() && isDigit(str.charAt(endIndex))) {
    endIndex++;
  }

  if (endIndex == dotIndex + 1) return str;

  int lastNonZero = endIndex - 1;
  while (lastNonZero > dotIndex && str.charAt(lastNonZero) == '0') {
    lastNonZero--;
  }

  String resultado = str.substring(0, dotIndex);
  if (lastNonZero > dotIndex) {
    resultado += str.substring(dotIndex, lastNonZero + 1);
  }
  if (endIndex < str.length()) {
    resultado += str.substring(endIndex);
  }

  return resultado;
}

void lcdMostrarMedicamento(const String& nombre, const String& dosaje) {
  lcdEncender();
  lcd.clear();
  // Linea 1: nombre del medicamento (truncado a 16 caracteres)
  lcd.setCursor(0, 0);
  lcd.print(nombre.substring(0, 16));
  // Linea 2: dosis formateada sin ceros decimales innecesarios (ej: "x500 mg", "x1.5 g")
  lcd.setCursor(0, 1);
  if (dosaje.length() > 0) {
    String dosisLimpia = limpiarDecimalesDosis(dosaje);
    String linea2 = (dosisLimpia.startsWith("x") || dosisLimpia.startsWith("X")) ? dosisLimpia : (String("x") + dosisLimpia);
    lcd.print(linea2.substring(0, 16));
  }
}

void lcdMostrarConfirmacion() {
  lcdEncender();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Toma Confirmada");
  lcdMostrandoConfirmacion = true;
  tConfirmacionLcd = millis();
}

void lcdActualizar(unsigned long tActual) {
  // Apagar la pantalla cuando el mensaje de confirmacion expire
  if (lcdMostrandoConfirmacion && tActual - tConfirmacionLcd >= DURACION_CONFIRMACION_LCD) {
    lcdApagar();
  }
}

// ---------------------------------------------------------------------------
// Conectividad
// ---------------------------------------------------------------------------
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    conectandoWifi = false;
    return;
  }

  if (!conectandoWifi) {
    conectandoWifi = true;
    tInicioWifi = millis();
    WiFi.disconnect(true, true); // limpia el estado previo: evita "cannot set config"
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CANAL);
    Serial.printf("[WIFI] Conectando a %s...\n", WIFI_SSID);
  } else if (millis() - tInicioWifi >= TIEMPO_MAX_WIFI_MS) {
    Serial.printf("[ERROR] Timeout WiFi (status=%d, heap libre=%u) -> reintentando\n",
      static_cast<int>(WiFi.status()), static_cast<unsigned int>(ESP.getFreeHeap()));
    conectandoWifi = false;
    WiFi.disconnect(true, true);
  }
}

void conectarMQTT() {
  if (client.connected()) return;

  // Espaciar los reintentos cada ~3s para no golpear al broker
  if (millis() - tUltimoIntentoMqtt < RECONEXION_MQTT_MS) return;
  tUltimoIntentoMqtt = millis();

  Serial.printf("[MQTT] Conectando a %s:%d...\n", MQTT_BROKER, MQTT_PORT);
  if (client.connect(clientId.c_str())) {
    Serial.println("[MQTT] Conectado");
    client.subscribe(topicState.c_str(), 1);
    client.subscribe(topicBluetoothConfig.c_str(), 1);
    client.subscribe(topicBluetoothScan.c_str(), 1);
    bluetoothRequestStatusReport();
    Serial.printf("[MQTT] Suscrito a %s (QoS 1)\n", topicState.c_str());
    // Al reconectar, el broker reentrega el state RETAINED: el device queda
    // sincronizado sin hacer nada extra.
  } else {
    Serial.printf("[MQTT] Fallo de conexion (rc=%d)\n", client.state());
  }
}

// ---------------------------------------------------------------------------
// Manejo del state recibido (recuerdamed/{DEVICE_ID}/state)
// ---------------------------------------------------------------------------
void manejarState(const byte* payload, unsigned int length) {
  // ArduinoJson v7: JsonDocument crece solo (memoria dinamica), no hace falta
  // dimensionar como en v6 (StaticJsonDocument). Quien si hay que dimensionar
  // es el buffer de recepcion MQTT (setBufferSize 8192 abajo): el snapshot
  // incluye horarios futuros, alertas y sus mensajes opcionales.
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[ERROR] State invalido: %s\n", error.c_str());
    return;
  }

  JsonArrayConst alerts = doc["alerts"].as<JsonArrayConst>();
  bool snoozedState = doc["snoozed"] | false;
  bool missed = doc["missed"] | false;
  const String firmaAnterior = firmaAlertas;
  const EstadoDispositivo estadoAnterior = estado;
  firmaAlertas = "";
  for (JsonObjectConst alert : alerts) {
    firmaAlertas += alert["scheduleId"].as<String>();
    firmaAlertas += alert["scheduledUtc"].as<String>();
    firmaAlertas += alert["dosage"].as<String>();
    firmaAlertas += alert["message"].as<String>();
    firmaAlertas += '|';
  }

  if (alerts.size() > 0) {
    // El device alerta por el PRIMERO de la lista; el resto es informativo.
    JsonObjectConst primerAlert = alerts[0].as<JsonObjectConst>();
    medicationIdActivo = primerAlert["medicationId"].as<String>();
    nombreActivo = primerAlert["name"].as<String>();
    dosajeActivo = primerAlert["dosage"].as<String>();
    scheduleIdActivo = primerAlert["scheduleId"].as<String>();
  } else if (!snoozedState) {
    // Solo limpiamos los datos si NO estamos en estado SNOOZE
    medicationIdActivo = "";
    nombreActivo = "";
    dosajeActivo = "";
    scheduleIdActivo = "";
  }

  if (alerts.size() == 0 || estadoAnterior != ALERTANDO || firmaAnterior != firmaAlertas) {
    bluetoothStopSpeech();
    vozSolicitada = false;
  }

  // La transicion de estado la decide SOLO el server: hay alert -> ALERTANDO;
  // sino, snoozed -> SNOOZE; sino -> IDLE. No hay scheduler ni reloj local.
  if (alerts.size() > 0) {
    estado = ALERTANDO;
    if (!lcdMostrandoConfirmacion) {
      lcdMostrarMedicamento(nombreActivo, dosajeActivo);
    }
    Serial.printf("[ALERTA] -> %s (%s)\n", nombreActivo.c_str(), dosajeActivo.c_str());
    Serial.printf("[DISPLAY] -> TOMAR AHORA: %s\n", nombreActivo.c_str());
  } else if (snoozedState) {
    estado = SNOOZE;
    // LCD: mantener en pantalla el medicamento y la dosis hasta que se confirme la toma
    if (!lcdMostrandoConfirmacion) {
      lcdMostrarMedicamento(nombreActivo, dosajeActivo);
    }
    Serial.println("[DISPLAY] -> SNOOZE activo");
  } else {
    estado = IDLE;
    // Solo apagar si no esta mostrando activamente "Toma Confirmada" (deja que expire sola)
    if (!lcdMostrandoConfirmacion) {
      lcdApagar();
    }
    Serial.println("[DISPLAY] -> TODO AL DIA");
  }

  Serial.printf("[MQTT] State aplicado: alerts=%d snoozed=%d missed=%d\n",
    (int)alerts.size(), (int)snoozedState, (int)missed);
}

// ---------------------------------------------------------------------------
// Publicacion de eventos (recuerdamed/{DEVICE_ID}/events)
// ---------------------------------------------------------------------------
void publicarEvento(const char* tipo, const char* medicationId, int snoozeSeconds) {
  // Nota: PubSubClient solo publica con QoS 0 (limitacion de la libreria); el
  // broker entrega igual al backend, que suscribe con QoS 1. Solo los tipos
  // "taken" y "snoozed" son validos; cualquier otro lo ignora el backend.
  JsonDocument doc;
  doc["type"] = tipo;
  doc["medicationId"] = medicationId;
  if (snoozeSeconds > 0) {
    doc["snoozeSeconds"] = snoozeSeconds;
  }

  char buffer[192];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));
  // Cast a uint8_t*: PubSubClient solo expone publish() con longitud para bytes
  // (no hay overload con char* + length), y char* -> const uint8_t* no es
  // conversion implicita en C++.
  bool ok = client.publish(topicEvents.c_str(), (const uint8_t*)buffer, n, false);
  if (!ok) {
    Serial.println("[ERROR] No se pudo publicar el evento");
  }
}

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  if (String(topic) == topicState) {
    manejarState(payload, length);
  } else if (String(topic) == topicBluetoothConfig) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) == DeserializationError::Ok) {
      const char* selected = doc["address"].as<const char*>();
      bluetoothSetTarget(selected);
      Serial.printf("[BT] Parlante seleccionado: %s\n", selected ? selected : "ninguno");
    }
  } else if (String(topic) == topicBluetoothScan) {
    bluetoothStartScan();
    Serial.println("[BT] Busqueda solicitada desde el dashboard");
  }
}

// ---------------------------------------------------------------------------
// FSM de usuario: actuadores segun el estado recibido
// ---------------------------------------------------------------------------
void setColorRgb(bool rojo, bool verde, bool azul) {
  digitalWrite(PIN_LED_ROJO, rojo);
  digitalWrite(PIN_LED_VERDE, verde);
  digitalWrite(PIN_LED_AZUL, azul);
}

void iniciarFeedbackVerde() {
  feedbackVerdeActivo = true;
  parpadeosFeedbackRestantes = TOTAL_PARPADEOS_FEEDBACK;
  tUltimoPasoFeedback = millis();
  estadoFeedbackVerde = false; // el primer paso del ciclo lo enciende
}

// Feedback de confirmacion: 3 parpadeos verdes (sin bloquear el loop)
void actualizarFeedbackVerde(unsigned long tActual) {
  if (!feedbackVerdeActivo) return;

  if (tActual - tUltimoPasoFeedback >= DURACION_PARPADEO_FEEDBACK) {
    tUltimoPasoFeedback = tActual;
    estadoFeedbackVerde = !estadoFeedbackVerde;
    setColorRgb(false, estadoFeedbackVerde, false);
    // Cada vez que el parpadeo pasa de encendido a apagado, se consume un ciclo.
    if (!estadoFeedbackVerde) {
      parpadeosFeedbackRestantes--;
      if (parpadeosFeedbackRestantes <= 0) {
        feedbackVerdeActivo = false;
        setColorRgb(false, false, false); // vuelve a IDLE limpio; el state lo re-pinta
      }
    }
  }
}

void actualizarActuadores(unsigned long tActual) {
  int valorLuz = analogRead(PIN_LDR);
  // Modulo sensor de Wokwi: a menos luz, mayor voltaje en AO (logica invertida vs Fase 1).
  bool esNoche = (valorLuz > UMBRAL_NOCHE);

  // El feedback de confirmacion tiene prioridad sobre el estado del server:
  // aunque el state ya llego con IDLE, el verde parpadea 3 veces.
  if (feedbackVerdeActivo) {
    actualizarFeedbackVerde(tActual);
    return;
  }

  if (estado == ALERTANDO) {
    // LED RGB ROJO parpadeando a la par que suena; buzzer SOLO si no es de noche
    setColorRgb(false, false, false);
    if (tActual - tUltimoParpadeo >= INTERVALO_PARPADEO) {
      tUltimoParpadeo = tActual;
      estadoLedRojo = !estadoLedRojo;
      digitalWrite(PIN_LED_ROJO, estadoLedRojo);
    }
    if (!esNoche && bluetoothUseBuzzer()) {
      if (!buzzerSonando) {
        buzzerSonando = true;
        pasoMelodia = 0;
        tUltimoPasoMelodia = tActual;
        notaIniciada = false;
      }
      actualizarMelodia(tActual);
    } else if (buzzerSonando) {
      detenerMelodia();
    }
  } else if (estado == SNOOZE) {
    // LED RGB AZUL fijo (alarma pospuesta), sin melodia
    setColorRgb(false, false, true);
      if (buzzerSonando) detenerMelodia();
  } else { // IDLE
    // LED RGB VERDE fijo (todo al dia)
    setColorRgb(false, true, false);
    if (buzzerSonando) detenerMelodia();
  }
}

// ---------------------------------------------------------------------------
// Botones -> eventos MQTT (antirrebote por marca de tiempo, sin delay())
// ---------------------------------------------------------------------------
void actualizarBotones(unsigned long tActual) {
  if (estado == IDLE || medicationIdActivo.length() == 0) return;

  bool btnSnooze = (digitalRead(PIN_BTN_SNOOZE) == LOW);
  bool btnConfirma = (digitalRead(PIN_BTN_CONFIRMA) == LOW);

  // CONFIRMA tiene prioridad (igual que Fase 1) y vale en ALERTANDO y SNOOZE.
  if (btnConfirma && tActual - tUltimoBtnConfirma >= DEBOUNCE_MS) {
    tUltimoBtnConfirma = tActual;
    publicarEvento("taken", medicationIdActivo.c_str(), 0);
    Serial.printf("[CONFIRMADO] Dosis tomada: %s (%s)\n", nombreActivo.c_str(), dosajeActivo.c_str());
    lcdMostrarConfirmacion();
    // Feedback local: 3 parpadeos verdes del RGB (el state del server re-pinta
    // el LED segun corresponda al terminar).
    iniciarFeedbackVerde();
    // El estado no cambia aca: el backend procesa el evento, actualiza y
    // re-publica el state. El device espera el proximo state; no re-alerta.
  } else if (btnSnooze && estado == ALERTANDO && tActual - tUltimoBtnSnooze >= DEBOUNCE_MS) {
    tUltimoBtnSnooze = tActual;
    publicarEvento("snoozed", medicationIdActivo.c_str(), SNOOZE_SEGUNDOS);
    Serial.printf("[SNOOZE] Alarma pospuesta (%d s server-side): %s\n", SNOOZE_SEGUNDOS, nombreActivo.c_str());
    // El snooze es server-side: el device no cuenta nada local.
  }
}

// ---------------------------------------------------------------------------
// Setup y loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[SISTEMA] RecuerdaMed v2 - Firmware integracion MQTT");

  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_SNOOZE, INPUT_PULLUP);
  pinMode(PIN_BTN_CONFIRMA, INPUT_PULLUP);

  // LCD 16x2 por I2C en pines elegidos: SDA = D4, SCL = D22
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.clear();
  lcd.noBacklight(); // arranca apagada en todo momento
  Serial.printf("[LCD] Inicializada en SDA=GPIO%d, SCL=GPIO%d (backlight OFF)\n", PIN_I2C_SDA, PIN_I2C_SCL);

  randomSeed(analogRead(PIN_LDR) ^ micros());

  clientId = String("recuerdamed-device-") + String(random(0x7FFFFFFF), HEX);
  topicState = String("recuerdamed/") + DEVICE_ID + "/state";
  topicEvents = String("recuerdamed/") + DEVICE_ID + "/events";
  topicBluetoothConfig = String("recuerdamed/") + DEVICE_ID + "/bluetooth/config";
  topicBluetoothScan = String("recuerdamed/") + DEVICE_ID + "/bluetooth/scan";
  topicBluetoothReport = String("recuerdamed/") + DEVICE_ID + "/bluetooth/report";

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callbackMqtt);
  // The normal retained snapshot needs room for alerts and future doses.
  client.setBufferSize(8192);

  Serial.printf("[SISTEMA] Device: %s\n", DEVICE_ID);
  Serial.printf("[SISTEMA] State topic: %s\n", topicState.c_str());
  Serial.printf("[SISTEMA] Events topic: %s\n", topicEvents.c_str());

  if (!bluetoothBegin()) Serial.println("[BT] No se pudo inicializar Bluetooth Classic/A2DP");

  conectarWiFi(); // dispara la conexion; se completa en el loop sin bloquear
}

void loop() {
  unsigned long tActual = millis();

  // 1. Mantener WiFi y MQTT vivos (todo no bloqueante)
  conectarWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      conectarMQTT();
    } else {
      client.loop();
    }
  }
  bluetoothLoop(client, topicBluetoothReport);

  // Una locucion por inicio de alarma; si el parlante se desconecta, el buzzer
  // retoma la alarma y se vuelve a solicitar voz cuando reconecte.
  if (!bluetoothIsConnected()) vozSolicitada = false;
  if (estado == ALERTANDO && bluetoothIsConnected() && !vozSolicitada &&
      scheduleIdActivo.length() && WiFi.status() == WL_CONNECTED) {
    String url = String("http://") + MQTT_BROKER + ":" + API_PORT +
      "/api/devices/" + DEVICE_ID + "/speech";
    vozSolicitada = bluetoothQueueSpeech(url);
    if (vozSolicitada) Serial.printf("[VOZ] Solicitada para %s\n", nombreActivo.c_str());
  }

  // 2. FSM: actuadores segun el estado que mando el server
  actualizarActuadores(tActual);

  // 3. LCD: apagar tras el mensaje de confirmacion
  lcdActualizar(tActual);

  // 4. Botones -> eventos MQTT (taken / snoozed)
  actualizarBotones(tActual);
}
