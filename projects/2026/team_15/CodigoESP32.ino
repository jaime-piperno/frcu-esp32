/*
  ESP32 — Single-Board Plotter Controller (WebSocket + X/Y/Z stepper motors)
  Adaptado para 3 motores 28BYJ-48 en modo FULL4WIRE con Botón de Pausa/Reanudación.
*/
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <Preferences.h>
#include <deque>

// ---------- Red Wi-Fi ----------
const char* AP_SSID = "CNC-Plotter";
const char* AP_PASS = "12345678";     
const bool  USE_AP_MODE = true;       

const char* STA_SSID = "YOUR_ROUTER_SSID";
const char* STA_PASS = "YOUR_ROUTER_PASSWORD";

const int WS_PORT = 81;
WebSocketsServer webSocket(WS_PORT);

// ---------- Pines ----------
// Eje X
#define X_IN1 13
#define X_IN2 14
#define X_IN3 26
#define X_IN4 25

// Eje Y
#define Y_IN1 4
#define Y_IN2 16
#define Y_IN3 17
#define Y_IN4 18

// Eje Z
#define Z_IN1 32
#define Z_IN2 19
#define Z_IN3 22
#define Z_IN4 23

// Pin para Botón (Pausa / Reanudación / Emergencia)
#define EMERGENCY_STOP_PIN 27

// Inicialización de motores (FULL4WIRE para máxima fuerza)
AccelStepper stepperX(AccelStepper::FULL4WIRE, X_IN1, X_IN3, X_IN2, X_IN4);
AccelStepper stepperY(AccelStepper::FULL4WIRE, Y_IN1, Y_IN3, Y_IN2, Y_IN4);
AccelStepper stepperZ(AccelStepper::FULL4WIRE, Z_IN1, Z_IN3, Z_IN2, Z_IN4);
MultiStepper steppersXY;

// ---------- Altura del Eje Z (en mm) ----------
#define PEN_UP_Z_MM    0.0
#define PEN_DOWN_Z_MM  5.0
#define Z_SPEED_STEPS_PER_SEC 400
#define Z_ACCEL_STEPS_PER_SEC2 300

// ---------- Calibración ----------
float stepsPerMmX = 47, stepsPerMmY = 47, stepsPerMmZ = 100;
bool  invertZ = false;   
float defaultFeedrateMmMin = 250;
float rapidFeedrateMmMin   = 600;
bool  isPaused = false;

// ---------- Control del Botón Físico ----------
volatile bool buttonTriggered = false;
volatile unsigned long lastButtonPress = 0;

// Rutina de servicio de interrupción (ISR en RAM) con antirrebote de 250ms
void IRAM_ATTR buttonISR() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress > 250) {
    buttonTriggered = true;
    lastButtonPress = currentMillis;
  }
}

// ---------- Almacenamiento persistente (NVS) ----------
Preferences prefs;
unsigned long lastPositionSaveMs = 0;
long lastSavedX = 0, lastSavedY = 0, lastSavedZ = 0;

// ---------- Cola de comandos ----------
std::deque<String> cmdQueue;
bool xyMoving = false;
bool zMoving = false;

// Prototipos de funciones
void pauseSystem();
void resumeSystem();
void serviceSteppers();
void serviceQueue();
void savePositionThrottled(bool force);
void webSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length);
void sendStatus(const char* status, const String& message);
void processCommand(const String& raw);
void handleGcodeLine(String line);
void startLinearMove(float xMm, float yMm, bool rapid);
void startZMove(float zMm);
float currentXmm();
float currentYmm();
float currentZmm();

void setup() {
  Serial.begin(115200);

  // Configuración del botón con pull-up e interrupción
  pinMode(EMERGENCY_STOP_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_STOP_PIN), buttonISR, FALLING);

  prefs.begin("cncplotter", false);
  long savedX = prefs.getLong("posX", 0);
  long savedY = prefs.getLong("posY", 0);
  long savedZ = prefs.getLong("posZ", lround(PEN_UP_Z_MM * stepsPerMmZ));

  // Velocidades máximas (Ejes X e Y)
  stepperX.setMaxSpeed(400);
  stepperY.setMaxSpeed(400);
  stepperX.setCurrentPosition(savedX);
  stepperY.setCurrentPosition(savedY);
  steppersXY.addStepper(stepperX);
  steppersXY.addStepper(stepperY);
  lastSavedX = savedX; lastSavedY = savedY;

  // Velocidades y aceleración (Eje Z)
  stepperZ.setMaxSpeed(Z_SPEED_STEPS_PER_SEC);
  stepperZ.setAcceleration(Z_ACCEL_STEPS_PER_SEC2);
  stepperZ.setCurrentPosition(savedZ);
  lastSavedZ = savedZ;

  if (USE_AP_MODE) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.print("STA IP: "); Serial.println(WiFi.localIP());
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  // Manejo de la acción del botón físico
  if (buttonTriggered) {
    buttonTriggered = false;
    if (isPaused) {
      resumeSystem();
    } else {
      pauseSystem();
    }
  }

  webSocket.loop();
  serviceSteppers();     
  serviceQueue();        
  savePositionThrottled(false);  
  delay(1);                
}

