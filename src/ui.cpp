#include "ui.h"
#include "config.h"
#include "appdata.h"
#include "board.h"
#include "logo_delo.h"

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <qrcode.h>
#include <base64.h>
#include <WiFi.h>
#include <math.h>

#ifndef EPD_ROTATION
#define EPD_ROTATION 0
#endif

namespace UI {

// ============================================================================
//  Panel y tipografías
// ============================================================================
static const int W = 200, H = 200;

// GxEPD2_BW con un "buffer sombra" (1 bit/píxel, 1 = blanco) para poder volcar por
// serie lo que se dibujó y verlo en la PC como imagen (comando 'a' / 'd' por USB).
class ShadowDisplay : public GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> {
 public:
  explicit ShadowDisplay(GxEPD2_154_D67 epd) : GxEPD2_BW(epd) { memset(shadow, 0xFF, sizeof(shadow)); }
  uint8_t shadow[W * H / 8];
  bool dark = false;                 // modo oscuro: se invierte cada píxel al dibujar
  inline uint16_t map(uint16_t c) const { return dark ? (c == GxEPD_WHITE ? GxEPD_BLACK : GxEPD_WHITE) : c; }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    color = map(color);
    GxEPD2_BW::drawPixel(x, y, color);
    switch (getRotation()) {           // misma transformación que GxEPD2_BW
      case 1: { int16_t t = x; x = W - y - 1; y = t; break; }
      case 2: x = W - x - 1; y = H - y - 1; break;
      case 3: { int16_t t = x; x = y; y = H - t - 1; break; }
      default: break;
    }
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint16_t i = x / 8 + y * (W / 8);
    if (color == GxEPD_WHITE) shadow[i] |= (1 << (7 - x % 8));
    else shadow[i] &= ~(1 << (7 - x % 8));
  }
  void fillScreen(uint16_t color) override {
    color = map(color);
    GxEPD2_BW::fillScreen(color);
    memset(shadow, color == GxEPD_WHITE ? 0xFF : 0x00, sizeof(shadow));
  }
};

static ShadowDisplay display(GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

#define F_CLOCK  u8g2_font_logisoso50_tn
#define F_BIG    u8g2_font_helvB24_tf
#define F_B14    u8g2_font_helvB14_tf
#define F_B12    u8g2_font_helvB12_tf
#define F_B10    u8g2_font_helvB10_tf
#define F_R10    u8g2_font_helvR10_tf
#define F_B08    u8g2_font_helvB08_tf
#define F_R08    u8g2_font_helvR08_tf
#define F_TINY   u8g2_font_5x8_tf
#define F_R24    u8g2_font_helvR24_tf

static const int LINE_H8 = 11;   // interlineado para fuentes de 8 px

// ---- helpers de texto ----
static void font(const uint8_t* f) { u8g2.setFont(f); }
static int  tw(const char* s) { return u8g2.getUTF8Width(s); }
static void text(int x, int y, const char* s) { u8g2.setCursor(x, y); u8g2.print(s); }
static void textRight(int xr, int y, const char* s) { text(xr - tw(s), y, s); }
static void textCenter(int cx, int y, const char* s) { text(cx - tw(s) / 2, y, s); }

// Texto con letras espaciadas (overlines estilo "tracking"); respeta UTF-8.
static int textSpaced(int x, int y, const char* s, int spacing) {
  const char* p = s;
  while (*p) {
    int len = 1;
    if (((uint8_t)*p & 0xE0) == 0xC0) len = 2;
    else if (((uint8_t)*p & 0xF0) == 0xE0) len = 3;
    else if (((uint8_t)*p & 0xF8) == 0xF0) len = 4;
    char ch[5] = {0};
    memcpy(ch, p, len);
    text(x, y, ch);
    x += tw(ch) + spacing;
    p += len;
  }
  return x;
}

// Acorta con "..." hasta que entre en maxW (respetando UTF-8). Las fuentes Helvetica de
// U8g2 son Latin-1: no tienen el glifo "…", por eso tres puntos.
static String fit(const char* s, int maxW) {
  String t = s;
  if (tw(t.c_str()) <= maxW) return t;
  while (t.length() > 0) {
    int n = t.length() - 1;
    while (n > 0 && ((uint8_t)t[n] & 0xC0) == 0x80) n--;
    t.remove(n);
    String probe = t + "...";
    if (tw(probe.c_str()) <= maxW) return probe;
  }
  return t;
}

// Word-wrap: devuelve la cantidad de líneas escritas en lines[] (máx maxLines).
static int wrap(const char* s, int maxW, String* lines, int maxLines) {
  int n = 0;
  String cur;
  String word;
  const char* p = s;
  auto flushWord = [&]() {
    if (word.length() == 0) return;
    String probe = cur.length() ? cur + " " + word : word;
    if (tw(probe.c_str()) <= maxW) {
      cur = probe;
    } else {
      if (cur.length() && n < maxLines) lines[n++] = cur;
      cur = word;
      // palabra más larga que el ancho: cortar a la fuerza
      while (tw(cur.c_str()) > maxW && n < maxLines) {
        int k = cur.length() - 1;
        while (k > 1 && tw(cur.substring(0, k).c_str()) > maxW) {
          k--;
          while (k > 1 && ((uint8_t)cur[k] & 0xC0) == 0x80) k--;
        }
        lines[n++] = cur.substring(0, k);
        cur = cur.substring(k);
      }
    }
    word = "";
  };
  while (*p) {
    if (*p == ' ') { flushWord(); }
    else word += *p;
    p++;
  }
  flushWord();
  if (cur.length() && n < maxLines) lines[n++] = cur;
  else if (cur.length() && n == maxLines && n > 0) lines[n - 1] = fit((lines[n - 1] + " " + cur).c_str(), maxW);
  return n;
}

static void hline(int y, int x0 = 0, int x1 = W) { display.drawFastHLine(x0, y, x1 - x0, GxEPD_BLACK); }

static String money(float v) {
  long n = lroundf(v);
  char buf[16];
  if (n >= 1000) snprintf(buf, sizeof(buf), "$%ld.%03ld", n / 1000, n % 1000);
  else snprintf(buf, sizeof(buf), "$%ld", n);
  return String(buf);
}

// ============================================================================
//  Íconos (dibujados con primitivas, 1 bit)
// ============================================================================
static void drawBattery(int x, int y, int mv) {
  // 20x9: cuerpo 17x9 + borne
  display.drawRect(x, y, 17, 9, GxEPD_BLACK);
  display.fillRect(x + 17, y + 2, 2, 5, GxEPD_BLACK);
  if (Board::onUsbPower(mv)) {
    // rayo (carga / USB)
    display.fillTriangle(x + 9, y + 1, x + 5, y + 5, x + 9, y + 5, GxEPD_BLACK);
    display.fillTriangle(x + 8, y + 4, x + 12, y + 4, x + 8, y + 8, GxEPD_BLACK);
    return;
  }
  int pct = Board::batteryPercent(mv);
  int w = (13 * pct) / 100;
  if (w > 0) display.fillRect(x + 2, y + 2, w, 5, GxEPD_BLACK);
}

static void drawSignal(int x, int y, bool ok, int rssi) {
  // 4 barras de 3px, base en y+9
  int bars = ok ? (rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1) : 0;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2;
    int bx = x + i * 4, by = y + 9 - h;
    if (i < bars) display.fillRect(bx, by, 3, h, GxEPD_BLACK);
    else display.drawRect(bx, by, 3, h, GxEPD_BLACK);
  }
  if (!ok) {  // tachado
    display.drawLine(x, y + 9, x + 15, y, GxEPD_BLACK);
    display.drawLine(x + 1, y + 9, x + 16, y, GxEPD_BLACK);
  }
}

