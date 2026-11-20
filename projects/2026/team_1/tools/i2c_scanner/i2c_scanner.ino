// tools/i2c_scanner/i2c_scanner.ino
// Escáner I2C: lista las direcciones que responden en SDA=21 / SCL=22.
// El BME680 debe aparecer en 0x76 (SDO a GND) o 0x77 (SDO a 3V3).
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
}

void loop() {
  Serial.println("Escaneando bus I2C...");
  int encontrados = 0;
  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Dispositivo en 0x%02X%s\n", dir,
                    (dir == 0x76 || dir == 0x77) ? "  <- probablemente el BME680" : "");
      encontrados++;
    }
  }
  if (encontrados == 0) Serial.println("  Nada. Revisá cableado, VCC=3V3 y que CS esté a 3V3.");
  delay(3000);  // es un sketch de diagnóstico aparte: acá delay() no molesta
}