void pauseSystem() {
  // 1. Detener inmediatamente el avance lógico de todos los ejes
  stepperX.moveTo(stepperX.currentPosition());
  stepperY.moveTo(stepperY.currentPosition());
  stepperZ.moveTo(stepperZ.currentPosition());
  xyMoving = false;
  zMoving = false;

  // Opcional: Si quieres que el botón borre el trabajo por completo (Parada de Emergencia total), 
  // descomenta la siguiente línea. Si prefieres que funcione como una simple Pausa, déjala comentada.
  // cmdQueue.clear();

  // 2. Marcar como pausado
  isPaused = true;

  // 3. Guardar posición actual
  savePositionThrottled(true);

  // 4. Apagar las bobinas para evitar recalentamiento
  stepperX.disableOutputs();
  stepperY.disableOutputs();
  stepperZ.disableOutputs();

  Serial.println("[ALERTA] Sistema Pausado.");
  sendStatus("error", "system_paused");
}

void resumeSystem() {
  isPaused = false;
  
  // Volver a energizar las bobinas de los motores
  stepperX.enableOutputs();
  stepperY.enableOutputs();
  stepperZ.enableOutputs();

  Serial.println("[INFO] Sistema Reanudado.");
  sendStatus("ok", "system_resumed");
}

#define POSITION_SAVE_INTERVAL_MS 3000
void savePositionThrottled(bool force) {
  long curX = stepperX.currentPosition();
  long curY = stepperY.currentPosition();
  long curZ = stepperZ.currentPosition();
  bool changed = (curX != lastSavedX || curY != lastSavedY || curZ != lastSavedZ);
  
  if (!changed) return;
  unsigned long now = millis();
  if (!force && (now - lastPositionSaveMs) < POSITION_SAVE_INTERVAL_MS) return;
  
  prefs.putLong("posX", curX);
  prefs.putLong("posY", curY);
  prefs.putLong("posZ", curZ);
  lastSavedX = curX; lastSavedY = curY; lastSavedZ = curZ;
  lastPositionSaveMs = now;
}

#define MAX_TRAVEL_XY_MM 90
float clampTravel(float v) {
  if (v < -MAX_TRAVEL_XY_MM) return -MAX_TRAVEL_XY_MM;
  if (v > MAX_TRAVEL_XY_MM) return MAX_TRAVEL_XY_MM;
  return v;
}

void webSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    webSocket.sendTXT(clientId, "{\"status\":\"ok\",\"message\":\"esp32_ready\"}");
    sendStatus("ok", "status");
  } else if (type == WStype_TEXT) {
    String msg((char*)payload, length);

    StaticJsonDocument<200> peek;
    if (deserializeJson(peek, msg) == DeserializationError::Ok) {
      const char* cmd = peek["cmd"] | "";
      
      // Control de pausa desde web
      if (strcmp(cmd, "pause") == 0) {
        if (isPaused) resumeSystem();
        else pauseSystem();
        return;
      }
      if (strcmp(cmd, "stop") == 0) {
        pauseSystem();
        cmdQueue.clear(); // El botón "Stop" de la web sí cancela definitivamente
        return;
      }
      if (strcmp(cmd, "status") == 0) {
        sendStatus("ok", "status");
        return;
      }
    }
    
    cmdQueue.push_back(msg);
    const size_t MAX_QUEUE = 8;
    while (cmdQueue.size() > MAX_QUEUE) {
      cmdQueue.pop_front();
      sendStatus("error", "queue_overflow_dropped_line");
    }
  }
}

void serviceSteppers() {
  if (xyMoving) {
    stepperX.runSpeedToPosition();  
    stepperY.runSpeedToPosition();
    if (stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
      xyMoving = false;
      sendStatus("ok", "moved");
    }
  }
  
  if (zMoving) {
    stepperZ.run();
    if (stepperZ.distanceToGo() == 0) {
      zMoving = false;
      sendStatus("ok", "pen_moved");  
    }
  }
}

bool isBusy() { return xyMoving || zMoving; }

#define MAX_STEP_RATE_SPS 400.0

void startLinearMove(float xMm, float yMm, bool rapid) {
  xMm = clampTravel(xMm);
  yMm = clampTravel(yMm);
  float feedrate = rapid ? rapidFeedrateMmMin : defaultFeedrateMmMin;
  float stepsPerSec = (feedrate / 60.0) * max(stepsPerMmX, stepsPerMmY);
  if (stepsPerSec > MAX_STEP_RATE_SPS) stepsPerSec = MAX_STEP_RATE_SPS;
  stepperX.setMaxSpeed(stepsPerSec);
  stepperY.setMaxSpeed(stepsPerSec);

  long targets[2];
  targets[0] = lround(xMm * stepsPerMmX);
  targets[1] = lround(yMm * stepsPerMmY);
  steppersXY.moveTo(targets);  
  xyMoving = true;
}