static void drawThermo(int x, int y) {
  // 8x14
  display.drawRoundRect(x + 2, y, 4, 9, 2, GxEPD_BLACK);
  display.fillCircle(x + 4, y + 11, 3, GxEPD_BLACK);
  display.fillRect(x + 3, y + 4, 2, 6, GxEPD_BLACK);
}

static void drawDrop(int x, int y) {
  // 9x13
  display.fillTriangle(x + 4, y, x, y + 8, x + 8, y + 8, GxEPD_BLACK);
  display.fillCircle(x + 4, y + 8, 4, GxEPD_BLACK);
}

enum WIcon { ICO_SUN, ICO_MOON, ICO_SUNCLOUD, ICO_MOONCLOUD, ICO_CLOUD, ICO_FOG, ICO_DRIZZLE, ICO_RAIN, ICO_SNOW, ICO_STORM };

static const char* weatherText(int code) {
  switch (code) {
    case 0:  return "Despejado";
    case 1:  return "Mayormente despejado";
    case 2:  return "Parcialmente nublado";
    case 3:  return "Nublado";
    case 45: case 48: return "Niebla";
    case 51: case 53: case 55: return "Llovizna";
    case 56: case 57: return "Llovizna helada";
    case 61: return "Lluvia leve";
    case 63: return "Lluvia";
    case 65: return "Lluvia intensa";
    case 66: case 67: return "Lluvia helada";
    case 71: case 73: case 75: case 77: return "Nieve";
    case 80: return "Chubascos leves";
    case 81: return "Chubascos";
    case 82: return "Chubascos fuertes";
    case 85: case 86: return "Chubascos de nieve";
    case 95: return "Tormenta";
    case 96: case 99: return "Tormenta con granizo";
    default: return "—";
  }
}

static WIcon weatherIcon(int code, bool isDay) {
  if (code == 0) return isDay ? ICO_SUN : ICO_MOON;
  if (code == 1 || code == 2) return isDay ? ICO_SUNCLOUD : ICO_MOONCLOUD;
  if (code == 3) return ICO_CLOUD;
  if (code == 45 || code == 48) return ICO_FOG;
  if (code >= 51 && code <= 57) return ICO_DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return ICO_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICO_SNOW;
  if (code >= 95) return ICO_STORM;
  return ICO_CLOUD;
}

// Nube con contorno; (x,y) esquina, k = escala (1.0 = caja de 40 px), dy desplazamiento vertical
static void cloud(int x, int y, float k, int dy = 0) {
  auto X = [&](float v) { return (int)lroundf(x + v * k); };
  auto Y = [&](float v) { return (int)lroundf(y + (v + dy) * k); };
  int r1 = lroundf(7 * k), r2 = lroundf(10 * k), r3 = lroundf(6 * k);
  int in = (k >= 0.8f) ? 2 : 1;
  display.fillCircle(X(13), Y(26), r1, GxEPD_BLACK);
  display.fillCircle(X(23), Y(19), r2, GxEPD_BLACK);
  display.fillCircle(X(31), Y(26), r3, GxEPD_BLACK);
  display.fillRect(X(13), Y(24), X(31) - X(13), Y(32) - Y(24) + 1, GxEPD_BLACK);
  display.fillCircle(X(13), Y(26), r1 - in, GxEPD_WHITE);
  display.fillCircle(X(23), Y(19), r2 - in, GxEPD_WHITE);
  display.fillCircle(X(31), Y(26), r3 - in, GxEPD_WHITE);
  display.fillRect(X(13), Y(24), X(31) - X(13), Y(32) - Y(24) + 1 - in, GxEPD_WHITE);
}

static void sun(int cx, int cy, int r, int rayIn, int rayOut) {
  display.fillCircle(cx, cy, r, GxEPD_BLACK);
  display.fillCircle(cx, cy, r - 2, GxEPD_WHITE);
  for (int a = 0; a < 8; a++) {
    float ang = a * (float)M_PI / 4.0f;
    float c = cosf(ang), s = sinf(ang);
    display.drawLine(cx + c * rayIn, cy + s * rayIn, cx + c * rayOut, cy + s * rayOut, GxEPD_BLACK);
    display.drawLine(cx + c * rayIn + (s > 0.1f ? 1 : 0), cy + s * rayIn, cx + c * rayOut + (s > 0.1f ? 1 : 0), cy + s * rayOut, GxEPD_BLACK);
  }
}

static void moon(int cx, int cy, int r) {
  display.fillCircle(cx, cy, r, GxEPD_BLACK);
  display.fillCircle(cx + r / 2, cy - r / 3, r - 1, GxEPD_WHITE);
}

