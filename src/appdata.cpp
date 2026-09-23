#include "appdata.h"
#include "board.h"
#include <esp_system.h>

// Clave del portal Wi-Fi: prefijo + últimos 4 dígitos hex del MAC de fábrica (>= 8 caracteres).
// Única por placa y visible sólo en la pantalla, en vez de una clave publicada en el repo.
const char* apPassword() {
  static char pwd[24] = {0};
  if (!pwd[0]) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(pwd, sizeof(pwd), "%s%02x%02x", AP_PASSWORD_PREFIX, mac[4], mac[5]);
  }
  return pwd;
}

// ---- Fuentes de noticias (RSS). Probadas el 2026-09: Infobae, Página/12, TN y La Voz bloquean bots ----
const NewsSource NEWS_SOURCES[] = {
  {"clarin",   "Clarín",    "https://www.clarin.com/rss/lo-ultimo/"},
  {"ambito",   "Ámbito",    "https://www.ambito.com/rss/pages/home.xml"},
  {"perfil",   "Perfil",    "https://www.perfil.com/feed"},
  {"bbc",      "BBC Mundo", "https://feeds.bbci.co.uk/mundo/rss.xml"},
  {"lanacion", "La Nación", "https://www.lanacion.com.ar/arc/outboundfeeds/rss/?outputType=xml"},
};
const uint8_t NEWS_SOURCE_COUNT = sizeof(NEWS_SOURCES) / sizeof(NEWS_SOURCES[0]);

const NewsSource* findNewsSource(const char* key) {
  if (!key) return nullptr;
  for (uint8_t i = 0; i < NEWS_SOURCE_COUNT; i++)
    if (strcmp(NEWS_SOURCES[i].key, key) == 0) return &NEWS_SOURCES[i];
  return nullptr;
}

// ---- Fechas en castellano ----
static const char* DAYS_SHORT[]   = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
static const char* DAYS_LONG[]    = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
static const char* MONTHS_SHORT[] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};
static const char* MONTHS_LONG[]  = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                                     "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};

const char* dayShort(int wday)   { return (wday >= 0 && wday < 7) ? DAYS_SHORT[wday] : "—"; }
const char* dayLong(int wday)    { return (wday >= 0 && wday < 7) ? DAYS_LONG[wday] : "—"; }
const char* monthShort(int mon)  { return (mon >= 0 && mon < 12) ? MONTHS_SHORT[mon] : "—"; }
const char* monthLong(int mon)   { return (mon >= 0 && mon < 12) ? MONTHS_LONG[mon] : "—"; }

// Sakamoto: 0 = domingo. m en 1..12
int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void formatHHMM(time_t epoch, char* out, size_t n) {
  if (epoch <= 0) { snprintf(out, n, "--:--"); return; }
  struct tm t;
  localtime_r(&epoch, &t);
  snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

// ============================================================================
//  Cadencias de energía
//  Cada despertar cuesta ~0,8 s y cada sincronización varios segundos de radio:
//  el perfil, la franja nocturna y la batería baja deciden cada cuánto ocurren.
// ============================================================================
bool isNight() {
  if (!g_cfg.nightMode || !g_state.timeValid) return false;
  int h = g_now.tm_hour;
  return (NIGHT_START_H < NIGHT_END_H) ? (h >= NIGHT_START_H && h < NIGHT_END_H)
                                       : (h >= NIGHT_START_H || h < NIGHT_END_H);
}

// Batería baja (y sin USB): pasar a la cadencia de ahorro aunque el perfil sea otro
static bool lowBattery() {
  return !Board::onUsbPower(g_batteryMv) && Board::batteryPercent(g_batteryMv) <= LOW_BATTERY_PCT;
}

uint16_t clockIntervalMin() {
  if (isNight()) return NIGHT_CLOCK_MIN;
  uint8_t p = lowBattery() ? PWR_SAVER : g_cfg.profile;
  switch (p) {
    case PWR_PERF:   return 1;
    case PWR_SAVER:  return 5;
    default:         return 2;      // equilibrado
  }
}

uint16_t syncIntervalMin() {
  if (isNight()) return NIGHT_SYNC_MIN;
  uint16_t base = g_cfg.intervalMin;
  if (lowBattery()) return base < 60 ? 60 : base;
  if (g_cfg.profile == PWR_SAVER) return base < 60 ? 60 : base;
  if (g_cfg.profile == PWR_BALANCED && base < 30) return 30;
  return base;
}

// Autonomía estimada con los tiempos REALES medidos en la placa (g_pwr) y corrientes
// TÍPICAS del ESP32-S3 + panel (no medidas con amperímetro): despierto sin radio ~42 mA,
// con Wi-Fi ~110 mA, en deep-sleep ~0,15 mA. Sirve para comparar configuraciones.
float estimatedBatteryDays() {
  const float I_AWAKE = 42.0f, I_WIFI = 110.0f, I_SLEEP = 0.15f, CAPACITY_MAH = 1000.0f;
  float awakeMs = g_pwr.cycles ? (float)(g_pwr.sumBootMs + g_pwr.sumAppMs) / g_pwr.cycles : 815.0f;
  float syncMs  = g_pwr.syncs  ? (float)g_pwr.sumSyncMs / g_pwr.syncs : 3000.0f;
  if (syncMs > 20000.0f) syncMs = 20000.0f;         // no extrapolar con la sync inicial completa
  float wakesDay = 1440.0f / (float)clockIntervalMin();
  float syncsDay = 1440.0f / (float)syncIntervalMin();
  float mahDay = wakesDay * (awakeMs / 3600000.0f) * I_AWAKE
               + syncsDay * (syncMs / 3600000.0f) * I_WIFI
               + 24.0f * I_SLEEP;
  return mahDay > 0.1f ? CAPACITY_MAH / mahDay : 0.0f;
}

// ---- Viento: dirección en texto (de dónde viene) ----
const char* windDirText(int deg) {
  static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
  int i = (int)(((deg % 360) + 360) % 360 + 22.5) / 45;
  return dirs[i % 8];
}

// ---- Luna: fase a partir de la luna nueva de referencia (2000-01-06 18:14 UTC) ----
double moonPhase(time_t epoch) {
  const double SYNODIC = 29.530588853;
  double days = ((double)epoch - 947182440.0) / 86400.0;
  double phase = days / SYNODIC;
  phase -= (long)phase;
  if (phase < 0) phase += 1.0;
  return phase;
}

const char* moonPhaseName(double p) {
  if (p < 0.03 || p > 0.97) return "Luna nueva";
  if (p < 0.22) return "Luna creciente";
  if (p < 0.28) return "Cuarto creciente";
  if (p < 0.47) return "Gibosa creciente";
  if (p < 0.53) return "Luna llena";
  if (p < 0.72) return "Gibosa menguante";
  if (p < 0.78) return "Cuarto menguante";
  return "Luna menguante";
}

// ---- Índice UV (escala OMS) ----
const char* uvText(float uv) {
  if (uv < 3) return "bajo";
  if (uv < 6) return "moderado";
  if (uv < 8) return "alto";
  if (uv < 11) return "muy alto";
  return "extremo";
}
