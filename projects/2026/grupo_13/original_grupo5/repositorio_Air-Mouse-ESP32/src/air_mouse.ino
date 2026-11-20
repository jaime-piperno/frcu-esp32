#include <HijelHID_BLEMouse.h>

// Instanciación usando la clase correcta de la librería
HijelBLEMouse bleMouse("ESP32 Joystick Mouse", "Maker", 100);

// -------------------------------------------------------------------
// 1. ASIGNACIÓN DE PINES
// -------------------------------------------------------------------
// Joystick KY-023
const int PIN_VRX       = 32; // Eje X (D32)
const int PIN_VRY       = 33; // Eje Y (D33)

// Botones de Clics
const int PIN_BTN_RIGHT = 25; // Clic Derecho (D25)
const int PIN_BTN_LEFT  = 26; // Clic Izquierdo (D26)

// Encoder Rotativo KY-040 (Scroll)
const int PIN_ENCODER_CLK = 18; // KY-040 CLK (D18)
const int PIN_ENCODER_DT  = 19; // KY-040 DT (D19)

// LED RGB KY-009
const int PIN_LED_R     = 13; // Canal Rojo (D13)
const int PIN_LED_G     = 14; // Canal Verde (D14)
const int PIN_LED_B     = 27; // Canal Azul (D27)

// Buzzer Activo KY-012
const int PIN_BUZZER    = 12; // Pin de señal del Buzzer (D12)

// -------------------------------------------------------------------
// 2. PARÁMETROS DE CONTROL Y ESTADOS
// -------------------------------------------------------------------
int centerX = 2048;
int centerY = 2048;

const int DEADZONE = 500;
const float SPEED  = 0.010;

// Variables de estado
bool lastLeftState    = HIGH;
bool lastRightState   = HIGH;
int lastClkState;
bool wasConnectedBefore = false;

// Variables para parpadeo asíncrono sin bloquear el programa
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// -------------------------------------------------------------------
// FUNCIONES AUXILIARES DE HARDWARE
// -------------------------------------------------------------------

// Control del LED RGB
void setRGB(bool red, bool green, bool blue) {
  digitalWrite(PIN_LED_R, red ? HIGH : LOW);
  digitalWrite(PIN_LED_G, green ? HIGH : LOW);
  digitalWrite(PIN_LED_B, blue ? HIGH : LOW);
}

// Generación de beeps tonales con el Buzzer Activo
void playBeep(int durationMs, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durationMs);
    digitalWrite(PIN_BUZZER, LOW);
    if (count > 1) delay(60); // Pausa entre beeps
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

  // Iniciar servicio BLE Mouse
  bleMouse.begin();
}

// -------------------------------------------------------------------
// LOOP PRINCIPAL
// -------------------------------------------------------------------
void loop() {
  bool isConnected = bleMouse.isConnected();

  // -----------------------------------------------------------------
  // A. GESTIÓN DE ESTADOS BLUETOOTH (LED RGB Y BUZZER)
  // -----------------------------------------------------------------
  if (isConnected) {
    // Transición: Recién conectado
    if (!wasConnectedBefore) {
      setRGB(false, true, false); // Verde fijo
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
  int rawX = analogRead(PIN_VRX);
  int rawY = analogRead(PIN_VRY);

  int deltaX = rawX - centerX;
  int deltaY = rawY - centerY;

  int moveX = 0;
  int moveY = 0;

  if (abs(deltaX) > DEADZONE) {
    int signalX = (deltaX > 0) ? (deltaX - DEADZONE) : (deltaX + DEADZONE);
    moveX = (int)(signalX * SPEED);
  }

  if (abs(deltaY) > DEADZONE) {
    int signalY = (deltaY > 0) ? (deltaY - DEADZONE) : (deltaY + DEADZONE);
    moveY = (int)(signalY * SPEED);
  }

  moveX = constrain(moveX, -12, 12);
  moveY = constrain(moveY, -12, 12);

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

// -----------------------------------------------------------------
  // E. ENVÍO DE ACCIONES BLE MOUSE
  // -----------------------------------------------------------------
  if (isConnected) {
    // Mover cursor (X e Y)
    if (moveX != 0 || moveY != 0) {
      bleMouse.move(moveX, moveY);
    }

    // Mover Scroll (Encoder)
    if (wheelMove != 0) {
      bleMouse.scroll(wheelMove);
    }

    // Clic Izquierdo (Pin 26) usando MouseButton::Left
    if (currentLeftState == LOW && lastLeftState == HIGH) {
      bleMouse.click(MouseButton::Left);
      delay(30);
    }

    // Clic Derecho (Pin 25) usando MouseButton::Right
    if (currentRightState == LOW && lastRightState == HIGH) {
      bleMouse.click(MouseButton::Right);
      delay(30);
    }
  }

  lastLeftState  = currentLeftState;
  lastRightState = currentRightState;

  delay(5);
}