static void drawWeatherIcon(int x, int y, int size, WIcon ic) {
  float k = size / 40.0f;
  auto X = [&](float v) { return (int)lroundf(x + v * k); };
  auto Y = [&](float v) { return (int)lroundf(y + v * k); };
  auto S = [&](float v) { return (int)lroundf(v * k); };
  switch (ic) {
    case ICO_SUN:       sun(X(20), Y(20), S(9), S(12), S(17)); break;
    case ICO_MOON:      moon(X(20), Y(20), S(10)); break;
    case ICO_SUNCLOUD:  sun(X(27), Y(13), S(6), S(8), S(12)); cloud(x, y, k, 2); break;
    case ICO_MOONCLOUD: moon(X(27), Y(13), S(7)); cloud(x, y, k, 2); break;
    case ICO_CLOUD:     cloud(x, y, k, 2); break;
    case ICO_FOG:
      for (int i = 0; i < 4; i++) {
        int yy = Y(14 + i * 6);
        display.fillRect(X(6 + (i % 2) * 4), yy, S(26), max(1, S(2)), GxEPD_BLACK);
      }
      break;
    case ICO_DRIZZLE:
      cloud(x, y, k, -5);
      // gotitas: cuadraditos de 2x2 (fillCircle r=1 dibuja una cruz, se confunde con nieve)
      for (int i = 0; i < 3; i++) display.fillRect(X(12 + i * 8), Y(33), 2, 2, GxEPD_BLACK);
      break;
    case ICO_RAIN:
      cloud(x, y, k, -5);
      // rayas diagonales de 2 px de ancho y >= 7 px de alto
      for (int i = 0; i < 3; i++) {
        int x1 = X(15 + i * 8), y1 = Y(31), y2 = y1 + max(7, S(8));
        display.drawLine(x1, y1, x1 - 3, y2, GxEPD_BLACK);
        display.drawLine(x1 + 1, y1, x1 - 2, y2, GxEPD_BLACK);
      }
      break;
    case ICO_SNOW:
      cloud(x, y, k, -5);
      // copos: asteriscos de 6 brazos
      for (int i = 0; i < 3; i++) {
        int cx = X(12 + i * 8), cy = Y(35), r = max(3, S(4));
        display.drawLine(cx - r, cy, cx + r, cy, GxEPD_BLACK);
        display.drawLine(cx, cy - r, cx, cy + r, GxEPD_BLACK);
        display.drawLine(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, GxEPD_BLACK);
        display.drawLine(cx - r + 1, cy + r - 1, cx + r - 1, cy - r + 1, GxEPD_BLACK);
      }
      break;
    case ICO_STORM:
      cloud(x, y, k, -4);
      display.fillTriangle(X(23), Y(27), X(16), Y(36), X(23), Y(36), GxEPD_BLACK);
      display.fillTriangle(X(23), Y(33), X(28), Y(31), X(18), Y(42), GxEPD_BLACK);
      break;
  }
}

// ============================================================================
//  Bloques comunes
// ============================================================================
static void clockStr(char* out, size_t n) {
  if (g_state.timeValid) snprintf(out, n, "%02d:%02d", g_now.tm_hour, g_now.tm_min);
  else snprintf(out, n, "--:--");
}

static void drawHeader(const char* title) {
  font(F_B12);
  text(2, 13, fit(title, 120).c_str());
  char hm[8]; clockStr(hm, sizeof(hm));
  font(F_B08);
  textRight(150, 12, hm);
  drawSignal(156, 3, g_state.wifiOk, g_state.rssi);
  drawBattery(178, 3, g_batteryMv);
  hline(17);
}

static void drawNav(uint8_t page) {
  font(F_TINY);
  text(2, 199, "< PWR");
  textRight(W - 2, 199, "BOOT >");
  int x0 = W / 2 - (PAGE_COUNT * 7) / 2 + 3;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (i == page) display.fillCircle(x0 + i * 7, 195, 2, GxEPD_BLACK);
    else display.drawPixel(x0 + i * 7, 195, GxEPD_BLACK);
  }
}

// ============================================================================
//  Secciones
// ============================================================================
static void pageClock() {
  // Cabecera: fecha + íconos
  char buf[48];
  font(F_B08);
  if (g_state.timeValid)
    snprintf(buf, sizeof(buf), "%s %d %s %d", dayShort(g_now.tm_wday), g_now.tm_mday, monthShort(g_now.tm_mon), g_now.tm_year + 1900);
  else snprintf(buf, sizeof(buf), "Sin hora (sin sync)");
  text(2, 12, buf);
  drawSignal(156, 3, g_state.wifiOk, g_state.rssi);
  drawBattery(178, 3, g_batteryMv);
  hline(16);

  // Reloj grande
  char hm[8]; clockStr(hm, sizeof(hm));
  font(F_CLOCK);
  textCenter(W / 2, 72, hm);

  // Clima exterior
  if (g_weather.valid) {
    drawWeatherIcon(4, 80, 44, weatherIcon(g_weather.code, g_weather.isDay));
    font(F_BIG);
    snprintf(buf, sizeof(buf), "%.0f°", g_weather.temp);
    text(54, 108, buf);
    int xr = 54 + tw(buf) + 8;
    font(F_R08);
    snprintf(buf, sizeof(buf), "ST %.0f°", g_weather.feels); text(xr, 92, buf);
    snprintf(buf, sizeof(buf), "Hum %.0f%%", g_weather.hum); text(xr, 104, buf);
    snprintf(buf, sizeof(buf), "%s %.0f km/h", windDirText(g_weather.windDir), g_weather.wind); text(xr, 116, buf);
    font(F_R08);
    text(54, 127, fit(weatherText(g_weather.code), 144).c_str());
  } else {
    font(F_R08);
    text(54, 100, "Clima: sin datos");
    text(54, 112, "(pendiente de sync)");
    drawWeatherIcon(4, 80, 44, ICO_CLOUD);
  }
  hline(131);

  // Interior (SHTC3)
  if (g_indoor.valid) {
    drawThermo(6, 135);
    font(F_B10);
    snprintf(buf, sizeof(buf), "%.1f°C", g_indoor.temp); text(20, 148, buf);
    drawDrop(96, 136);
    snprintf(buf, sizeof(buf), "%.0f%%", g_indoor.hum); text(110, 148, buf);
    font(F_R08);
    textRight(W - 3, 148, "interior");
  } else {
    font(F_R08);
    text(6, 148, "Sensor interior: sin lectura");
  }
  hline(153);

  // Pie: dólar + próximo feriado
  font(F_R08);
  if (g_dolar.valid && g_dolar.n >= 2)
    snprintf(buf, sizeof(buf), "Blue %s · Oficial %s", money(g_dolar.item[1].venta).c_str(), money(g_dolar.item[0].venta).c_str());
  else snprintf(buf, sizeof(buf), "Dólar: sin datos");
  text(3, 167, fit(buf, W - 6).c_str());
  if (g_holidays.valid && g_holidays.n > 0) {
    const Holiday& h = g_holidays.item[0];
    if (h.daysLeft == 0) snprintf(buf, sizeof(buf), "¡Hoy es feriado! %s", h.nombre);
    else if (h.daysLeft == 1) snprintf(buf, sizeof(buf), "Mañana feriado: %s", h.nombre);
    else snprintf(buf, sizeof(buf), "Feriado %s %d/%d (en %d días)", dayShort(h.wday), h.mday, h.month, h.daysLeft);
  } else snprintf(buf, sizeof(buf), "Feriados: sin datos");
  text(3, 181, fit(buf, W - 6).c_str());
}

