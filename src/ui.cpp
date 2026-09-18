#include "ui.h"
#include "config.h"
#include "appdata.h"
#include "board.h"

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
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
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

static const int LINE_H8 = 11;   // interlineado para fuentes de 8 px

// ---- helpers de texto ----
static void font(const uint8_t* f) { u8g2.setFont(f); }
static int  tw(const char* s) { return u8g2.getUTF8Width(s); }
static void text(int x, int y, const char* s) { u8g2.setCursor(x, y); u8g2.print(s); }
static void textRight(int xr, int y, const char* s) { text(xr - tw(s), y, s); }
static void textCenter(int cx, int y, const char* s) { text(cx - tw(s) / 2, y, s); }

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
    snprintf(buf, sizeof(buf), "%.0f km/h", g_weather.wind); text(xr, 116, buf);
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
  snprintf(buf, sizeof(buf), "Modo: %s", g_alwaysOn ? (g_usbHost && !g_cfg.alwaysOn ? "siempre encendido (USB)" : "siempre encendido") : "bajo consumo"); text(4, y, buf); y += LINE_H8;

  hline(y - 4);
  y += 8;
  font(F_B08);
  text(4, y, "Configurar red / ciudad / noticias:"); y += LINE_H8;
  font(F_R08);
  text(4, y, "Mantené BOOT 2 s: abre el portal"); y += LINE_H8;
  snprintf(buf, sizeof(buf), "Red \"%s\" -> 192.168.4.1", AP_NAME); text(4, y, fit(buf, W - 8).c_str()); y += LINE_H8;
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
  snprintf(buf, sizeof(buf), "Modo: %s", g_alwaysOn ? (g_usbHost && !g_cfg.alwaysOn ? "siempre encendido (USB)" : "siempre encendido") : "bajo consumo (deep-sleep)"); text(4, y, buf); y += LINE_H8;
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
    case PAGE_DOLAR:    pageDolar(); break;
    case PAGE_NEWS:     pageNews(); break;
    case PAGE_HOLIDAYS: pageHolidays(); break;
    case PAGE_WIFI:     pageWifi(); break;
    case PAGE_SYSTEM:   pageSystem(); break;
    default:            pageClock(); break;
  }
  drawNav(page);
}

void render(uint8_t page, bool fullRefresh) {
  drawPage(page);
  display.display(!fullRefresh);
}

void dumpBuffer(uint8_t page) {
  Serial.printf("[fb-begin %u]\n", page);
  String b64 = base64::encode(display.shadow, sizeof(display.shadow));
  for (size_t i = 0; i < b64.length(); i += 100) Serial.println(b64.substring(i, i + 100));
  Serial.println("[fb-end]");
}

void dumpAllPages(uint8_t currentPage) {
  for (uint8_t p = 0; p < PAGE_COUNT; p++) { drawPage(p); dumpBuffer(p); }
  drawPage(currentPage);   // dejar el buffer como estaba
}

void renderSplash(const char* status) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.drawRect(6, 6, W - 12, H - 12, GxEPD_BLACK);
  display.drawRect(8, 8, W - 16, H - 16, GxEPD_BLACK);
  font(F_B14);
  textCenter(W / 2, 62, "ePaper Monitor");
  font(F_BIG);
  textCenter(W / 2, 96, "Argentina");
  drawWeatherIcon(W / 2 - 20, 104, 40, ICO_SUNCLOUD);
  font(F_R08);
  textCenter(W / 2, 160, status);
  char buf[32];
  snprintf(buf, sizeof(buf), "v%s · Waveshare ESP32-S3", FW_VERSION);
  textCenter(W / 2, 184, buf);
  display.display(false);
}

void renderPortal() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  font(F_B12);
  textCenter(W / 2, 14, "Configurar Wi-Fi");
  hline(18);

  // QR con la red del portal (WIFI:T:nopass;S:<ssid>;;)
  QRCode qr;
  uint8_t qrData[160];
  char payload[64];
  snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", AP_NAME);
  qrcode_initText(&qr, qrData, 3, ECC_LOW, payload);
  const int scale = 3, x0 = 4, y0 = 24;
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, GxEPD_BLACK);

  int x = x0 + qr.size * scale + 6;
  font(F_R08);
  text(x, 36, "1) Escaneá el QR");
  text(x, 47, "o conectate a:");
  font(F_B08);
  text(x, 60, AP_NAME);
  font(F_R08);
  text(x, 76, "2) Abrí en el");
  text(x, 87, "navegador:");
  font(F_B08);
  text(x, 100, "192.168.4.1");
  font(F_R08);
  text(x, 116, "3) Elegí tu red,");
  text(x, 127, "poné la clave y");
  text(x, 138, "guardá.");

  hline(146);
  text(4, 158, "También podés cambiar ciudad, intervalo");
  text(4, 169, "y fuente de noticias. Se cierra a los 5 min.");
  font(F_B08);
  textCenter(W / 2, 190, "Esperando configuración…");
  display.display(false);
}

void renderMessage(const char* title, const char* line1, const char* line2, bool fullRefresh) {
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