void startZMove(float zMm) {
  float signZ = invertZ ? -1.0 : 1.0;
  stepperZ.moveTo(lround(zMm * stepsPerMmZ * signZ));
  zMoving = true;
}

float currentXmm() { return stepperX.currentPosition() / stepsPerMmX; }
float currentYmm() { return stepperY.currentPosition() / stepsPerMmY; }
float currentZmm() {
  float signZ = invertZ ? -1.0 : 1.0;
  return stepperZ.currentPosition() * signZ / stepsPerMmZ;
}

void serviceQueue() {
  if (isBusy()) return;
  if (isPaused) return;
  if (cmdQueue.empty()) return;

  String raw = cmdQueue.front();
  cmdQueue.pop_front();
  processCommand(raw);
}

void processCommand(const String& raw) {
  StaticJsonDocument<400> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) { sendStatus("error", "invalid_json"); return; }

  const char* cmd = doc["cmd"] | "";
  JsonObject payload = doc["payload"];

  if (strcmp(cmd, "jog") == 0) {
    const char* axis = payload["axis"] | "X";
    float dist = payload["dist"] | 0.0;
    if (strcmp(axis, "Z") == 0) startZMove(currentZmm() + dist);
    else {
      float tx = currentXmm(), ty = currentYmm();
      if (strcmp(axis, "X") == 0) tx += dist; else ty += dist;
      startLinearMove(tx, ty, true);
    }
  } else if (strcmp(cmd, "home") == 0) {
    startZMove(PEN_UP_Z_MM);
    cmdQueue.push_front("{\"cmd\":\"__home_xy\"}");
  } else if (strcmp(cmd, "__home_xy") == 0) {
    startLinearMove(0, 0, true);
  } else if (strcmp(cmd, "set_zero") == 0) {
    stepperX.setCurrentPosition(0);
    stepperY.setCurrentPosition(0);
    stepperZ.setCurrentPosition(lround(PEN_UP_Z_MM * stepsPerMmZ * (invertZ ? -1.0 : 1.0)));
    savePositionThrottled(true);   
    sendStatus("ok", "zero_set");
  } else if (strcmp(cmd, "status") == 0) {
    sendStatus("ok", "status");
  } else if (strcmp(cmd, "gcode_line") == 0) {
    handleGcodeLine(payload["line"] | "");
  } else if (strcmp(cmd, "set_calibration") == 0) {
    float sx = payload["stepsX"] | stepsPerMmX;
    float sy = payload["stepsY"] | stepsPerMmY;
    float sz = payload["stepsZ"] | stepsPerMmZ;
    if (sx > 0) stepsPerMmX = sx;
    if (sy > 0) stepsPerMmY = sy;
    if (sz > 0) stepsPerMmZ = sz;
    invertZ = payload["invertZ"] | invertZ;
    sendStatus("ok", "calibration_saved");
  } else if (strcmp(cmd, "set_settings") == 0) {
    defaultFeedrateMmMin = payload["default"] | defaultFeedrateMmMin;
    rapidFeedrateMmMin   = payload["rapid"]   | rapidFeedrateMmMin;
    sendStatus("ok", "settings_saved");
  } else {
    sendStatus("error", "unknown_cmd");
  }
}

void handleGcodeLine(String line) {
  line.trim();
  if (line.length() == 0) { sendStatus("ok", "empty_line"); return; }

  if (line.startsWith("M03")) { startZMove(PEN_DOWN_Z_MM); return; }
  if (line.startsWith("M05")) { startZMove(PEN_UP_Z_MM); return; }
  if (line.startsWith("HOME")) {
    cmdQueue.push_front("{\"cmd\":\"home\"}");
    return;
  }
  if (line.startsWith("G00") || line.startsWith("G01")) {
    bool rapid = line.startsWith("G00");
    float x = currentXmm(), y = currentYmm();
    int xi = line.indexOf('X');
    int yi = line.indexOf('Y');
    if (xi >= 0) x = line.substring(xi + 1).toFloat();
    if (yi >= 0) y = line.substring(yi + 1).toFloat();
    startLinearMove(x, y, rapid);
    return;
  }
  sendStatus("ok", "ignored_line");
}

void sendStatus(const char* status, const String& message) {
  StaticJsonDocument<250> doc;
  doc["status"] = status;
  doc["message"] = message;
  JsonObject data = doc.createNestedObject("data");
  data["x"] = currentXmm();
  data["y"] = currentYmm();
  data["z"] = currentZmm();
  String out;
  serializeJson(doc, out);
  webSocket.broadcastTXT(out);
}