static void pageWeather() {
  char buf[64];
  drawHeader("Clima");
  if (!g_weather.valid) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin datos de clima");
    font(F_R08);
    textCenter(W / 2, 116, "Se cargan en la próxima sync");
    return;
  }
  // Ciudad en su propia línea + hora de actualización
  char hm[8]; formatHHMM(g_weather.updated, hm, sizeof(hm));
  font(F_R08);
  snprintf(buf, sizeof(buf), "act. %s", hm);
  int wAct = tw(buf);
  font(F_R10);
  int wCity = tw(g_cfg.cityName);
  if (wCity + wAct + 12 <= W - 6) {               // entran ciudad y hora de actualización
    text(3, 30, g_cfg.cityName);
    font(F_R08);
    textRight(W - 3, 30, buf);
  } else {                                        // ciudad larga: prioridad a la ciudad
    text(3, 30, fit(g_cfg.cityName, W - 6).c_str());
  }

  // Condición actual
  drawWeatherIcon(2, 36, 48, weatherIcon(g_weather.code, g_weather.isDay));
  font(F_BIG);
  snprintf(buf, sizeof(buf), "%.0f°", g_weather.temp);
  text(58, 66, buf);
  int xr = 58 + tw(buf) + 10;
  font(F_R08);
  snprintf(buf, sizeof(buf), "ST %.0f°", g_weather.feels); text(xr, 48, buf);
  snprintf(buf, sizeof(buf), "Hum %.0f%%", g_weather.hum); text(xr, 60, buf);
  snprintf(buf, sizeof(buf), "%.0f km/h", g_weather.wind); text(xr, 72, buf);
  font(F_B08);
  text(58, 85, fit(weatherText(g_weather.code), 140).c_str());
  font(F_R08);
  const WeatherDay& t = g_weather.day[0];
  snprintf(buf, sizeof(buf), "Hoy: máx %d° · mín %d° · lluvia %d%%", t.tmax, t.tmin, t.rainProb);
  text(3, 98, fit(buf, W - 6).c_str());
  hline(103);

  // Pronóstico 3 días
  for (int i = 1; i <= 3; i++) {
    const WeatherDay& d = g_weather.day[i];
    int cx = 33 + (i - 1) * 67;
    font(F_B08);
    snprintf(buf, sizeof(buf), "%s %d", dayShort(d.wday), d.mday);
    textCenter(cx, 116, buf);
    drawWeatherIcon(cx - 16, 120, 32, weatherIcon(d.code, true));
    font(F_B08);
    snprintf(buf, sizeof(buf), "%d° / %d°", d.tmax, d.tmin);
    textCenter(cx, 167, buf);
    font(F_R08);
    snprintf(buf, sizeof(buf), "lluvia %d%%", d.rainProb);
    textCenter(cx, 180, buf);
    if (i < 3) display.drawFastVLine(cx + 33, 108, 76, GxEPD_BLACK);
  }
}

static void pageDolar() {
  drawHeader("Dólar");
  if (!g_dolar.valid) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin cotizaciones");
    return;
  }
  font(F_B08);
  text(6, 30, "Cotización");
  textRight(132, 30, "Compra");
  textRight(194, 30, "Venta");
  hline(33, 4, W - 4);
  for (int i = 0; i < g_dolar.n; i++) {
    const DolarItem& it = g_dolar.item[i];
    int y = 50 + i * 22;
    bool blue = (strcmp(it.nombre, "Blue") == 0);
    font(blue ? F_B12 : F_R10);
    text(6, y, it.nombre);
    font(blue ? F_B12 : F_R10);
    textRight(132, y, money(it.compra).c_str());
    textRight(194, y, money(it.venta).c_str());
    if (i < g_dolar.n - 1) display.drawFastHLine(4, y + 6, W - 8, GxEPD_BLACK);
  }
  font(F_R08);
  char hm[8]; formatHHMM(g_dolar.updated, hm, sizeof(hm));
  char buf[48];
  snprintf(buf, sizeof(buf), "dolarapi.com · %s", hm);
  textCenter(W / 2, 184, buf);
}

static void pageNews() {
  char buf[48];
  drawHeader("Noticias");
  if (!g_news.valid || g_news.n == 0) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin noticias");
    font(F_R08);
    textCenter(W / 2, 116, "Se cargan en la próxima sync");
    return;
  }
  // Medio + hora de actualización, en su propia línea
  char hm[8]; formatHHMM(g_news.updated, hm, sizeof(hm));
  font(F_R08);
  snprintf(buf, sizeof(buf), "act. %s", hm);
  textRight(W - 3, 30, buf);
  font(F_B10);
  text(3, 30, g_news.source);
  hline(34, 3, W - 3);

  font(F_R08);
  int y = 47;
  String lines[3];
  for (int i = 0; i < g_news.n && y <= 184; i++) {
    int n = wrap(g_news.title[i], W - 16, lines, 3);
    if (y + (n - 1) * LINE_H8 > 184) {                   // no entra completo: recortar con "..."
      int fitN = max(1, (184 - y) / LINE_H8 + 1);
      if (fitN < n) { lines[fitN - 1] = fit((lines[fitN - 1] + " " + lines[fitN]).c_str(), W - 16); n = fitN; }
    }
    display.fillRect(4, y - 6, 3, 3, GxEPD_BLACK);      // bullet cuadrado
    for (int l = 0; l < n; l++) { text(12, y, lines[l].c_str()); y += LINE_H8; }
    y += 3;
  }
}

static void pageHolidays() {
  drawHeader("Feriados");
  if (!g_holidays.valid || g_holidays.n == 0) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin datos de feriados");
    return;
  }
  char buf[48];
  int y = 33;
  String lines[2];
  for (int i = 0; i < g_holidays.n; i++) {
    const Holiday& h = g_holidays.item[i];
    font(F_R08);
    int n = wrap(h.nombre, W - 8, lines, 2);
    int blockH = 13 + n * LINE_H8;                  // título + líneas del nombre
    if (y + blockH > 186) break;                    // no entra: no dibujar a medias
    // Título: fecha (negrita) + tipo (chico)   |   "en N días" a la derecha
    font(F_B10);
    snprintf(buf, sizeof(buf), "%s %d %s", dayShort(h.wday), h.mday, monthShort(h.month - 1));
    text(4, y, buf);
    int xTipo = 4 + tw(buf) + 6;
    font(F_B08);
    if (h.daysLeft == 0) snprintf(buf, sizeof(buf), "¡HOY!");
    else if (h.daysLeft == 1) snprintf(buf, sizeof(buf), "mañana");
    else snprintf(buf, sizeof(buf), "en %d días", h.daysLeft);
    textRight(W - 4, y, buf);
    int xRight = W - 4 - tw(buf);
    if (h.tipo[0]) {
      font(F_TINY);
      if (xTipo + tw(h.tipo) < xRight - 4) text(xTipo, y, h.tipo);
    }
    font(F_R08);
    for (int l = 0; l < n; l++) text(4, y + 13 + l * LINE_H8, lines[l].c_str());
    y += blockH + 6;
    if (i < g_holidays.n - 1 && y < 180) {
      display.drawFastHLine(4, y - 1, W - 8, GxEPD_BLACK);   // separador debajo del bloque
      y += 13;
    }
  }
}

