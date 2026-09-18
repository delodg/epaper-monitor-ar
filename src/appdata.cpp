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
