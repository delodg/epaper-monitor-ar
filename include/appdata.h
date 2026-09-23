#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// ============================================================================
//  Modelo de datos. Todo lo que está en RTC memory (RTC_DATA_ATTR) sobrevive
//  al deep-sleep, así la placa no vuelve a descargar todo en cada despertar.
// ============================================================================

// ---- Configuración persistente (NVS / Preferences) ----
struct Config {
  char     city[40];        // ciudad pedida por el usuario
  char     cityName[40];    // nombre resuelto por el geocoder
  float    lat, lon;
  uint16_t intervalMin;     // minutos entre sincronizaciones
  char     news[12];        // fuente de noticias (clave)
  bool     alwaysOn;        // true = sin deep-sleep (ideal con USB)
  bool     darkMode;        // true = pantalla invertida (fondo negro, tinta blanca)
  uint8_t  profile;         // PowerProfile: rendimiento / equilibrado / ahorro
  bool     nightMode;       // de madrugada, refrescar y sincronizar menos
  float    marineLat, marineLon;   // celda marina que funcionó (se recuerda para no sondear)
};

// Cadencia efectiva (depende del perfil, la noche y la batería)
uint16_t clockIntervalMin();
uint16_t syncIntervalMin();
bool     isNight();
float    estimatedBatteryDays();   // estimación con los tiempos medidos y corrientes típicas

// ---- Clima (Open-Meteo) ----
struct WeatherDay {
  int8_t  code;
  int8_t  tmax, tmin;
  int8_t  rainProb;
  int8_t  wday;          // 0=domingo
  int8_t  mday;
};
struct WeatherData {
  bool   valid;
  float  temp, feels, hum, wind, gust;
  int16_t windDir;     // grados
  int8_t code;
  bool   isDay;
  WeatherDay day[4];   // hoy + 3 días
  time_t updated;
};

// ---- Mar: nivel del mar (marea), olas y agua (Open-Meteo Marine) ----
#define MARINE_HOURS 48
#define LEVEL_NONE   INT16_MIN
struct TideExtreme {
  time_t  t;
  int16_t cm;
  bool    high;
};
struct MarineData {
  bool    valid;
  time_t  t0;                    // epoch de la hora 0 de la serie
  int16_t level[MARINE_HOURS];   // cm sobre el nivel medio; LEVEL_NONE = sin dato
  TideExtreme ext[8];
  uint8_t nExt;
  float   waveNow, waveMax24, sst;   // m, m, °C (NAN si no hay dato)
  float   cellLat, cellLon;          // celda del modelo que devolvió datos
  time_t  updated;
};

// ---- Sol y luna ----
struct SunData {
  bool    valid;
  int16_t sunrise, sunset;       // minutos desde medianoche (hoy)
  int16_t sunriseT, sunsetT;     // mañana
  int32_t daylight, daylightT;   // segundos
  float   uvMax;
  int8_t  mday;                  // día del mes al que corresponde
  time_t  updated;
};

// ---- Economía ----
// Cada campo tiene su propia antigüedad: la inflación es mensual (y cuesta 2 descargas
// grandes), el riesgo país diario, y las cotizaciones/cripto cambian todo el tiempo.
struct EconData {
  bool  valid;
  int16_t riesgoPais;
  char  riesgoFecha[6];          // "17/09"
  float inflMensual, inflInteranual;
  char  inflMes[4];              // "ago"
  float euroCompra, euroVenta, realCompra, realVenta;
  float btcUsd, ethUsd;
  time_t updated;                // última actualización de cotizaciones/cripto
  time_t riesgoUpdated, inflUpdated;
};

// ---- Historial interior (una muestra cada 15 min, 24 h) ----
#define INDOOR_SAMPLES 96
struct IndoorHistory {
  int16_t temp[INDOOR_SAMPLES];  // °C x10
  uint8_t hum[INDOOR_SAMPLES];   // %
  uint8_t head;                  // próxima posición a escribir
  uint8_t count;
  int32_t lastSlot;              // yday*96 + hora*4 + min/15 de la última muestra
};

// ---- Dólar (dolarapi.com) ----
struct DolarItem {
  char  nombre[10];
  float compra, venta;
};
struct DolarData {
  bool      valid;
  DolarItem item[6];
  uint8_t   n;
  time_t    updated;
};

// ---- Noticias (RSS) ----
struct NewsData {
  bool    valid;
  char    source[14];
  char    title[NEWS_MAX_TITLES][140];
  uint8_t n;
  time_t  updated;
};

