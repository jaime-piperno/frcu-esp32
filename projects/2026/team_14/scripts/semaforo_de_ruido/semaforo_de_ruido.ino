/*
  ============================================================
  MONITOR DE RUIDO - SEMAFORO RGB + BUZZER + LCD (ESP32 + KY-037)
  ============================================================

  CONEXIONES (ESP32 DevKit V1)
  ------------------------------------------------------------
  Componente          Senal        Pin ESP32   Alimentacion
  ------------------------------------------------------------
  Microfono KY-037    A0           GPIO34      3V3  (nunca 5 V)
                      D0           sin usar
  LED RGB (catodo     R            GPIO26      catodo a GND
  comun, 220 ohm)     G            GPIO25
                      B            GPIO27
  Buzzer              senal        GPIO23      3V3 o 5 V
  Encoder rotativo    CLK          GPIO18      3V3
                      DT           GPIO19
                      SW           GPIO21
  LCD 1602 I2C        SDA          GPIO32      VIN (5 V)
                      SCL          GPIO33
  ------------------------------------------------------------
  GPIO34 es solo entrada y no tiene resistencias internas: sirve
  para el microfono, no para salidas. El I2C no usa los GPIO21 y
  GPIO22 por defecto porque el GPIO21 lo ocupa el boton del encoder.
  El LCD trabaja a 5 V y sus resistencias pull-up dejan SDA y SCL en
  5 V, por lo que corresponde intercalar un conversor de nivel a 3,3 V.

  FUNCIONAMIENTO
  ------------------------------------------------------------
  - Amplitud pico a pico en ventanas de 50 ms (cancela el DC).
  - Piso de silencio calibrado al arrancar y con el boton del encoder.
  - Umbrales = piso + margen - sensibilidad * 2, con histeresis.
  - Encoder por tabla de cuadratura (CLK y DT) protegida con portMUX.
  - Buzzer en FUERTE; no se mide mientras suena.
  - LED RGB por PWM, actualizado solo al cambiar de nivel.
  - LCD 1602 I2C:
      fila 0: nivel + amplitud, o aviso temporal (sensibilidad / calibracion)
      fila 1: barra de ruido con 80 segmentos y marcas de umbral
              (punteada = amarillo, llena = rojo)

  REQUISITOS
  ------------------------------------------------------------
  Core ESP32 2.0.3+ o 3.x.
  Libreria "LiquidCrystal I2C" (Frank de Brabander).
  ============================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- PINES (ver la tabla del encabezado) ----------------
const uint8_t PIN_MIC     = 34;  // solo entrada, ADC1
const uint8_t PIN_LED_R   = 26;
const uint8_t PIN_LED_G   = 25;
const uint8_t PIN_LED_B   = 27;
const uint8_t PIN_BUZZER  = 23;
const uint8_t PIN_ENC_CLK = 18;
const uint8_t PIN_ENC_DT  = 19;
const uint8_t PIN_ENC_SW  = 21;
const uint8_t PIN_I2C_SDA = 32;
const uint8_t PIN_I2C_SCL = 33;

// ---------------- MICROFONO ----------------
// 50 ms abarcan varios ciclos de las frecuencias audibles mas graves, asi que la
// amplitud pico a pico se mantiene estable de una ventana a la siguiente.
const uint32_t SAMPLE_WINDOW_MS = 50;
// ADC_11db: fondo de escala ~2,45 V. Es la atenuacion con la que quedo ajustado el
// potenciometro del KY-037 y a la que corresponden los margenes de mas abajo. Con
// ADC_0db (~950 mV) se gana resolucion, pero hay que rehacer la calibracion entera
// y recalcular MARGEN_AMARILLO_BASE y MARGEN_ROJO_BASE.
const adc_attenuation_t MIC_ATTEN = ADC_11db;
const uint16_t ADC_MAX = 4095;
// Rango aceptable del offset de continua: fuera de el la senal se recorta contra
// alguno de los extremos del ADC y la amplitud medida deja de ser confiable.
const uint16_t DC_MIN  = 200;
const uint16_t DC_MAX  = 3900;

// Declarado aca arriba: el IDE de Arduino inserta los prototipos
// antes de la primera funcion, y el tipo tiene que existir antes.
struct Ventana {
  uint16_t minimo;
  uint16_t maximo;
  uint32_t suma;
  uint32_t n;
};

// ---------------- CALIBRACION ----------------
const uint8_t  VENTANAS_CALIBRACION      = 15;
const uint16_t MARGEN_SEGURIDAD_SILENCIO = 15;
const uint32_t ESPERA_PRE_CALIBRACION_MS = 600;

// ---------------- UMBRALES ----------------
// Los umbrales son relativos al piso de silencio medido en la calibracion y no
// valores absolutos, asi el semaforo se adapta al ambiente en cada encendido:
//   umbral amarillo = piso + MARGEN_AMARILLO_BASE - sensibilidad * FACTOR_SENSIBILIDAD
//   umbral rojo     = piso + MARGEN_ROJO_BASE     - sensibilidad * FACTOR_SENSIBILIDAD
// Para reajustarlos: abrir el Monitor Serie a 115200 y anotar la Amplitud en
// silencio, hablando normal, hablando fuerte y con musica fuerte; cada margen va
// a mitad de camino entre las dos situaciones que se quieren separar.
const int32_t MARGEN_AMARILLO_BASE = 35;  // amplitud sobre el piso que pasa a MODERADO
const int32_t MARGEN_ROJO_BASE     = 95;  // amplitud sobre el piso que pasa a FUERTE
// Cuanto hay que bajar respecto del umbral para retroceder de nivel. Evita que el
// LED oscile entre dos colores cuando el ruido se queda justo en el limite.
const int32_t HISTERESIS           = 12;
// Cuanto corre cada detente del encoder a los dos umbrales (horario = mas sensible).
const int32_t FACTOR_SENSIBILIDAD  = 2;
const int32_t SENS_RANGO = 10;
// Tope superior: el punto donde el amarillo toca su limite (piso + H + 1).
// Mas alla girar el encoder no cambiaria nada (zona muerta).
const int32_t SENS_MAX_LIMITE = (MARGEN_AMARILLO_BASE - HISTERESIS - 1) / FACTOR_SENSIBILIDAD;
const int32_t SENS_MAX = (SENS_MAX_LIMITE < SENS_RANGO) ? SENS_MAX_LIMITE : SENS_RANGO;
const int32_t SENS_MIN = -SENS_RANGO;

static_assert(MARGEN_AMARILLO_BASE >= HISTERESIS + 1,
              "MARGEN_AMARILLO_BASE debe ser >= HISTERESIS + 1");
static_assert(MARGEN_ROJO_BASE - MARGEN_AMARILLO_BASE >= HISTERESIS + 1,
              "MARGEN_ROJO_BASE debe superar a MARGEN_AMARILLO_BASE en al menos HISTERESIS + 1");

uint16_t baselineNoise = 0;
int32_t  sensibilidad  = 0;

enum NivelRuido { SILENCIO, MODERADO, FUERTE };
NivelRuido nivelActual    = SILENCIO;
uint16_t   ultimaAmplitud = 0;

// ---------------- LED RGB (catodo comun, PWM 0-255) ----------------
struct Color { uint8_t r, g, b; };
const Color COLOR_APAGADO  = {  0,   0,   0};
const Color COLOR_VERDE    = {  0, 255,   0};
const Color COLOR_AMARILLO = {255, 120,   0};  // ajustar a ojo
const Color COLOR_ROJO     = {255,   0,   0};
const Color COLOR_AZUL     = {  0,   0, 255};

// ---------------- ENCODER ----------------
const int32_t PASOS_POR_DETENTE = 4;
const bool    INVERTIR_ENCODER  = false;  // true si el sentido horario baja la sensibilidad
DRAM_ATTR const int8_t TABLA_CUADRATURA[16] = {  // en RAM: se lee desde la ISR
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
portMUX_TYPE     encoderMux    = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t encoderPasos  = 0;
volatile uint8_t encoderEstado = 0;

volatile bool  botonPresionado = false;
uint32_t       ultimoBoton     = 0;
const uint32_t DEBOUNCE_BOTON_MS = 250;

// ---------------- BUZZER ----------------
const bool     BUZZER_ACTIVO         = false;  // true: activo (HIGH/LOW) | false: pasivo (tone)
const bool     BUZZER_ACTIVO_EN_ALTO = true;   // false si el modulo suena con LOW
const uint32_t BUZZER_FREQ_HZ        = 2000;
const uint32_t BUZZER_ON_MS          = 150;
const uint32_t BUZZER_OFF_MS         = 150;
const uint32_t ESPERA_POST_BUZZER_MS = 40;
bool     buzzerEstado       = false;
uint32_t buzzerUltimoCambio = 0;

static_assert(BUZZER_OFF_MS >= ESPERA_POST_BUZZER_MS + SAMPLE_WINDOW_MS,
              "BUZZER_OFF_MS debe alojar la espera post-buzzer y una ventana completa");

// ---------------- LCD 1602 I2C ----------------
const uint8_t  LCD_DIRECCION = 0;          // 0 = autodetectar (0x27, 0x3F, 0x20-0x26, 0x38-0x3E)
const uint32_t I2C_FREQ_HZ   = 100000;     // el PCF8574 esta especificado a 100 kHz
const uint8_t  LCD_COLS      = 16;
const uint8_t  LCD_FILAS     = 2;
const uint32_t LCD_REFRESCO_MS             = 50;
const uint16_t LCD_MAX_CELDAS_POR_REFRESCO = 6;     // acota el tiempo bloqueado por I2C
const uint32_t LCD_AMPLITUD_MS             = 250;   // cada cuanto cambia el numero de la fila 0
const uint32_t LCD_AVISO_SENS_MS           = 1500;
const uint32_t LCD_AVISO_CALIB_MS          = 2000;
const uint32_t LCD_AVISO_BLOQUEANTE_MS     = 60000; // se reemplaza al terminar la calibracion

const int32_t BARRA_SUBDIV       = 5;                        // columnas de pixeles por celda
const int32_t BARRA_SEGMENTOS    = LCD_COLS * BARRA_SUBDIV;  // 80
const int32_t BARRA_SEG_ROJO_REF = 60;  // umbral rojo con sensibilidad 0 -> celda 12
const int32_t BARRA_CAIDA        = 3;   // segmentos que baja la barra por refresco

static_assert(BARRA_SEG_ROJO_REF > 0 && BARRA_SEG_ROJO_REF < BARRA_SEGMENTOS,
              "BARRA_SEG_ROJO_REF debe estar dentro de la barra");

// Caracteres personalizados (se evita el codigo 0)
const uint8_t CH_P1         = 1;
const uint8_t CH_P2         = 2;
const uint8_t CH_P3         = 3;
const uint8_t CH_P4         = 4;
const uint8_t CH_LLENO      = 5;
const uint8_t CH_MARCA_AM   = 6;
const uint8_t CH_MARCA_ROJO = 7;

uint8_t GLIFO_P1[8]         = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
uint8_t GLIFO_P2[8]         = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18};
uint8_t GLIFO_P3[8]         = {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C};
uint8_t GLIFO_P4[8]         = {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E};
uint8_t GLIFO_LLENO[8]      = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
uint8_t GLIFO_MARCA_AM[8]   = {0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00};
uint8_t GLIFO_MARCA_ROJO[8] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};

LiquidCrystal_I2C *lcd = nullptr;
bool     lcdDisponible = false;
uint8_t  lcdActual[LCD_FILAS][LCD_COLS];
uint8_t  lcdDeseado[LCD_FILAS][LCD_COLS];
char     textoAviso[LCD_FILAS][LCD_COLS + 1];
bool     avisoActivo[LCD_FILAS]   = {false, false};
uint32_t avisoDesde[LCD_FILAS]    = {0, 0};
uint32_t avisoDuracion[LCD_FILAS] = {0, 0};
uint32_t ultimoRefrescoLcd   = 0;
uint32_t ultimoTextoAmplitud = 0;
uint16_t amplitudMostrada    = 0;
int32_t  barraSegmentos      = 0;

// ---------------- DEBUG ----------------
const uint32_t PRINT_INTERVAL_MS = 300;
uint32_t ultimoPrint = 0;

// ============================================================
// ISR
// ============================================================
void IRAM_ATTR isrEncoder() {
  portENTER_CRITICAL_ISR(&encoderMux);
  uint8_t actual = (uint8_t)((digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT));
  uint8_t idx = (uint8_t)(((encoderEstado << 2) | actual) & 0x0F);
  encoderPasos += TABLA_CUADRATURA[idx];
  encoderEstado = actual;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

void IRAM_ATTR isrBoton() {
  botonPresionado = true;
}

// ============================================================
// LED
// ============================================================
void setColor(const Color &c) {
  analogWrite(PIN_LED_R, c.r);
  analogWrite(PIN_LED_G, c.g);
  analogWrite(PIN_LED_B, c.b);
}

const Color &colorDe(NivelRuido nivel) {
  switch (nivel) {
    case MODERADO: return COLOR_AMARILLO;
    case FUERTE:   return COLOR_ROJO;
    default:       return COLOR_VERDE;
  }
}

const char *nombreNivel(NivelRuido nivel) {
  switch (nivel) {
    case MODERADO: return "MODERADO";
    case FUERTE:   return "FUERTE";
    default:       return "SILENCIO";
  }
}

// ============================================================
// BUZZER
// ============================================================
void buzzerSalida(bool encendido) {
  if (BUZZER_ACTIVO) {
    digitalWrite(PIN_BUZZER, (encendido == BUZZER_ACTIVO_EN_ALTO) ? HIGH : LOW);
  } else if (encendido) {
    tone(PIN_BUZZER, BUZZER_FREQ_HZ);
  } else {
    noTone(PIN_BUZZER);
  }
}

void encenderBeep() {
  buzzerEstado = true;
  buzzerUltimoCambio = millis();
  buzzerSalida(true);
}

void apagarBuzzer() {
  buzzerEstado = false;
  buzzerUltimoCambio = millis();
  buzzerSalida(false);
}

void manejarBuzzer() {
  if (nivelActual != FUERTE) return;
  uint32_t t = millis() - buzzerUltimoCambio;
  if (buzzerEstado && t >= BUZZER_ON_MS) {
    apagarBuzzer();
  } else if (!buzzerEstado && t >= BUZZER_OFF_MS) {
    encenderBeep();
  }
}

// Solo se mide si el buzzer esta apagado, pasaron 40 ms desde que se apago
// y la ventana completa termina antes del proximo pitido.
bool puedeMedir() {
  if (nivelActual != FUERTE) return true;
  if (buzzerEstado) return false;
  uint32_t t = millis() - buzzerUltimoCambio;
  return t >= ESPERA_POST_BUZZER_MS && t + SAMPLE_WINDOW_MS <= BUZZER_OFF_MS;
}

// ============================================================
// MEDICION
// ============================================================
// Una lectura suelta no dice nada del nivel de ruido: depende del instante de la
// onda en que cayo el muestreo. Midiendo el maximo y el minimo durante 50 ms se
// obtiene la amplitud pico a pico, proporcional al volumen y sin el offset de
// continua del modulo, que se cancela en la resta.
Ventana medirVentana() {
  Ventana v = {ADC_MAX, 0, 0, 0};
  uint32_t inicio = millis();
  while (millis() - inicio < SAMPLE_WINDOW_MS) {
    uint16_t m = analogRead(PIN_MIC);
    if (m > v.maximo) v.maximo = m;
    if (m < v.minimo) v.minimo = m;
    v.suma += m;
    v.n++;
  }
  return v;
}

uint16_t amplitudDe(const Ventana &v) {
  return v.n ? (uint16_t)(v.maximo - v.minimo) : 0;
}

// ============================================================
// NIVELES
// ============================================================
void calcularUmbrales(int32_t &umbralAmarillo, int32_t &umbralRojo) {
  int32_t ajuste = sensibilidad * FACTOR_SENSIBILIDAD;
  umbralAmarillo = (int32_t)baselineNoise + MARGEN_AMARILLO_BASE - ajuste;
  umbralRojo     = (int32_t)baselineNoise + MARGEN_ROJO_BASE     - ajuste;
  // El amarillo nunca puede quedar tan cerca del piso que la histeresis
  // impida volver a SILENCIO; el rojo se desplaza lo mismo para no
  // colapsar la banda amarilla.
  int32_t minimoAmarillo = (int32_t)baselineNoise + HISTERESIS + 1;
  if (umbralAmarillo < minimoAmarillo) {
    umbralRojo    += minimoAmarillo - umbralAmarillo;
    umbralAmarillo = minimoAmarillo;
  }
  if (umbralRojo <= umbralAmarillo + HISTERESIS) umbralRojo = umbralAmarillo + HISTERESIS + 1;
}

NivelRuido evaluarNivel(uint16_t amplitud) {
  int32_t ua, ur;
  calcularUmbrales(ua, ur);
  int32_t a = amplitud;

  switch (nivelActual) {
    case SILENCIO:
      if (a >= ur) return FUERTE;
      if (a >= ua) return MODERADO;
      return SILENCIO;
    case MODERADO:
      if (a >= ur) return FUERTE;
      if (a < ua - HISTERESIS) return SILENCIO;
      return MODERADO;
    case FUERTE:
      if (a < ur - HISTERESIS) {
        if (a >= ua - HISTERESIS) return MODERADO;
        return SILENCIO;
      }
      return FUERTE;
  }
  return SILENCIO;
}

void cambiarNivel(NivelRuido nuevo) {
  if (nuevo == nivelActual) return;
  NivelRuido anterior = nivelActual;
  nivelActual = nuevo;
  setColor(colorDe(nuevo));
  if (nuevo == FUERTE) {
    encenderBeep();
  } else if (anterior == FUERTE) {
    apagarBuzzer();
  }
}

// ============================================================
// LCD
// ============================================================
bool dispositivoI2CPresente(uint8_t direccion) {
  Wire.beginTransmission(direccion);
  return Wire.endTransmission() == 0;
}

uint8_t detectarDireccionLcd() {
  if (LCD_DIRECCION != 0) {
    return dispositivoI2CPresente(LCD_DIRECCION) ? LCD_DIRECCION : 0;
  }
  if (dispositivoI2CPresente(0x27)) return 0x27;
  if (dispositivoI2CPresente(0x3F)) return 0x3F;
  for (uint8_t d = 0x20; d <= 0x26; d++) {
    if (dispositivoI2CPresente(d)) return d;
  }
  for (uint8_t d = 0x38; d <= 0x3E; d++) {
    if (dispositivoI2CPresente(d)) return d;
  }
  return 0;
}

void iniciarLcd() {
  // Se inicia el bus antes que la libreria: su Wire.begin() sin argumentos
  // no cambia los pines si el bus ya esta iniciado.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  uint8_t direccion = detectarDireccionLcd();
  if (direccion == 0) {
    Serial.println(F("!! LCD no encontrado: se sigue sin pantalla"));
    return;
  }

  lcd = new LiquidCrystal_I2C(direccion, LCD_COLS, LCD_FILAS);
  lcd->init();
  lcd->backlight();
  lcd->createChar(CH_P1,         GLIFO_P1);
  lcd->createChar(CH_P2,         GLIFO_P2);
  lcd->createChar(CH_P3,         GLIFO_P3);
  lcd->createChar(CH_P4,         GLIFO_P4);
  lcd->createChar(CH_LLENO,      GLIFO_LLENO);
  lcd->createChar(CH_MARCA_AM,   GLIFO_MARCA_AM);
  lcd->createChar(CH_MARCA_ROJO, GLIFO_MARCA_ROJO);

  // init() deja la pantalla en blanco
  for (uint8_t f = 0; f < LCD_FILAS; f++) {
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      lcdActual[f][c]  = ' ';
      lcdDeseado[f][c] = ' ';
    }
  }

  lcdDisponible = true;
  Serial.print(F(">> LCD en 0x"));
  Serial.println(direccion, HEX);
}

void cargarFila(uint8_t fila, const char *texto) {
  uint8_t c = 0;
  for (; c < LCD_COLS && texto[c] != '\0'; c++) lcdDeseado[fila][c] = (uint8_t)texto[c];
  for (; c < LCD_COLS; c++) lcdDeseado[fila][c] = ' ';
}

void mostrarAviso(uint8_t fila, const char *texto, uint32_t duracionMs) {
  if (fila >= LCD_FILAS) return;
  strncpy(textoAviso[fila], texto, LCD_COLS);
  textoAviso[fila][LCD_COLS] = '\0';
  avisoDesde[fila]    = millis();
  avisoDuracion[fila] = duracionMs;
  avisoActivo[fila]   = true;
}

// Escala fija (independiente de la sensibilidad): 0 = maximo medido en
// silencio, BARRA_SEG_ROJO_REF = umbral rojo con sensibilidad 0.
// Asi las marcas de umbral se mueven al girar el encoder.
int32_t segmentosDe(int32_t valor) {
  int32_t base    = (int32_t)baselineNoise - MARGEN_SEGURIDAD_SILENCIO;
  int32_t rojoRef = (int32_t)baselineNoise + MARGEN_ROJO_BASE;
  int32_t rango   = rojoRef - base;
  int32_t s = (valor - base) * BARRA_SEG_ROJO_REF / rango;
  return constrain(s, (int32_t)0, BARRA_SEGMENTOS);
}

void actualizarBarra() {
  int32_t objetivo = segmentosDe(ultimaAmplitud);
  if (objetivo >= barraSegmentos) {
    barraSegmentos = objetivo;
  } else {
    int32_t bajada = barraSegmentos - BARRA_CAIDA;
    barraSegmentos = (bajada > objetivo) ? bajada : objetivo;
  }
}

void armarBarra() {
  int32_t ua, ur;
  calcularUmbrales(ua, ur);
  int32_t celdaAmarillo = segmentosDe(ua) / BARRA_SUBDIV;
  int32_t celdaRojo     = segmentosDe(ur) / BARRA_SUBDIV;

  for (int32_t c = 0; c < LCD_COLS; c++) {
    int32_t lleno = constrain(barraSegmentos - c * BARRA_SUBDIV, (int32_t)0, BARRA_SUBDIV);
    uint8_t glifo;
    if (lleno >= BARRA_SUBDIV) {
      glifo = CH_LLENO;
    } else if (lleno > 0) {
      glifo = (uint8_t)(CH_P1 + lleno - 1);
    } else if (c == celdaRojo) {
      glifo = CH_MARCA_ROJO;
    } else if (c == celdaAmarillo) {
      glifo = CH_MARCA_AM;
    } else {
      glifo = ' ';
    }
    lcdDeseado[1][c] = glifo;
  }
}

void armarPantalla() {
  uint32_t ahora = millis();
  for (uint8_t f = 0; f < LCD_FILAS; f++) {
    if (avisoActivo[f] && ahora - avisoDesde[f] >= avisoDuracion[f]) avisoActivo[f] = false;
  }

  if (ahora - ultimoTextoAmplitud >= LCD_AMPLITUD_MS) {
    ultimoTextoAmplitud = ahora;
    amplitudMostrada = ultimaAmplitud;
  }

  if (avisoActivo[0]) {
    cargarFila(0, textoAviso[0]);
  } else {
    char linea[LCD_COLS + 1];
    snprintf(linea, sizeof(linea), "%-8.8s  A%5u", nombreNivel(nivelActual),
             (unsigned)amplitudMostrada);
    cargarFila(0, linea);
  }

  if (avisoActivo[1]) {
    cargarFila(1, textoAviso[1]);
  } else {
    armarBarra();
  }
}

// Escribe solo las celdas que cambiaron, hasta maxCeldas por llamada.
void volcarPantalla(uint16_t maxCeldas) {
  uint16_t escritas = 0;
  for (uint8_t f = 0; f < LCD_FILAS; f++) {
    bool cursorEnPosicion = false;
    for (uint8_t c = 0; c < LCD_COLS; c++) {
      if (lcdDeseado[f][c] == lcdActual[f][c]) {
        cursorEnPosicion = false;
        continue;
      }
      if (escritas >= maxCeldas) return;
      if (!cursorEnPosicion) {
        lcd->setCursor(c, f);
        cursorEnPosicion = true;
      }
      lcd->write(lcdDeseado[f][c]);
      lcdActual[f][c] = lcdDeseado[f][c];
      escritas++;
    }
  }
}

void refrescarLcd() {
  if (!lcdDisponible) return;
  uint32_t ahora = millis();
  if (ahora - ultimoRefrescoLcd < LCD_REFRESCO_MS) return;
  ultimoRefrescoLcd = ahora;
  actualizarBarra();
  armarPantalla();
  volcarPantalla(LCD_MAX_CELDAS_POR_REFRESCO);
}

void forzarRefrescoLcd() {
  if (!lcdDisponible) return;
  armarPantalla();
  volcarPantalla(LCD_FILAS * LCD_COLS);
  ultimoRefrescoLcd = millis();
}

// ============================================================
// CALIBRACION
// ============================================================
void calibrarSilencio() {
  apagarBuzzer();
  setColor(COLOR_AZUL);
  Serial.println(F(">> Calibrando piso de silencio... no hagas ruido"));

  // La pantalla se escribe antes de la espera: no hay trafico I2C durante el muestreo
  mostrarAviso(0, "Calibrando...", LCD_AVISO_BLOQUEANTE_MS);
  mostrarAviso(1, "No hagas ruido", LCD_AVISO_BLOQUEANTE_MS);
  forzarRefrescoLcd();

  delay(ESPERA_PRE_CALIBRACION_MS);

  uint16_t maxAmp    = 0;
  uint16_t minGlobal = ADC_MAX;
  uint16_t maxGlobal = 0;
  uint64_t suma      = 0;
  uint64_t n         = 0;

  for (uint8_t i = 0; i < VENTANAS_CALIBRACION; i++) {
    Ventana v = medirVentana();
    uint16_t a = amplitudDe(v);
    if (a > maxAmp) maxAmp = a;
    if (v.minimo < minGlobal) minGlobal = v.minimo;
    if (v.maximo > maxGlobal) maxGlobal = v.maximo;
    suma += v.suma;
    n    += v.n;
  }

  baselineNoise = maxAmp + MARGEN_SEGURIDAD_SILENCIO;
  uint16_t dc = n ? (uint16_t)(suma / n) : 0;

  Serial.print(F(">> Piso de silencio: "));
  Serial.print(baselineNoise);
  Serial.print(F(" | DC: "));
  Serial.print(dc);
  Serial.print(F(" | Rango ADC: "));
  Serial.print(minGlobal);
  Serial.print(F("-"));
  Serial.println(maxGlobal);

  bool saturado = (maxGlobal >= ADC_MAX || minGlobal == 0);
  bool dcFuera  = (dc < DC_MIN || dc > DC_MAX);
  if (saturado) {
    Serial.println(F("!! ADC saturado: ajustar el potenciometro o la atenuacion"));
  }
  if (dcFuera) {
    Serial.println(F("!! DC fuera de 200-3900: la senal puede recortarse"));
  }

  char linea[LCD_COLS + 1];
  if (saturado) {
    mostrarAviso(0, "!ADC saturado", LCD_AVISO_CALIB_MS);
  } else if (dcFuera) {
    mostrarAviso(0, "!DC fuera rango", LCD_AVISO_CALIB_MS);
  } else {
    mostrarAviso(0, "Piso calibrado", LCD_AVISO_CALIB_MS);
  }
  snprintf(linea, sizeof(linea), "Piso%4u DC%5u", (unsigned)baselineNoise, (unsigned)dc);
  mostrarAviso(1, linea, LCD_AVISO_CALIB_MS);

  ultimaAmplitud   = 0;
  amplitudMostrada = 0;
  barraSegmentos   = 0;

  nivelActual = SILENCIO;
  setColor(colorDe(nivelActual));
  forzarRefrescoLcd();
}

// ============================================================
// ENCODER
// ============================================================
void manejarEncoder() {
  int32_t detentes;
  portENTER_CRITICAL(&encoderMux);
  detentes = encoderPasos / PASOS_POR_DETENTE;
  encoderPasos -= detentes * PASOS_POR_DETENTE;
  portEXIT_CRITICAL(&encoderMux);

  if (detentes != 0) {
    if (INVERTIR_ENCODER) detentes = -detentes;
    int32_t nueva = constrain(sensibilidad + detentes, SENS_MIN, SENS_MAX);
    char linea[LCD_COLS + 1];
    if (nueva != sensibilidad) {
      sensibilidad = nueva;
      int32_t ua, ur;
      calcularUmbrales(ua, ur);
      Serial.print(F(">> Sensibilidad: "));
      Serial.print(sensibilidad);
      Serial.print(F(" | Umbral amarillo: "));
      Serial.print(ua);
      Serial.print(F(" | Umbral rojo: "));
      Serial.println(ur);
      snprintf(linea, sizeof(linea), "Sensibilidad %+3d", (int)sensibilidad);
    } else {
      snprintf(linea, sizeof(linea), "Sens. %s %+3d",
               (detentes > 0) ? "maxima" : "minima", (int)sensibilidad);
    }
    mostrarAviso(0, linea, LCD_AVISO_SENS_MS);
  }

  if (botonPresionado) {
    botonPresionado = false;
    uint32_t ahora = millis();
    if (ahora - ultimoBoton >= DEBOUNCE_BOTON_MS && digitalRead(PIN_ENC_SW) == LOW) {
      calibrarSilencio();
      ultimoBoton = millis();
      botonPresionado = false;  // descarta rebotes ocurridos durante la calibracion
    }
  }
}

// ============================================================
// DEBUG
// ============================================================
void imprimirDebug() {
  uint32_t ahora = millis();
  if (ahora - ultimoPrint < PRINT_INTERVAL_MS) return;
  ultimoPrint = ahora;

  int32_t ua, ur;
  calcularUmbrales(ua, ur);
  Serial.print(F("Amplitud: "));
  Serial.print(ultimaAmplitud);
  Serial.print(F(" | Piso: "));
  Serial.print(baselineNoise);
  Serial.print(F(" | Am: "));
  Serial.print(ua);
  Serial.print(F(" | Rojo: "));
  Serial.print(ur);
  Serial.print(F(" | Sens: "));
  Serial.print(sensibilidad);
  Serial.print(F(" | Nivel: "));
  Serial.println(nombreNivel(nivelActual));
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setColor(COLOR_APAGADO);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVO_EN_ALTO ? LOW : HIGH);  // reposo = callado

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  encoderEstado = (uint8_t)((digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_SW),  isrBoton,   FALLING);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MIC, MIC_ATTEN);

  iniciarLcd();

  calibrarSilencio();
  ultimoBoton = millis();
  botonPresionado = false;

  Serial.println(F(">> Sistema listo."));
}

void loop() {
  manejarEncoder();
  manejarBuzzer();

  if (puedeMedir()) {
    Ventana v = medirVentana();
    ultimaAmplitud = amplitudDe(v);
    cambiarNivel(evaluarNivel(ultimaAmplitud));
  } else {
    delay(1);  // evita girar en vacio mientras suena el buzzer
  }

  refrescarLcd();  // entre ventanas: nunca hay trafico I2C durante el muestreo
  imprimirDebug();
}
