#include <HijelHID_BLEMouse.h>

// ===================================================================
// AIR MOUSE ESP32 - ETAPA 2 (Grupo 13)
// Mejora: MODO PRECISIÓN con indicación por LED RGB y buzzer
//
// - Proyecto original: Grupo 5 (Air Mouse ESP32).
// - Mejora 100% por software: NO se modifica el hardware ni el cableado.
// - Cambio de modo: mantener apretado el BOTÓN DERECHO (D25) 1 segundo.
//     * Pulsación corta  -> clic derecho normal (se envía al soltar).
//     * Pulsación larga  -> alterna Modo Normal <-> Modo Precisión.
// - LED RGB (conectado):  VERDE = Modo Normal | ROJO = Modo Precisión
//   (azul parpadeando = buscando Bluetooth, amarillo = calibrando, sin cambios)
// - Buzzer: 3 beeps cortos al entrar a Precisión, 1 beep corto al salir.
// ===================================================================

// -------------------------------------------------------------------
// MODO SIMULACIÓN (para Wokwi)
// Wokwi no puede conectarse por Bluetooth a una PC, así que el mouse
// nunca queda "conectado" y no se ve nada. Con MODO_SIMULACION en 1:
//   - El sistema se comporta como si estuviera conectado (LED verde/rojo).
//   - No se envía nada por Bluetooth: los movimientos se muestran en el
//     Monitor Serie, junto con un "cursor virtual" que acumula la posición.
// IMPORTANTE: poner en 0 antes de cargarlo en la placa física.
// -------------------------------------------------------------------
#define MODO_SIMULACION 0

// -------------------------------------------------------------------
// DIAGNÓSTICO (para la placa física)
// Con DEBUG_SERIE en 1, cada 300 ms se imprime en el Monitor Serie
// (115200 baudios) la lectura del joystick y si hay conexión Bluetooth.
// Una vez que todo funcione, se puede volver a poner en 0.
// -------------------------------------------------------------------
#define DEBUG_SERIE 0

// Instanciación usando la clase correcta de la librería
HijelBLEMouse bleMouse("ESP32 Joystick Mouse", "Maker", 100);

// -------------------------------------------------------------------
// 1. ASIGNACIÓN DE PINES (sin cambios respecto al original)
// -------------------------------------------------------------------
// Joystick KY-023
const int PIN_VRX       = 32; // Eje X (D32)
const int PIN_VRY       = 33; // Eje Y (D33)

// Botones de Clics
const int PIN_BTN_RIGHT = 25; // Clic Derecho (D25) + cambio de modo (pulsación larga)
const int PIN_BTN_LEFT  = 26; // Clic Izquierdo (D26)

// Encoder Rotativo KY-040 (Scroll)
const int PIN_ENCODER_CLK = 18; // KY-040 CLK (D18)
const int PIN_ENCODER_DT  = 19; // KY-040 DT (D19)

// LED RGB KY-009
// CORRECCIÓN Etapa 2: en el montaje los canales R y G del módulo quedaron
// intercambiados, por eso se invirtieron los GPIO 13 y 14 en el código.
const int PIN_LED_R     = 14; // Canal Rojo (D14)
const int PIN_LED_G     = 13; // Canal Verde (D13)
const int PIN_LED_B     = 27; // Canal Azul (D27)

// Buzzer Activo KY-012
const int PIN_BUZZER    = 12; // Pin de señal del Buzzer (D12)

// -------------------------------------------------------------------
// 2. PARÁMETROS DE CONTROL Y ESTADOS
// -------------------------------------------------------------------
int centerX = 2048;
int centerY = 2048;

const int DEADZONE = 500; // Zona muerta (igual en los dos modos)

// --- MEJORA: parámetros de sensibilidad por modo ---
// En el original existía una sola constante SPEED = 0.010 y un tope de ±12.
// Ahora cada modo tiene su propia ganancia (sensibilidad) y su velocidad máxima.
const float SPEED_NORMAL     = 0.010; // Igual al valor original
const int   MAX_STEP_NORMAL  = 12;    // Igual al tope original

const float SPEED_PRECISION    = 0.003; // ~3 veces menos sensible
const int   MAX_STEP_PRECISION = 4;     // Tope de velocidad más bajo para movimientos finos

const unsigned long LONG_PRESS_MS = 1000; // Tiempo de pulsación larga para cambiar de modo

// Modo actual (false = Normal, true = Precisión)
bool precisionMode = false;

// Variables de estado
bool lastLeftState    = HIGH;
bool lastRightState   = HIGH;
int lastClkState;
bool wasConnectedBefore = false;

// --- MEJORA: variables para detectar pulsación larga del botón derecho ---
unsigned long rightPressStart = 0;   // Momento en que se apretó el botón derecho
bool longPressHandled = false;       // Evita alternar el modo más de una vez por pulsación

// Variables para parpadeo asíncrono sin bloquear el programa
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// --- SIMULACIÓN: cursor virtual para ver en el Monitor Serie cuánto se mueve ---
long virtualX = 0;
long virtualY = 0;
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL_MS = 200; // Imprime cada 200 ms para no saturar