static void pageWifi() {
  drawHeader("Wi-Fi");
  char buf[64];
  font(F_R08);
  int y = 31;
  const char* estado = g_state.lastError == ERR_NO_CREDENTIALS ? "Sin configurar" : (g_state.wifiOk ? "Conexión OK" : "Sin conexión");
  snprintf(buf, sizeof(buf), "Estado: %s", estado); text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Red: %s", g_state.ssid[0] ? g_state.ssid : "—"); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;
  snprintf(buf, sizeof(buf), "IP: %s", g_state.ip[0] ? g_state.ip : "—"); text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Señal: %d dBm", g_state.rssi); text(4, y, buf);
  drawSignal(100, y - 9, g_state.wifiOk, g_state.rssi); y += LINE_H8;
  char hm[8];
  formatHHMM(g_state.lastSync, hm, sizeof(hm));
  snprintf(buf, sizeof(buf), "Sync anterior: %s", hm); text(4, y, buf); y += LINE_H8;
  formatHHMM(g_state.lastSync ? g_state.lastSync + (time_t)g_cfg.intervalMin * 60 : 0, hm, sizeof(hm));
  snprintf(buf, sizeof(buf), "Próxima: %s (cada %u min)", hm, g_cfg.intervalMin); text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Ciudad: %s", g_cfg.cityName); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;
  const NewsSource* ns = findNewsSource(g_cfg.news);
  snprintf(buf, sizeof(buf), "Noticias: %s", ns ? ns->name : g_cfg.news); text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Modo: %s · tema %s", g_alwaysOn ? (g_usbHost && !g_cfg.alwaysOn ? "siempre on (USB)" : "siempre on") : "bajo consumo", g_cfg.darkMode ? "oscuro" : "claro"); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;

  hline(y - 4);
  y += 8;
  font(F_B08);
  text(4, y, "Configurar red / ciudad / noticias:"); y += LINE_H8;
  font(F_R08);
  text(4, y, "Mantené BOOT 2 s: abre el portal"); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Red %s (clave %s)", AP_NAME, AP_PASSWORD); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;
  text(4, y, "Mantené PWR 2 s: actualizar ahora");
}

static void pageSystem() {
  drawHeader("Sistema");
  char buf[64];
  font(F_R08);
  int y = 31;
  if (Board::onUsbPower(g_batteryMv)) snprintf(buf, sizeof(buf), "Alimentación: USB (%.2f V)", g_batteryMv / 1000.0f);
  else snprintf(buf, sizeof(buf), "Batería: %.2f V (%d%%)", g_batteryMv / 1000.0f, Board::batteryPercent(g_batteryMv));
  text(4, y, buf); y += LINE_H8;
  if (g_indoor.valid) snprintf(buf, sizeof(buf), "Interior: %.1f °C · %.0f %% HR", g_indoor.temp, g_indoor.hum);
  else snprintf(buf, sizeof(buf), "Interior: sensor sin lectura");
  text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Firmware: ePaper Monitor AR v%s", FW_VERSION); text(4, y, buf); y += LINE_H8;
  text(4, y, fit("Placa: Waveshare ePaper-1.54 (S3) V2", W - 8).c_str()); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Modo: %s · tema %s", g_alwaysOn ? (g_usbHost && !g_cfg.alwaysOn ? "siempre on (USB)" : "siempre on") : "bajo consumo", g_cfg.darkMode ? "oscuro" : "claro"); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Arranques: %lu · Intervalo: %u min", (unsigned long)g_state.bootCount, g_cfg.intervalMin); text(4, y, buf); y += LINE_H8;
  snprintf(buf, sizeof(buf), "RAM libre: %u KB · PSRAM: %u KB", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024)); text(4, y, buf); y += LINE_H8;
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(buf, sizeof(buf), "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); text(4, y, buf); y += LINE_H8;
  if (g_state.timeValid) {
    snprintf(buf, sizeof(buf), "Hora: %02d:%02d:%02d · %s", g_now.tm_hour, g_now.tm_min, g_now.tm_sec, "UTC-3 (ARG)");
  } else snprintf(buf, sizeof(buf), "Hora: sin sincronizar");
  text(4, y, buf); y += LINE_H8;

  hline(y - 4);
  y += 8;
  font(F_B08);
  text(4, y, "Botones"); y += LINE_H8;
  font(F_R08);
  text(4, y, "BOOT: siguiente · PWR: anterior"); y += LINE_H8;
  text(4, y, "BOOT 2 s: portal Wi-Fi · PWR 2 s: sync"); y += LINE_H8;
  text(4, y, "PWR 6 s: apagar");
}

// ============================================================================
//  Sección Mar: curva de marea + próximas pleamares/bajamares + olas/agua/viento
// ============================================================================
static void drawTideCurve(int x0, int y0, int w, int h, time_t from, int hours) {
  // rango de niveles en la ventana
  int16_t lo = INT16_MAX, hi = INT16_MIN;
  float hStart = (float)(from - g_marine.t0) / 3600.0f;
  for (int i = 0; i < MARINE_HOURS; i++) {
    if (g_marine.level[i] == LEVEL_NONE) continue;
    if (i < (int)hStart - 1 || i > (int)hStart + hours + 1) continue;
    lo = min(lo, g_marine.level[i]);
    hi = max(hi, g_marine.level[i]);
  }
  if (hi <= lo) return;
  int span = hi - lo;
  auto yOf = [&](float cm) { return (int)lroundf(y0 + h - 2 - (cm - lo) * (h - 4) / (float)span); };
  auto xOf = [&](float hrs) { return (int)lroundf(x0 + (hrs - hStart) * w / (float)hours); };

  // ejes: línea base punteada al nivel medio (0) si está en el rango
  if (lo < 0 && hi > 0) {
    int y = yOf(0);
    for (int x = x0; x < x0 + w; x += 4) display.drawPixel(x, y, GxEPD_BLACK);
  }
  // curva: interpolación lineal por décimos de hora
  int prevX = -1, prevY = -1;
  for (float hr = hStart; hr <= hStart + hours; hr += 0.1f) {
    int i = (int)floorf(hr);
    if (i < 0 || i + 1 >= MARINE_HOURS) break;
    if (g_marine.level[i] == LEVEL_NONE || g_marine.level[i + 1] == LEVEL_NONE) { prevX = -1; continue; }
    float f = hr - i;
    float cm = g_marine.level[i] + (g_marine.level[i + 1] - g_marine.level[i]) * f;
    int x = xOf(hr), y = yOf(cm);
    if (prevX >= 0) { display.drawLine(prevX, prevY, x, y, GxEPD_BLACK); display.drawLine(prevX, prevY + 1, x, y + 1, GxEPD_BLACK); }
    prevX = x; prevY = y;
  }
  // marca de "ahora"
  int xNow = xOf(hStart);
  for (int y = y0; y < y0 + h; y += 3) display.drawPixel(xNow, y, GxEPD_BLACK);
  // marcas horarias cada 6 h
  for (int k = 6; k < hours; k += 6) {
    int x = xOf(hStart + k);
    display.drawFastVLine(x, y0 + h - 3, 3, GxEPD_BLACK);
  }
  display.drawFastHLine(x0, y0 + h, w, GxEPD_BLACK);
}

