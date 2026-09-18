#pragma once
#include <Arduino.h>
#include <Wire.h>

// Driver mínimo para el sensor Sensirion SHTC3 (temperatura + humedad, I2C 0x70).
class SHTC3 {
 public:
  explicit SHTC3(TwoWire& wire = Wire, uint8_t addr = 0x70) : _w(wire), _addr(addr) {}
  bool begin();                              // wake + verificación de ID
  bool read(float& tempC, float& humRH);     // medición en modo normal, deja el sensor en sleep
 private:
  TwoWire& _w;
  uint8_t  _addr;
  bool cmd(uint16_t c);
  static uint8_t crc8(const uint8_t* d, int n);
};
