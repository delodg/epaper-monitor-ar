#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <time.h>

// Driver mínimo para el RTC NXP PCF85063A (I2C 0x51).
// Guarda hora LOCAL (Argentina no tiene horario de verano, así que es seguro).
class PCF85063 {
 public:
  explicit PCF85063(TwoWire& wire = Wire, uint8_t addr = 0x51) : _w(wire), _addr(addr) {}

  bool begin();                       // true si el chip responde
  bool read(struct tm& t);            // false si no responde o el oscilador se detuvo (hora inválida)
  bool write(const struct tm& t);     // escribe fecha/hora y limpia el flag OS
  bool present() const { return _present; }

 private:
  TwoWire& _w;
  uint8_t  _addr;
  bool     _present = false;

  static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
  static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n);
  bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n);
};
