#include <Arduino.h>

// --- Configuración de Pines ---
const int PIN_BOTON  = 4;   // Módulo de 3 patas (GPIO 4)
const int PIN_LED    = 2;   // LED (GPIO 2)
const int PIN_BUZZER = 18;  // Buzzer (GPIO 18)

const int FRECUENCIA_TONO = 1000; 

// --- Tiempos y Umbrales ---
const unsigned long TIEMPO_DEBOUNCE          = 50;   // 50 ms para filtrar rebotes mecánicos
const unsigned long TIEMPO_INACTIVIDAD_FINAL = 5000; // 5 segundos para cerrar el mensaje

unsigned long duracionUnidadBase = 0; // Se fija con la primera pulsación
bool velocidadCalibrada = false;

unsigned long tiempoInicioPulsacion = 0;
unsigned long tiempoSoltado          = 0;
unsigned long ultimoTiempoCambio     = 0; // Para el filtro antirrebote

bool estabaPresionado     = false;
bool esperandoTraduccion   = false;
bool mensajeFinalMostrado  = true;

String patronMorseActual = "";
String palabraCompleta   = ""; 

// --- Tabla de Traducción Morse ---
struct CodigoMorse {
  char letra;
  const char* patron;
};

const CodigoMorse TABLA_MORSE[] = {
  {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
  {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
  {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
  {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
  {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
  {'Z', "--.."},
  {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
  {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'0', "-----"}
};

const int TAMANO_TABLA = sizeof(TABLA_MORSE) / sizeof(TABLA_MORSE[0]);

char traducirMorse(String morse) {
  for (int i = 0; i < TAMANO_TABLA; i++) {
    if (morse == TABLA_MORSE[i].patron) {
      return TABLA_MORSE[i].letra;
    }
  }
  return '?'; 
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_BOTON, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED, LOW);
  noTone(PIN_BUZZER);

  Serial.println("\n=======================================================");
  Serial.println("  TRADUCTOR MORSE CON FILTRO ANTIRREBOTE (DEBOUNCE)");
  Serial.println("  1. Primera pulsación fija la velocidad.");
  Serial.println("  2. Antirrebote activo (evita dobles puntos accidentales).");
  Serial.println("  3. 5 segundos de silencio para mostrar el mensaje final.");
  Serial.println("=======================================================\n");
}

void loop() {
  int lectura = digitalRead(PIN_BOTON);
  bool estaPresionadoAhora = (lectura == HIGH); // Si tu módulo actúa invertido, cambia a LOW
  unsigned long tiempoActual = millis();

  // --- FILTRO ANTIRREBOTE Y DETECCIÓN DE CAMBIOS ---
  if ((tiempoActual - ultimoTiempoCambio) > TIEMPO_DEBOUNCE) {

    // 1. MOMENTO DE PRESIONAR EL BOTÓN (Transición a PRESIONADO)
    if (estaPresionadoAhora && !estabaPresionado) {
      tiempoInicioPulsacion = tiempoActual;
      ultimoTiempoCambio    = tiempoActual;
      estabaPresionado      = true;
      esperandoTraduccion   = true;
      mensajeFinalMostrado  = false;

      digitalWrite(PIN_LED, HIGH);       
      tone(PIN_BUZZER, FRECUENCIA_TONO);  
    }

    // 2. MOMENTO DE SOLTAR EL BOTÓN (Transición a SOLTADO)
    if (!estaPresionadoAhora && estabaPresionado) {
      unsigned long duracionPulsacion = tiempoActual - tiempoInicioPulsacion;
      ultimoTiempoCambio = tiempoActual;
      estabaPresionado   = false;

      digitalWrite(PIN_LED, LOW);        
      noTone(PIN_BUZZER);                

      // Descartar pulsaciones fantasma demasiado cortas (< 60 ms)
      if (duracionPulsacion >= 60) {

        // PRIMERA PULSACIÓN: Calibrar la velocidad base
        if (!velocidadCalibrada) {
          duracionUnidadBase = duracionPulsacion;
          velocidadCalibrada = true;
          
          patronMorseActual += ".";
          Serial.print("\n[Ritmo base fijado: ");
          Serial.print(duracionUnidadBase);
          Serial.println(" ms]");
          Serial.print("Pulso: .");
        } 
        // SIGUIENTES PULSACIONES: Evaluar si es Punto o Raya
        else {
          if (duracionPulsacion < (duracionUnidadBase * 1.8)) {
            patronMorseActual += ".";
            Serial.print(".");
          } else {
            patronMorseActual += "-";
            Serial.print("-");
          }
        }
      }

      tiempoSoltado = tiempoActual;
    }
  }

  // --- TRADUCCIÓN DE LETRA (Pausa corta) ---
  if (esperandoTraduccion && !estaPresionadoAhora && velocidadCalibrada) {
    unsigned long tiempoPausa = tiempoActual - tiempoSoltado;
    unsigned long limiteFinLetra = max((unsigned long)800, duracionUnidadBase * 3);

    if (tiempoPausa >= limiteFinLetra) {
      if (patronMorseActual.length() > 0) {
        char letra = traducirMorse(patronMorseActual);
        palabraCompleta += letra;

        Serial.print("  ==> Letra: '");
        Serial.print(letra);
        Serial.print("' | TEXTO ACTUAL: \"");
        Serial.print(palabraCompleta);
        Serial.println("\"");

        patronMorseActual = ""; 
      }
      esperandoTraduccion = false;
    }
  }

  // --- ESPACIO ENTRE PALABRAS (Pausa media) ---
  if (!esperandoTraduccion && !estaPresionadoAhora && velocidadCalibrada && palabraCompleta.length() > 0) {
    unsigned long tiempoPausa = tiempoActual - tiempoSoltado;
    unsigned long limiteEspacio = max((unsigned long)2000, duracionUnidadBase * 7);

    if (tiempoPausa >= limiteEspacio && !palabraCompleta.endsWith(" ") && tiempoPausa < TIEMPO_INACTIVIDAD_FINAL) {
      palabraCompleta += " ";
      Serial.print("\n[ESPACIO] | TEXTO ACTUAL: \"");
      Serial.print(palabraCompleta);
      Serial.println("\"");
    }
  }

  // --- MOSTRAR MENSAJE FINAL POR INACTIVIDAD (Pausa de 5 segundos) ---
  if (!estaPresionadoAhora && !mensajeFinalMostrado && palabraCompleta.length() > 0) {
    unsigned long tiempoInactivo = tiempoActual - tiempoSoltado;

    if (tiempoInactivo >= TIEMPO_INACTIVIDAD_FINAL) {
      palabraCompleta.trim(); 

      Serial.println("\n");
      Serial.println("*******************************************************");
      Serial.print("  MENSAJE FINAL TRADUCIDO: ");
      Serial.println(palabraCompleta);
      Serial.println("*******************************************************");
      Serial.println("  (Listo para un nuevo mensaje. La primera pulsación");
      Serial.println("   fijará la velocidad del nuevo intento)\n");

      palabraCompleta      = "";
      velocidadCalibrada   = false;
      mensajeFinalMostrado = true;
    }
  }
}