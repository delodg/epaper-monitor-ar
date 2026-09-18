#include "rtc_pcf85063.h"

// Registros del PCF85063A
static const uint8_t REG_CTRL1   = 0x00;
static const uint8_t REG_SECONDS = 0x04;  // 0x04..0x0A: sec, min, hour, day, weekday, month, year

bool PCF85063::readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  _w.beginTransmission(_addr);
  _w.write(reg);
  if (_w.endTransmission(false) != 0) return false;
  if (_w.requestFrom((int)_addr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = _w.read();
  return true;
}

bool PCF85063::writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) {
  _w.beginTransmission(_addr);
  _w.write(reg);
  _w.write(buf, n);
  return _w.endTransmission() == 0;
}

bool PCF85063::begin() {
  uint8_t ctrl;
  _present = readRegs(REG_CTRL1, &ctrl, 1);
  if (!_present) return false;
  // Modo 24 h, oscilador en marcha, CAP_SEL por defecto (7 pF).
  if (ctrl & 0x22) {              // STOP (bit5) o 12_24 (bit1) activos -> normalizar
    ctrl &= ~0x22;
    writeRegs(REG_CTRL1, &ctrl, 1);
  }
  return true;
}

bool PCF85063::read(struct tm& t) {
  uint8_t b[7];
  if (!readRegs(REG_SECONDS, b, 7)) return false;
  if (b[0] & 0x80) return false;   // OS flag: el oscilador se detuvo, hora no confiable
  memset(&t, 0, sizeof(t));
  t.tm_sec  = bcd2bin(b[0] & 0x7F);
  t.tm_min  = bcd2bin(b[1] & 0x7F);
  t.tm_hour = bcd2bin(b[2] & 0x3F);
  t.tm_mday = bcd2bin(b[3] & 0x3F);
  t.tm_wday = b[4] & 0x07;
  t.tm_mon  = bcd2bin(b[5] & 0x1F) - 1;
  t.tm_year = 100 + bcd2bin(b[6]);          // años desde 1900 (RTC guarda 2000+YY)
  if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_hour > 23 || t.tm_min > 59) return false;
  return true;
}

bool PCF85063::write(const struct tm& t) {
  uint8_t b[7];
  b[0] = bin2bcd(t.tm_sec) & 0x7F;          // bit7 = 0 limpia el flag OS
  b[1] = bin2bcd(t.tm_min);
  b[2] = bin2bcd(t.tm_hour);
  b[3] = bin2bcd(t.tm_mday);
  b[4] = t.tm_wday & 0x07;
  b[5] = bin2bcd(t.tm_mon + 1);
  b[6] = bin2bcd((t.tm_year + 1900) % 100);
  return writeRegs(REG_SECONDS, b, 7);
}
