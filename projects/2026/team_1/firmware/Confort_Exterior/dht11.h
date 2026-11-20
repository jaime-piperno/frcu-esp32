// dht11.h — Controlador de sensor de temperatura y humedad DHT11 para ESP32
#pragma once
#include <Arduino.h>

class DHT11 {
 public:
  explicit DHT11(uint8_t pin) : _pin(pin), _ultimaTemp(20.0f), _ultimaHum(50.0f), _ultimoExito(0) {}
  void begin();
  bool leer(float& temperatura, float& humedad);

 private:
  uint8_t _pin;
  float _ultimaTemp;
  float _ultimaHum;
  uint32_t _ultimoExito;
};
