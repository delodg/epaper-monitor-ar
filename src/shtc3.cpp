#include "shtc3.h"

static const uint16_t CMD_WAKEUP   = 0x3517;
static const uint16_t CMD_SLEEP    = 0xB098;
static const uint16_t CMD_READ_ID  = 0xEFC8;
static const uint16_t CMD_MEASURE  = 0x7866;  // modo normal, sin clock-stretching, T primero

bool SHTC3::cmd(uint16_t c) {
  _w.beginTransmission(_addr);
  _w.write((uint8_t)(c >> 8));
  _w.write((uint8_t)(c & 0xFF));
  return _w.endTransmission() == 0;
}

uint8_t SHTC3::crc8(const uint8_t* d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool SHTC3::begin() {
  if (!cmd(CMD_WAKEUP)) return false;
  delayMicroseconds(300);
  if (!cmd(CMD_READ_ID)) return false;
  delay(1);
  uint8_t b[3];
  if (_w.requestFrom((int)_addr, 3) != 3) return false;
  for (int i = 0; i < 3; i++) b[i] = _w.read();
  uint16_t id = (b[0] << 8) | b[1];
  cmd(CMD_SLEEP);
  return (id & 0x083F) == 0x0807;   // patrón de ID del SHTC3
}

bool SHTC3::read(float& tempC, float& humRH) {
  if (!cmd(CMD_WAKEUP)) return false;
  delayMicroseconds(300);
  if (!cmd(CMD_MEASURE)) return false;
  // Sin clock-stretching el sensor responde NACK hasta terminar (~12 ms): esperar y reintentar.
  uint8_t b[6];
  bool ok = false;
  delay(14);
  for (int tries = 0; tries < 10 && !ok; tries++) {
    if (tries) delay(4);
    if (_w.requestFrom((int)_addr, 6) == 6) {
      for (int i = 0; i < 6; i++) b[i] = _w.read();
      ok = true;
    }
  }
  cmd(CMD_SLEEP);
  if (!ok) return false;
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;
  uint16_t rawT = (b[0] << 8) | b[1];
  uint16_t rawH = (b[3] << 8) | b[4];
  tempC = -45.0f + 175.0f * rawT / 65535.0f;
  humRH = 100.0f * rawH / 65535.0f;
  return true;
}
