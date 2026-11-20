// diagnostico_dht.ino — Prueba aislada del DHT11 (GPIO 14).
//
// Separa cableado de codigo: imprime en que etapa exacta falla el protocolo
// si el sensor responde, y muestra los 40 bits crudos con su checksum.
//
// Uso: flash en el ESP32 del nodo exterior y mirar el Monitor Serie a 115200.

#include <Arduino.h>

constexpr uint8_t PIN_DHT = 14;
constexpr uint32_t PERIODO_MS = 2500;  // el DHT11 exige >= 1 s entre lecturas

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================================");
  Serial.println("# DIAGNOSTICO DHT11  (GPIO 14)");
  Serial.println("=================================================");
  Serial.println("# VCC -> 3V3 | GND -> GND | DATA -> GPIO 14");
  Serial.println("# Pull-up externo 4.7k-10k entre DATA y 3V3: OBLIGATORIO");
  Serial.println();

  pinMode(PIN_DHT, INPUT_PULLUP);
  delay(2000);
  Serial.printf("# Nivel del pin en reposo: %s (esperado HIGH)\n",
                digitalRead(PIN_DHT) ? "HIGH" : "LOW");
  Serial.printf("# Si esta en LOW sin conectar nada, hay un corto a GND.\n\n");
}

void loop() {
  // ---- 1. Pulso de inicio ----
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(18);
  digitalWrite(PIN_DHT, HIGH);
  delayMicroseconds(30);
  pinMode(PIN_DHT, INPUT_PULLUP);

  // ---- 2. Esperar que el sensor baje la linea ----
  uint32_t t0 = micros();
  while (digitalRead(PIN_DHT) == HIGH) {
    if (micros() - t0 > 200) {
      Serial.printf("[%lu] FALLA ETAPA 1: el sensor nunca baja la linea (>200 us).\n", millis());
      Serial.println("        -> No hay respuesta: sensor sin alimentacion, DATA mal conectada");
      Serial.println("           o falta el pull-up externo.");
      delay(PERIODO_MS);
      return;
    }
  }
  uint32_t tEspera = micros() - t0;

  // ---- 3. Medir el LOW de respuesta (80 us esperados) ----
  t0 = micros();
  while (digitalRead(PIN_DHT) == LOW) {
    if (micros() - t0 > 200) {
      Serial.printf("[%lu] FALLA ETAPA 2: la linea quedo en LOW (>200 us).\n", millis());
      delay(PERIODO_MS);
      return;
    }
  }
  uint32_t tLow = micros() - t0;

  // ---- 4. Medir el HIGH de respuesta (80 us esperados) ----
  t0 = micros();
  while (digitalRead(PIN_DHT) == HIGH) {
    if (micros() - t0 > 200) {
      Serial.printf("[%lu] FALLA ETAPA 3: la linea quedo en HIGH (>200 us).\n", millis());
      delay(PERIODO_MS);
      return;
    }
  }
  uint32_t tHigh = micros() - t0;

  Serial.printf("[%lu] HANDSHAKE OK (espera=%lu us, LOW=%lu us, HIGH=%lu us)\n",
                millis(), tEspera, tLow, tHigh);

  // ---- 5. Leer los 40 bits ----
  uint8_t datos[5] = {0, 0, 0, 0, 0};
  bool okBits = true;

  for (int i = 0; i < 40; i++) {
    t0 = micros();
    while (digitalRead(PIN_DHT) == LOW) {
      if (micros() - t0 > 90) { okBits = false; break; }
    }
    uint32_t tAlto = micros();
    while (digitalRead(PIN_DHT) == HIGH) {
      if (micros() - tAlto > 110) { okBits = false; break; }
    }
    if (!okBits) break;
    if (micros() - tAlto > 40) datos[i / 8] |= (1 << (7 - (i % 8)));
  }

  if (!okBits) {
    Serial.println("        -> FALLA EN LOS BITS: handshake OK pero la trama se corrompe.");
    Serial.println("           Tipico de pull-up debil o cable largo. Probar con 4.7k.");
    delay(PERIODO_MS);
    return;
  }

  uint8_t suma = (datos[0] + datos[1] + datos[2] + datos[3]) & 0xFF;

  // DHT11: entero de 8 bits por magnitud; los impares llevan decimales que
  // este modelo no implementa.
  float hum = datos[0];
  float temp = datos[2];

  Serial.printf("        BYTES: %02X %02X %02X %02X | checksum calc=%02X recib=%02X | %s\n",
                datos[0], datos[1], datos[2], datos[3], suma, datos[4],
                (suma == datos[4]) ? "OK" : "MAL");
  Serial.printf("        DHT11: %.0f C | %.0f %% HR\n", temp, hum);
  Serial.println();

  delay(PERIODO_MS);
}