static void pageMarine() {
  drawHeader("Mar");
  char buf[64], hm[8];
  if (!g_marine.valid) {
    font(F_R10);
    textCenter(W / 2, 96, "Sin datos del mar");
    font(F_R08);
    textCenter(W / 2, 112, "Se cargan en la próxima sync o la");
    textCenter(W / 2, 123, "ubicación está lejos de la costa.");
    return;
  }
  time_t now = time(nullptr);
  formatHHMM(g_marine.updated, hm, sizeof(hm));
  font(F_R08);
  snprintf(buf, sizeof(buf), "Marea próximas 24 h · act. %s", hm);
  text(3, 29, buf);
  drawTideCurve(6, 33, W - 12, 50, now, 24);
  font(F_TINY);
  text(4, 92, "ahora");
  textCenter(W / 2, 92, "alturas s/ bajamar min.");
  textRight(W - 4, 92, "+24 h");

  // próximas pleamares / bajamares en dos columnas; alturas sobre el nivel mínimo de la
  // serie (aprox. la bajamar más baja, como en las tablas de marea)
  int16_t datum = INT16_MAX;
  for (int i = 0; i < MARINE_HOURS; i++) if (g_marine.level[i] != LEVEL_NONE) datum = min(datum, g_marine.level[i]);
  int y = 107;
  int shown = 0;
  for (int i = 0; i < g_marine.nExt && shown < 6; i++) {
    const TideExtreme& e = g_marine.ext[i];
    if (e.t < now - 1800) continue;                  // ya pasó (la caché se renueva cada 3 h)
    int col = shown % 2, row = shown / 2;
    int x = 6 + col * 98, yy = y + row * 14;
    if (e.high) display.fillTriangle(x, yy, x + 7, yy, x + 3, yy - 7, GxEPD_BLACK);
    else display.fillTriangle(x, yy - 7, x + 7, yy - 7, x + 3, yy, GxEPD_BLACK);
    formatHHMM(e.t, hm, sizeof(hm));
    font(F_B08);
    snprintf(buf, sizeof(buf), "%s %s", e.high ? "Alta" : "Baja", hm);
    text(x + 11, yy, buf);
    font(F_R08);
    snprintf(buf, sizeof(buf), "%.1f m", (e.cm - datum) / 100.0f);
    textRight(x + 93, yy, buf);
    shown++;
  }
  if (shown == 0) { font(F_R08); text(6, y, "Sin extremos en las próximas horas"); }

  hline(150, 3, W - 3);
  font(F_R08);
  y = 162;
  if (!isnan(g_marine.waveNow)) snprintf(buf, sizeof(buf), "Olas %.1f m (máx 24 h %.1f m)", g_marine.waveNow, isnan(g_marine.waveMax24) ? g_marine.waveNow : g_marine.waveMax24);
  else snprintf(buf, sizeof(buf), "Olas: sin dato en esta celda");
  text(4, y, fit(buf, W - 8).c_str());
  y += LINE_H8;
  if (!isnan(g_marine.sst)) snprintf(buf, sizeof(buf), "Agua %.1f °C", g_marine.sst);
  else snprintf(buf, sizeof(buf), "Agua: sin dato");
  if (g_weather.valid) {
    char w2[40];
    snprintf(w2, sizeof(w2), " · Viento %s %.0f km/h", windDirText(g_weather.windDir), g_weather.wind);
    strlcat(buf, w2, sizeof(buf));
  }
  text(4, y, fit(buf, W - 8).c_str());
  y += LINE_H8;
  if (g_weather.valid && g_weather.gust > 0) {
    snprintf(buf, sizeof(buf), "Ráfagas %.0f km/h · Open-Meteo Marine", g_weather.gust);
    text(4, y, fit(buf, W - 8).c_str());
  }
}

// ============================================================================
//  Sección Sol y Luna
// ============================================================================
static void drawMoon(int cx, int cy, int r, double phase) {
  // Hemisferio sur: la luna creciente se ilumina por la IZQUIERDA.
  // La luna usa colores físicos (iluminado = blanco real) sin importar el tema: como el
  // buffer invierte en modo oscuro, se pasan los colores "al revés" para compensar.
  const uint16_t litSide  = g_cfg.darkMode ? GxEPD_BLACK : GxEPD_WHITE;
  const uint16_t darkSide = g_cfg.darkMode ? GxEPD_WHITE : GxEPD_BLACK;
  display.drawCircle(cx, cy, r, GxEPD_BLACK);
  display.drawCircle(cx, cy, r - 1, GxEPD_BLACK);
  float t = cosf(2.0f * (float)M_PI * (float)phase);   // 1 nueva ... -1 llena ... 1
  bool waxing = phase < 0.5;
  for (int dy = -r + 2; dy <= r - 2; dy++) {
    float wf = sqrtf((float)(r - 2) * (r - 2) - (float)dy * dy);
    int w = (int)wf;
    int xT = (int)lroundf(t * wf);        // terminador (hemisferio norte, iluminado a la derecha)
    int a, b;                              // tramo iluminado en x relativo al centro (hemisferio norte)
    if (waxing) { a = xT; b = w; } else { a = -w; b = -xT; }
    int sa = -b, sb = -a;                  // espejado para el hemisferio sur
    display.drawFastHLine(cx - w, cy + dy, 2 * w + 1, darkSide);            // disco en sombra
    if (sb >= sa) display.drawFastHLine(cx + sa, cy + dy, sb - sa + 1, litSide);  // parte iluminada
  }
}

static void drawSunIcon(int cx, int cy, int r) { sun(cx, cy, r, r + 3, r + 7); }