// -------------------------------------------------------------------
// FUNCIONES AUXILIARES DE HARDWARE
// -------------------------------------------------------------------

// Devuelve si hay conexión (en simulación se fuerza a "conectado")
bool estaConectado() {
#if MODO_SIMULACION
  return true;
#else
  return bleMouse.isConnected();
#endif
}

// Control del LED RGB
void setRGB(bool red, bool green, bool blue) {
  digitalWrite(PIN_LED_R, red ? HIGH : LOW);
  digitalWrite(PIN_LED_G, green ? HIGH : LOW);
  digitalWrite(PIN_LED_B, blue ? HIGH : LOW);
}

// Generación de beeps con el Buzzer Activo
void playBeep(int durationMs, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durationMs);
    digitalWrite(PIN_BUZZER, LOW);
    if (count > 1) delay(60); // Pausa entre beeps
  }
}

// --- MEJORA: muestra en el LED el modo actual (solo cuando está conectado) ---
void showModeColor() {
  if (precisionMode) {
    setRGB(true, false, false); // Rojo = Modo Precisión
  } else {
    setRGB(false, true, false); // Verde = Modo Normal
  }
}

// --- MEJORA: alterna entre Modo Normal y Modo Precisión ---
void toggleMode() {
  precisionMode = !precisionMode;

  if (estaConectado()) {
    showModeColor(); // Si está desconectado sigue el parpadeo azul
  }

  if (precisionMode) {
    playBeep(40, 3); // 3 beeps cortos: entra a Precisión
    Serial.println("-> Modo PRECISIÓN activado (SPEED = 0.003, tope = 4)");
  } else {
    playBeep(40, 1); // 1 beep corto: vuelve a Normal
    Serial.println("-> Modo NORMAL activado (SPEED = 0.010, tope = 12)");
  }
}

// Rutina de calibración dinámica de centro
void calibrateJoystick() {
  long sumX = 0, sumY = 0;
  int muestras = 100;

  for (int i = 0; i < muestras; i++) {
    sumX += analogRead(PIN_VRX);
    sumY += analogRead(PIN_VRY);
    delay(15);
  }

  centerX = sumX / muestras;
  centerY = sumY / muestras;

  Serial.print("Calibración completada -> Centro X: ");
  Serial.print(centerX);
  Serial.print(" | Centro Y: ");
  Serial.println(centerY);
}

// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db);

  // Configuración de Entradas
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);

  // Configuración de Salidas (LED RGB y Buzzer)
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Apagar salidas por seguridad
  setRGB(false, false, false);
  digitalWrite(PIN_BUZZER, LOW);

  // Estado Inicial: Amarillo (Rojo + Verde) durante la calibración
  setRGB(true, true, false);

  lastClkState = digitalRead(PIN_ENCODER_CLK);

  // Espera de 3 segundos para la calibración del joystick
  delay(3000);
  calibrateJoystick();

#if MODO_SIMULACION
  // En Wokwi NO se inicia el Bluetooth: el emulador no soporta bien la radio
  // BLE y la tarea btController se cuelga, disparando el watchdog (reinicio).
  Serial.println("*** MODO SIMULACIÓN ACTIVO: Bluetooth deshabilitado ***");
#else
  // Iniciar servicio BLE Mouse (solo en la placa física)
  bleMouse.begin();
#endif
}

