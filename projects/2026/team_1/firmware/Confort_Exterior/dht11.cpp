// dht11.cpp — Lector de pulsos digitales a microsegundos para sensor DHT11
#include "dht11.h"

void DHT11::begin() {
  pinMode(_pin, INPUT_PULLUP);
}

bool DHT11::leer(float& temperatura, float& humedad) {
  // Evitar leer el DHT11 con una frecuencia mayor a 1 lectura por segundo
  uint32_t ahora = millis();
  if (_ultimoExito != 0 && (ahora - _ultimoExito < 1000)) {
    temperatura = _ultimaTemp;
    humedad = _ultimaHum;
    return true;
  }

  uint8_t datos[5] = {0, 0, 0, 0, 0};

  // 1. Pulso de inicio: host baja la línea entre 18 y 20 ms (requerido por DHT11)
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  delay(20);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(30);
  pinMode(_pin, INPUT_PULLUP);

  // 2. Esperar respuesta del sensor (80us LOW, 80us HIGH)
  uint32_t inicio = micros();
  while (digitalRead(_pin) == HIGH) {
    if (micros() - inicio > 100) return false;
  }
  inicio = micros();
  while (digitalRead(_pin) == LOW) {
    if (micros() - inicio > 100) return false;
  }
  inicio = micros();
  while (digitalRead(_pin) == HIGH) {
    if (micros() - inicio > 100) return false;
  }

  // 3. Leer los 40 bits con sección crítica
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);

  for (int i = 0; i < 40; i++) {
    uint32_t t = micros();
    while (digitalRead(_pin) == LOW) {
      if (micros() - t > 90) { portEXIT_CRITICAL(&mux); return false; }
    }
    uint32_t tInicioAlto = micros();
    while (digitalRead(_pin) == HIGH) {
      if (micros() - tInicioAlto > 110) { portEXIT_CRITICAL(&mux); return false; }
    }
    uint32_t duracionAlto = micros() - tInicioAlto;
    if (duracionAlto > 40) {
      datos[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  portEXIT_CRITICAL(&mux);

  // 4. Verificación de checksum
  if (((datos[0] + datos[1] + datos[2] + datos[3]) & 0xFF) != datos[4]) {
    temperatura = _ultimaTemp;
    humedad = _ultimaHum;
    return false;
  }

  // 5. Conversión de magnitudes: el DHT11 estándar envía enteros de 8 bits.
  //    Si el byte decimal es menor a 10 se suma, sino se usa solo el entero (para tolerar clones).
  float h = (float)datos[0];
  if (datos[1] > 0 && datos[1] < 10) h += (float)datos[1] * 0.1f;

  float t = (float)datos[2];
  if (datos[3] > 0 && datos[3] < 10) t += (float)datos[3] * 0.1f;

  if (h < 0.0f || h > 100.0f || t < -10.0f || t > 60.0f) {
    temperatura = _ultimaTemp;
    humedad = _ultimaHum;
    return false;
  }

  _ultimaTemp = t;
  _ultimaHum = h;
  _ultimoExito = ahora;
  temperatura = t;
  humedad = h;
  return true;
}
