#include "appdata.h"

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