static void pageSun() {
  drawHeader("Sol y Luna");
  char buf[64];
  if (!g_sun.valid) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin datos del sol");
    font(F_R08);
    textCenter(W / 2, 116, "Se cargan en la próxima sync");
  } else {
    drawSunIcon(22, 46, 9);
    font(F_B10);
    snprintf(buf, sizeof(buf), "Amanece %02d:%02d", g_sun.sunrise / 60, g_sun.sunrise % 60);
    text(44, 40, buf);
    snprintf(buf, sizeof(buf), "Atardece %02d:%02d", g_sun.sunset / 60, g_sun.sunset % 60);
    text(44, 56, buf);
    font(F_R08);
    int dl = g_sun.daylight, delta = (g_sun.daylightT - g_sun.daylight) / 60;
    snprintf(buf, sizeof(buf), "Día de %dh %02dm (mañana %+d min)", dl / 3600, (dl % 3600) / 60, delta);
    text(4, 72, fit(buf, W - 8).c_str());
    snprintf(buf, sizeof(buf), "UV máx %.1f (%s)", g_sun.uvMax, uvText(g_sun.uvMax));
    text(4, 84, buf);
    if (g_sun.uvMax >= 6) { font(F_B08); textRight(W - 4, 84, "¡protector!"); }
  }
  hline(90, 3, W - 3);

  // Luna (fase calculada localmente)
  time_t now = time(nullptr);
  double ph = g_state.timeValid ? moonPhase(now) : 0.0;
  float illum = (1.0f - cosf(2.0f * (float)M_PI * (float)ph)) / 2.0f * 100.0f;
  drawMoon(36, 130, 26, ph);
  font(F_B10);
  text(72, 118, moonPhaseName(ph));
  font(F_R08);
  snprintf(buf, sizeof(buf), "Iluminación %.0f%%", illum);
  text(72, 131, buf);
  double toFull = (ph < 0.5) ? (0.5 - ph) : (1.5 - ph);
  double toNew  = 1.0 - ph;
  snprintf(buf, sizeof(buf), "Llena en %.0f días", toFull * 29.53);
  text(72, 143, buf);
  snprintf(buf, sizeof(buf), "Nueva en %.0f días", toNew * 29.53);
  text(72, 155, buf);
  font(F_TINY);
  text(72, 166, "(hemisferio sur)");
  hline(172, 3, W - 3);
  font(F_R08);
  if (g_state.timeValid) {
    int nowMin = g_now.tm_hour * 60 + g_now.tm_min;
    if (g_sun.valid && nowMin < g_sun.sunrise) snprintf(buf, sizeof(buf), "Amanece en %dh %02dm", (g_sun.sunrise - nowMin) / 60, (g_sun.sunrise - nowMin) % 60);
    else if (g_sun.valid && nowMin < g_sun.sunset) snprintf(buf, sizeof(buf), "Atardece en %dh %02dm", (g_sun.sunset - nowMin) / 60, (g_sun.sunset - nowMin) % 60);
    else if (g_sun.valid) snprintf(buf, sizeof(buf), "Mañana amanece %02d:%02d", g_sun.sunriseT / 60, g_sun.sunriseT % 60);
    else snprintf(buf, sizeof(buf), "—");
    textCenter(W / 2, 185, buf);
  }
}

// ============================================================================
//  Sección Economía
// ============================================================================
static void pageEcon() {
  drawHeader("Economía");
  char buf[64], hm[8];
  if (!g_econ.valid) {
    font(F_R10);
    textCenter(W / 2, 100, "Sin datos económicos");
    font(F_R08);
    textCenter(W / 2, 116, "Se cargan en la próxima sync");
    return;
  }
  int y = 36;
  font(F_B10); text(4, y, "Riesgo país");
  font(F_B14);
  snprintf(buf, sizeof(buf), "%d", g_econ.riesgoPais);
  textRight(W - 4, y + 2, buf);
  font(F_TINY);
  snprintf(buf, sizeof(buf), "pb · %s", g_econ.riesgoFecha);
  textRight(W - 4, y + 12, buf);
  y += 22;
  display.drawFastHLine(4, y - 5, W - 8, GxEPD_BLACK);
  y += 8;
  font(F_B10); text(4, y, "Inflación");
  font(F_R10);
  snprintf(buf, sizeof(buf), "%s %.1f%%", g_econ.inflMes[0] ? g_econ.inflMes : "mes", g_econ.inflMensual);
  textRight(W - 4, y, buf);
  y += 13;
  font(F_R08);
  snprintf(buf, sizeof(buf), "interanual %.1f%%", g_econ.inflInteranual);
  textRight(W - 4, y, buf);
  y += 10;
  display.drawFastHLine(4, y - 3, W - 8, GxEPD_BLACK);
  y += 12;
  font(F_B08); text(4, y, "Moneda"); textRight(132, y, "Compra"); textRight(W - 4, y, "Venta");
  y += 13;
  font(F_R10);
  text(4, y, "Euro");   textRight(132, y, money(g_econ.euroCompra).c_str()); textRight(W - 4, y, money(g_econ.euroVenta).c_str());
  y += 14;
  text(4, y, "Real");   textRight(132, y, money(g_econ.realCompra).c_str()); textRight(W - 4, y, money(g_econ.realVenta).c_str());
  y += 10;
  display.drawFastHLine(4, y - 3, W - 8, GxEPD_BLACK);
  y += 12;
  font(F_B08); text(4, y, "Cripto (US$)");
  font(F_R10);
  snprintf(buf, sizeof(buf), "BTC %s", money(g_econ.btcUsd).c_str());
  text(70, y, buf);
  y += 14;
  snprintf(buf, sizeof(buf), "ETH %s", money(g_econ.ethUsd).c_str());
  text(70, y, buf);
  formatHHMM(g_econ.updated, hm, sizeof(hm));
  font(F_TINY);
  snprintf(buf, sizeof(buf), "ArgentinaDatos - DolarApi - CoinGecko - %s", hm);
  textCenter(W / 2, 185, fit(buf, W - 6).c_str());
}

// ============================================================================
//  Sección Interior: temperatura y humedad con historial de 24 h
// ============================================================================
static void drawSparkline(int x0, int y0, int w, int h, bool humidity) {
  if (g_hist.count < 2) { font(F_TINY); text(x0, y0 + h / 2, "juntando muestras..."); return; }
  int n = g_hist.count;
  float lo = 1e9, hi = -1e9;
  for (int k = 0; k < n; k++) {
    int idx = (g_hist.head + INDOOR_SAMPLES - n + k) % INDOOR_SAMPLES;
    float v = humidity ? g_hist.hum[idx] : g_hist.temp[idx] / 10.0f;
    lo = min(lo, v); hi = max(hi, v);
  }
  if (hi - lo < (humidity ? 4.0f : 1.0f)) { float mid = (hi + lo) / 2; lo = mid - (humidity ? 2.0f : 0.5f); hi = mid + (humidity ? 2.0f : 0.5f); }
  int prevX = -1, prevY = -1;
  for (int k = 0; k < n; k++) {
    int idx = (g_hist.head + INDOOR_SAMPLES - n + k) % INDOOR_SAMPLES;
    float v = humidity ? g_hist.hum[idx] : g_hist.temp[idx] / 10.0f;
    int x = x0 + (int)lroundf((float)k * (w - 1) / (float)(INDOOR_SAMPLES - 1));
    int y = y0 + h - 1 - (int)lroundf((v - lo) * (h - 2) / (hi - lo));
    if (prevX >= 0) display.drawLine(prevX, prevY, x, y, GxEPD_BLACK);
    prevX = x; prevY = y;
  }
  display.drawFastHLine(x0, y0 + h, w, GxEPD_BLACK);
  char buf[16];
  font(F_TINY);
  snprintf(buf, sizeof(buf), humidity ? "%.0f%%" : "%.1f", hi); text(x0 + w + 3, y0 + 6, buf);
  snprintf(buf, sizeof(buf), humidity ? "%.0f%%" : "%.1f", lo); text(x0 + w + 3, y0 + h, buf);
}