// ---- Feriados (argentinadatos.com) ----
struct Holiday {
  char   nombre[44];
  char   tipo[14];        // inamovible / trasladable / puente
  int8_t month, mday, wday;
  int16_t daysLeft;
};
struct HolidayData {
  bool    valid;
  Holiday item[4];
  uint8_t n;
  int16_t fetchedYday;    // día del año en que se bajó (se refresca 1 vez por día)
  time_t  updated;
};

// ---- Sensores internos ----
struct IndoorData {
  bool  valid;
  float temp, hum;
};

// ---- Medición de consumo (RTC memory): cuánto tiempo pasa despierta la placa ----
struct PowerStats {
  uint32_t cycles;          // ciclos de despertar medidos
  uint32_t sumBootMs;       // ROM + bootloader + init del core, hasta setup()
  uint32_t sumAppMs;        // desde setup() hasta el deep-sleep
  uint32_t maxAppMs;
  uint32_t syncs, sumSyncMs;
  uint32_t testCycles;      // > 0: modo batería simulado (ignora el USB y duerme fijo)
  uint64_t sleepStartUs;    // gettimeofday antes de dormir (se mantiene en deep-sleep)
  uint32_t sleepReqMs;      // duración pedida al deep-sleep
  uint32_t lastBootMs, lastAppMs;
  uint32_t sumPreMs;        // init del core + PSRAM antes de setup()
};

// ---- Estado de la aplicación (RTC memory) ----
#define STATE_MAGIC 0x41525031  // "ARP1"
struct AppState {
  uint32_t magic;
  uint8_t  page;
  uint32_t bootCount;
  time_t   lastSync;          // epoch UTC de la última sincronización OK
  time_t   lastAttempt;       // último intento (para no reintentar cada minuto sin Wi-Fi)
  time_t   lastFullRefresh;
  time_t   geoEpoch;          // última geolocalización por IP (0 = nunca en este ciclo de energía)
  time_t   ntpEpoch;          // última sincronización NTP (el RTC mantiene la hora entre medio)
  time_t   dolarEpoch;        // última cotización del dólar
  time_t   newsEpoch;         // últimas noticias
  bool     wifiOk;            // última conexión exitosa
  bool     timeValid;
  char     ssid[33];
  char     ip[16];
  int8_t   rssi;
  uint8_t  lastError;         // código informativo para la pantalla Wi-Fi
  bool     poweredOff;        // se durmió por "apagar": el próximo PWR sólo enciende
};

enum SyncError : uint8_t {
  ERR_NONE = 0,
  ERR_NO_CREDENTIALS,
  ERR_WIFI_FAILED,
  ERR_NTP_FAILED,
};

// Definidos en main.cpp
extern Config      g_cfg;
extern AppState    g_state;
extern WeatherData g_weather;
extern DolarData   g_dolar;
extern NewsData    g_news;
extern HolidayData g_holidays;
extern MarineData  g_marine;
extern SunData     g_sun;
extern EconData    g_econ;
extern IndoorHistory g_hist;
extern PowerStats  g_pwr;
extern IndoorData  g_indoor;
extern int         g_batteryMv;
extern struct tm   g_now;     // hora local actual (Argentina)
extern bool        g_alwaysOn; // modo efectivo: config "siempre encendido" o host USB detectado
extern bool        g_usbHost;  // hay una PC conectada por USB

// ---- Fuentes de noticias disponibles ----
struct NewsSource {
  const char* key;
  const char* name;
  const char* url;
};
extern const NewsSource NEWS_SOURCES[];
extern const uint8_t    NEWS_SOURCE_COUNT;
const NewsSource* findNewsSource(const char* key);

// ---- Utilidades de fecha en castellano ----
const char* dayShort(int wday);      // "Dom".."Sáb"
const char* dayLong(int wday);       // "Domingo".."Sábado"
const char* monthShort(int mon);     // "Ene".."Dic"  (0..11)
const char* monthLong(int mon);      // "enero".."diciembre"
int  dayOfWeek(int y, int m, int d); // 0=domingo (m: 1..12)
void formatHHMM(time_t epoch, char* out, size_t n);  // hora local "HH:MM"
const char* windDirText(int deg);                    // "N", "NE", ... "NO"
double moonPhase(time_t epoch);                      // 0 = nueva, 0.5 = llena
const char* moonPhaseName(double phase);
const char* uvText(float uv);
const char* apPassword();                            // clave del portal, única por placa (prefijo + MAC)
