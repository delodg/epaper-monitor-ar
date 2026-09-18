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
};

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
  float  temp, feels, hum, wind;
  int8_t code;
  bool   isDay;
  WeatherDay day[4];   // hoy + 3 días
  time_t updated;
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
  bool     wifiOk;            // última conexión exitosa
  bool     timeValid;
  char     ssid[33];
  char     ip[16];
  int8_t   rssi;
  uint8_t  lastError;         // código informativo para la pantalla Wi-Fi
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