static void pageIndoor() {
  drawHeader("Interior");
  char buf[64];
  if (g_indoor.valid) {
    drawThermo(6, 28);
    font(F_BIG);
    snprintf(buf, sizeof(buf), "%.1f°", g_indoor.temp);
    text(20, 50, buf);
    drawDrop(108, 30);
    snprintf(buf, sizeof(buf), "%.0f%%", g_indoor.hum);
    text(122, 50, buf);
  } else {
    font(F_R10);
    text(6, 46, "Sensor SHTC3 sin lectura");
  }
  font(F_R08);
  text(4, 68, "Temperatura · últimas 24 h");
  drawSparkline(4, 72, 160, 36, false);
  font(F_R08);
  text(4, 128, "Humedad · últimas 24 h");
  drawSparkline(4, 132, 160, 36, true);
  font(F_TINY);
  if (g_alwaysOn) snprintf(buf, sizeof(buf), "%u muestras/15 min (USB: lee de mas)", g_hist.count);
  else snprintf(buf, sizeof(buf), "%u muestras (1 cada 15 min)", g_hist.count);
  text(4, 184, fit(buf, W - 8).c_str());
}

// ============================================================================
//  API
// ============================================================================
void begin(bool coldBoot) {
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
  display.init(0, coldBoot, 10, false);
  display.setRotation(EPD_ROTATION);
  display.setTextColor(GxEPD_BLACK);
  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
}

static void drawPage(uint8_t page) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  switch (page) {
    case PAGE_CLOCK:    pageClock(); break;
    case PAGE_WEATHER:  pageWeather(); break;
    case PAGE_MARINE:   pageMarine(); break;
    case PAGE_SUN:      pageSun(); break;
    case PAGE_DOLAR:    pageDolar(); break;
    case PAGE_ECON:     pageEcon(); break;
    case PAGE_NEWS:     pageNews(); break;
    case PAGE_HOLIDAYS: pageHolidays(); break;
    case PAGE_INDOOR:   pageIndoor(); break;
    case PAGE_WIFI:     pageWifi(); break;
    case PAGE_SYSTEM:   pageSystem(); break;
    default:            pageClock(); break;
  }
  drawNav(page);
}

void render(uint8_t page, bool fullRefresh) {
  display.dark = g_cfg.darkMode;
  drawPage(page);
  display.display(!fullRefresh);
}

void setDarkMode(bool on) { display.dark = on; }

void dumpBuffer(uint8_t page) {
  Serial.printf("[fb-begin %u]\n", page);
  String b64 = base64::encode(display.shadow, sizeof(display.shadow));
  for (size_t i = 0; i < b64.length(); i += 100) Serial.println(b64.substring(i, i + 100));
  Serial.println("[fb-end]");
}

void dumpAllPages(uint8_t currentPage) {
  display.dark = g_cfg.darkMode;
  for (uint8_t p = 0; p < PAGE_COUNT; p++) { drawPage(p); dumpBuffer(p); }
  drawPage(currentPage);   // dejar el buffer como estaba
}

void renderSplash(const char* status) {
  // Portada minimalista: jerarquía tipográfica, sin marco, mucho aire.
  const int M = 16;                       // margen izquierdo/derecho
  display.dark = g_cfg.darkMode;
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  // Overline con tracking + barra de acento
  font(F_TINY);
  textSpaced(M, 44, "ARGENTINA", 3);
  display.fillRect(M, 50, 28, 3, GxEPD_BLACK);

  // Nombre de la app: "ePaper" liviano + "Monitor" en negrita
  font(F_R24);
  text(M - 1, 86, "ePaper");
  font(F_BIG);
  text(M - 1, 114, "Monitor");

  // Tagline
  font(F_R08);
  text(M, 132, "Hora · Clima · Mar · Dólar · Noticias");

  // Estado (con un pequeño indicador)
  display.fillRect(M, 148, 3, 3, GxEPD_BLACK);
  text(M + 8, 151, status);

  // Firma: logo chico + versión, sobre una línea fina
  display.drawFastHLine(M, 168, W - 2 * M, GxEPD_BLACK);
  display.drawBitmap(M, 178, LOGO_DELO, LOGO_DELO_W, LOGO_DELO_H, GxEPD_BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "v%s", FW_VERSION);
  font(F_TINY);
  textRight(W - M, 189, buf);
  display.display(false);
}

void renderPortal() {
  display.dark = g_cfg.darkMode;
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  font(F_B12);
  textCenter(W / 2, 14, "Configurar Wi-Fi");
  hline(18);

  // QR con la red del portal (WIFI:T:nopass;S:<ssid>;;)
  QRCode qr;
  uint8_t qrData[160];
  char payload[64];
  snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", AP_NAME, AP_PASSWORD);
  qrcode_initText(&qr, qrData, 3, ECC_LOW, payload);
  const int scale = 3, x0 = 4, y0 = 24;
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, GxEPD_BLACK);

  int x = x0 + qr.size * scale + 6;
  font(F_R08);
  text(x, 34, "1) Escaneá el QR");
  text(x, 45, "o conectate a:");
  font(F_B08);
  text(x, 57, AP_NAME);
  font(F_R08);
  text(x, 68, "clave:");
  font(F_B08);
  text(x + 32, 68, AP_PASSWORD);
  font(F_R08);
  text(x, 84, "2) Abrí en el");
  text(x, 95, "navegador:");
  font(F_B08);
  text(x, 107, "192.168.4.1");
  font(F_R08);
  text(x, 122, "3) Elegí tu red,");
  text(x, 133, "poné la clave y");
  text(x, 144, "guardá.");

  hline(146);
  text(4, 158, "También podés cambiar ciudad, intervalo");
  text(4, 169, "y fuente de noticias. Se cierra a los 5 min.");
  font(F_B08);
  textCenter(W / 2, 190, "Esperando configuración…");
  display.display(false);
}

void renderMessage(const char* title, const char* line1, const char* line2, bool fullRefresh) {
  display.dark = g_cfg.darkMode;
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  font(F_B14);
  textCenter(W / 2, 80, title);
  font(F_R10);
  if (line1) textCenter(W / 2, 108, line1);
  if (line2) textCenter(W / 2, 126, line2);
  display.display(!fullRefresh);
}

void hibernate() { display.hibernate(); }

}  // namespace UI
