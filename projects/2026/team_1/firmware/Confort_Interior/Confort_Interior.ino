// Confort_Interior.ino — Monitoreo de Confort Ambiental: Nodo Interior (Servidor Web)
// Sensores: Bosch BME680 (I2C) + Fotorresistor LDR (ADC1) + Micrófono KY-037 (Digital DO)
#include <Wire.h>
#include "config.h"
#include "control.h"
#include "sensor.h"
#include "storage.h"
#include "web.h"

static EstadoControl control;

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  storageIniciar();
  sensorIniciar();
  webIniciar();

  Serial.println(F("\n========================================================"));
  Serial.println(F("# SISTEMA DE CONFORT AMBIENTAL — NODO INTERIOR INICIADO"));
  Serial.printf("# Buffers RAM: %u bytes. Heap libre: %u bytes\n",
                (unsigned)(sizeof historial + sizeof vivo), ESP.getFreeHeap());
  Serial.println(F("========================================================"));
  Serial.print(CSV_CABECERA);
}

void loop() {
  Lectura interior;
  if (sensorActualizar(interior)) {
    Lectura exterior;
    webObtenerRemoto(exterior);

    // 1) Calcular deltas de confort entre Interior y Exterior
    calcularDeltasConfort(control, interior, exterior);

    // 2) Historial y registro CSV en monitor serial
    storageAgregar(interior, uptimeS());
    char fila[160];
    formatearFila(fila, sizeof fila, empaquetar(interior, uptimeS()), epochArranque);
    Serial.print(fila);

    // 3) Publicación a través de WebSocket a clientes web
    webPublicar(interior, exterior, control);
  }

  webActualizar();
}