// -------------------------------------------------------------------
// LOOP PRINCIPAL
// -------------------------------------------------------------------
void loop() {
  bool isConnected = estaConectado();

  // -----------------------------------------------------------------
  // A. GESTIÓN DE ESTADOS BLUETOOTH (LED RGB Y BUZZER)
  // -----------------------------------------------------------------
  if (isConnected) {
    // Transición: Recién conectado
    if (!wasConnectedBefore) {
      showModeColor();           // MEJORA: verde o rojo según el modo actual
      playBeep(40, 2);           // 2 beeps cortos de bienvenida
      wasConnectedBefore = true;
      Serial.println("-> Bluetooth Conectado!");
    }
  } else {
    // Transición: Desconectado o buscando conexión
    if (wasConnectedBefore) {
      playBeep(200, 1);          // 1 beep largo de alerta de desconexión
      wasConnectedBefore = false;
      Serial.println("-> Bluetooth Desconectado");
    }

    // Parpadeo Azul en espera de conexión (cada 500 ms)
    if (millis() - lastBlinkTime >= 500) {
      lastBlinkTime = millis();
      blinkState = !blinkState;
      setRGB(false, false, blinkState);
    }
  }

  // -----------------------------------------------------------------
  // B. LECTURA Y PROCESAMIENTO DEL JOYSTICK
  // -----------------------------------------------------------------
  // MEJORA: la sensibilidad y la velocidad máxima dependen del modo
  float speed  = precisionMode ? SPEED_PRECISION    : SPEED_NORMAL;
  int maxStep  = precisionMode ? MAX_STEP_PRECISION : MAX_STEP_NORMAL;

  int rawX = analogRead(PIN_VRX);
  int rawY = analogRead(PIN_VRY);

  int deltaX = rawX - centerX;
  int deltaY = rawY - centerY;

  int moveX = 0;
  int moveY = 0;

  if (abs(deltaX) > DEADZONE) {
    int signalX = (deltaX > 0) ? (deltaX - DEADZONE) : (deltaX + DEADZONE);
    moveX = (int)(signalX * speed);
  }

  if (abs(deltaY) > DEADZONE) {
    int signalY = (deltaY > 0) ? (deltaY - DEADZONE) : (deltaY + DEADZONE);
    moveY = (int)(signalY * speed);
  }

  moveX = constrain(moveX, -maxStep, maxStep);
  moveY = constrain(moveY, -maxStep, maxStep);

  // -----------------------------------------------------------------
  // C. LECTURA DEL ENCODER KY-040 (SCROLL)
  // -----------------------------------------------------------------
  int wheelMove = 0;
  int currentClkState = digitalRead(PIN_ENCODER_CLK);

  if (currentClkState != lastClkState && currentClkState == LOW) {
    if (digitalRead(PIN_ENCODER_DT) != currentClkState) {
      wheelMove = -1; // Abajo
    } else {
      wheelMove = 1;  // Arriba
    }
  }
  lastClkState = currentClkState;

  // -----------------------------------------------------------------
  // D. LECTURA DE BOTONES
  // -----------------------------------------------------------------
  bool currentLeftState  = digitalRead(PIN_BTN_LEFT);
  bool currentRightState = digitalRead(PIN_BTN_RIGHT);

  // MEJORA: detección de pulsación larga del botón derecho (funciona
  // conectado o desconectado, así se puede elegir el modo antes de conectar)
  if (currentRightState == LOW && lastRightState == HIGH) {
    // Se acaba de apretar: arranca a contar el tiempo
    rightPressStart = millis();
    longPressHandled = false;
  }

  if (currentRightState == LOW && !longPressHandled &&
      millis() - rightPressStart >= LONG_PRESS_MS) {
    // Se mantuvo apretado el tiempo suficiente: cambia de modo una sola vez
    toggleMode();
    longPressHandled = true;
  }

  // -----------------------------------------------------------------
  // E. ENVÍO DE ACCIONES BLE MOUSE
  // -----------------------------------------------------------------
  if (isConnected) {
#if MODO_SIMULACION
    // SIMULACIÓN: en vez de mandar por Bluetooth, se acumula en el cursor
    // virtual y se imprime en el Monitor Serie
    virtualX += moveX;
    virtualY += moveY;

    if ((moveX != 0 || moveY != 0) && millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
      lastPrintTime = millis();
      Serial.print(precisionMode ? "[PRECISION] " : "[NORMAL]    ");
      Serial.print("rawX: ");     Serial.print(rawX);
      Serial.print(" | moveX: "); Serial.print(moveX);
      Serial.print(" | moveY: "); Serial.print(moveY);
      Serial.print(" | Cursor virtual: (");
      Serial.print(virtualX); Serial.print(", "); Serial.print(virtualY);
      Serial.println(")");
    }

    if (wheelMove != 0) {
      Serial.print("Scroll: "); Serial.println(wheelMove);
    }

    if (currentLeftState == LOW && lastLeftState == HIGH) {
      Serial.println("Clic IZQUIERDO");
      delay(30);
    }

    if (currentRightState == HIGH && lastRightState == LOW && !longPressHandled) {
      Serial.println("Clic DERECHO");
      delay(30);
    }
#else
#if DEBUG_SERIE
    // DIAGNÓSTICO: muestra lo que lee el joystick y lo que se envía
    if (millis() - lastPrintTime >= 300) {
      lastPrintTime = millis();
      Serial.print(precisionMode ? "[PRECISION] " : "[NORMAL]    ");
      Serial.print("rawX: ");     Serial.print(rawX);
      Serial.print(" | rawY: ");  Serial.print(rawY);
      Serial.print(" | centro: ("); Serial.print(centerX);
      Serial.print(", ");         Serial.print(centerY);
      Serial.print(") | moveX: "); Serial.print(moveX);
      Serial.print(" | moveY: "); Serial.print(moveY);
      Serial.print(" | conectado: ");
      Serial.println(bleMouse.isConnected() ? "SI" : "NO");
    }
#endif

    // Mover cursor (X e Y)
    if (moveX != 0 || moveY != 0) {
      bleMouse.move(moveX, moveY);
    }

    // Mover Scroll (Encoder)
    if (wheelMove != 0) {
      bleMouse.scroll(wheelMove);
    }

    // Clic Izquierdo (Pin 26): sin cambios, se envía al apretar
    if (currentLeftState == LOW && lastLeftState == HIGH) {
      bleMouse.click(MouseButton::Left);
      delay(30);
    }

    // MEJORA: Clic Derecho (Pin 25) ahora se envía al SOLTAR y solo si
    // fue una pulsación corta (si fue larga, ya se usó para cambiar de modo)
    if (currentRightState == HIGH && lastRightState == LOW && !longPressHandled) {
      bleMouse.click(MouseButton::Right);
      delay(30);
    }
#endif
  }

  lastLeftState  = currentLeftState;
  lastRightState = currentRightState;

  delay(5);
}
