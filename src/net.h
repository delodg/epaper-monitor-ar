#pragma once
#include <Arduino.h>

// Conectividad: Wi-Fi (WiFiManager), NTP y descarga de datos (clima, dólar,
// feriados, noticias). Todo escribe en las estructuras globales de appdata.h.
namespace Net {

bool hasCredentials();                    // hay una red guardada en NVS
bool connect(uint32_t timeoutMs);         // conecta con la red guardada
bool isConnected();
void disconnect();                        // apaga la radio
bool runPortal();                         // portal cautivo bloqueante (AP_NAME); true si quedó conectado
void loadConfig();                        // Preferences -> g_cfg
void saveConfig();                        // g_cfg -> Preferences
void updateLinkInfo();                    // SSID / IP / RSSI -> g_state

bool syncTime();                          // NTP -> reloj del sistema + RTC externo
bool cityIsAuto();                        // ciudad "auto" = geolocalizar por la conexión
bool geolocateByIp();                     // IP pública -> lat/lon/ciudad (ip-api.com / ipwho.is)
bool geocodeCity();                       // g_cfg.city -> lat/lon/cityName (Open-Meteo geocoding)
bool fetchWeather();
bool fetchDolar();
bool fetchHolidays();
bool fetchNews();

}  // namespace Net
