/*
  ============================================================
  TEST DE ATENUACIONES DEL ADC - diagnostico del KY-037
  ============================================================
  Script auxiliar del proyecto "Semaforo de ruido".

  Para que sirve
  ------------------------------------------------------------
  El KY-037 entrega una senal de audio montada sobre un offset de
  continua que depende de la posicion de su potenciometro. Ese offset
  tiene que caer dentro del rango que mide el ADC: si queda por encima,
  todas las lecturas se clavan en 4095 y no se detecta nada.

  El ADC del ESP32 admite cuatro atenuaciones, cada una con un fondo de
  escala distinto. Este script mide 500 ms con cada una e informa el
  offset de continua, el minimo, el maximo y la amplitud pico a pico,
  marcando las que saturan. Con eso se elige la atenuacion adecuada y
  se ajusta el potenciometro del modulo.

  Como usarlo
  ------------------------------------------------------------
  1. Cargar y abrir el Monitor Serie a 115200 baudios.
  2. En silencio, girar el potenciometro del KY-037 mientras se observa
     la linea de la atenuacion que se quiere usar. El objetivo es un DC
     centrado, sin que el minimo llegue a 0 ni el maximo a 4095.
     Con ADC_11db el valor de referencia del proyecto es un DC de ~520.
  3. Hablar o aplaudir para verificar que la amplitud pico a pico sube
     de forma clara respecto del silencio.

  Atencion: las cuatro atenuaciones se miden una despues de la otra, no
  al mismo tiempo. Un ruido corto cae solamente en la linea que se estaba
  midiendo en ese instante, asi que para comparar hay que mantener la
  misma condicion durante toda la vuelta.

  Conexion: A0 del KY-037 a GPIO34. VCC del modulo a 3V3, nunca a 5 V.
  ============================================================
*/

#include <Arduino.h>

const uint8_t  PIN_MIC    = 34;   // mismo pin que usa el proyecto
const uint32_t VENTANA_MS = 500;  // duracion de la medicion de cada atenuacion
const uint32_t PAUSA_MS   = 1500; // espera entre vueltas completas
const uint16_t ADC_MAX    = 4095;

struct Lectura {
  uint16_t minimo;
  uint16_t maximo;
  uint16_t dc;
};

// Recorre la senal durante VENTANA_MS y se queda con los extremos y el promedio.
// El promedio es el offset de continua; la diferencia entre extremos, la amplitud.
Lectura medir() {
  uint16_t minimo = ADC_MAX;
  uint16_t maximo = 0;
  uint64_t suma   = 0;
  uint32_t n      = 0;

  uint32_t inicio = millis();
  while (millis() - inicio < VENTANA_MS) {
    uint16_t m = analogRead(PIN_MIC);
    if (m < minimo) minimo = m;
    if (m > maximo) maximo = m;
    suma += m;
    n++;
  }

  Lectura l;
  l.minimo = minimo;
  l.maximo = maximo;
  l.dc     = n ? (uint16_t)(suma / n) : 0;
  return l;
}

void probar(const char *nombre, adc_attenuation_t atten) {
  analogSetAttenuation(atten);              // global, para pines aun sin inicializar
  analogSetPinAttenuation(PIN_MIC, atten);  // especifica del pin
  analogRead(PIN_MIC);                      // descarta la primera lectura
  delay(50);                                // deja asentar el cambio de atenuacion

  Lectura l = medir();

  Serial.print(nombre);
  Serial.print(F("\tDC: "));
  Serial.print(l.dc);
  Serial.print(F("\tmin: "));
  Serial.print(l.minimo);
  Serial.print(F("\tmax: "));
  Serial.print(l.maximo);
  Serial.print(F("\tpico a pico: "));
  Serial.print(l.maximo - l.minimo);
  if (l.maximo >= ADC_MAX || l.minimo == 0) Serial.print(F("\t<-- SATURA"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  analogReadResolution(12);

  Serial.println();
  Serial.println(F("TEST DE ATENUACIONES DEL ADC (GPIO34)"));
  Serial.println(F("Cada vuelta mide 500 ms con cada atenuacion, una despues de la otra."));
  Serial.println(F("Objetivo: DC centrado en el rango, sin min=0 ni max=4095."));
  Serial.println();
}

void loop() {
  Serial.println(F("---- vuelta ----"));
  probar("0db  (~950mV) ", ADC_0db);
  probar("2.5db(~1.25V) ", ADC_2_5db);
  probar("6db  (~1.75V) ", ADC_6db);
  probar("11db (~2.45V) ", ADC_11db);
  Serial.println();
  delay(PAUSA_MS);
}
