#include "net.h"
#include "config.h"
#include "appdata.h"
#include "board.h"
#include "rtc_pcf85063.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>

extern PCF85063 g_rtc;

namespace Net {

// ============================================================================
//  Configuración persistente
// ============================================================================
static const char* PREF_NS = "epar";

void loadConfig() {
  Preferences p;
  p.begin(PREF_NS, false);            // lectura/escritura: crea el namespace la primera vez
  if (p.getUShort("cfgver", 0) != CFG_VERSION) {
    // Nueva ciudad por defecto en el firmware: pisa la guardada (el resto se conserva)
    p.putString("city", DEFAULT_CITY);
    p.putString("cname", FALLBACK_CITY_NAME);
    p.putFloat("lat", DEFAULT_LAT);
    p.putFloat("lon", DEFAULT_LON);
    p.putUShort("cfgver", CFG_VERSION);
  }
  String city  = p.getString("city", DEFAULT_CITY);
  String cname = p.getString("cname", FALLBACK_CITY_NAME);
  String news  = p.getString("news", DEFAULT_NEWS);
  g_cfg.lat         = p.getFloat("lat", DEFAULT_LAT);
  g_cfg.lon         = p.getFloat("lon", DEFAULT_LON);
  g_cfg.intervalMin = p.getUShort("interval", DEFAULT_INTERVAL_MIN);
  g_cfg.alwaysOn    = p.getBool("alwayson", false);
  g_cfg.darkMode    = p.getBool("dark", false);
  g_cfg.profile     = p.getUChar("profile", DEFAULT_PROFILE);
  g_cfg.nightMode   = p.getBool("night", true);
  g_cfg.marineLat   = p.getFloat("mlat", 0.0f);
  g_cfg.marineLon   = p.getFloat("mlon", 0.0f);
  p.end();
  if (g_cfg.profile > PWR_SAVER) g_cfg.profile = DEFAULT_PROFILE;
  strlcpy(g_cfg.city, city.c_str(), sizeof(g_cfg.city));
  strlcpy(g_cfg.cityName, cname.c_str(), sizeof(g_cfg.cityName));
  strlcpy(g_cfg.news, news.c_str(), sizeof(g_cfg.news));
  if (g_cfg.intervalMin < 5) g_cfg.intervalMin = 5;
  if (g_cfg.intervalMin > 120) g_cfg.intervalMin = 120;
  if (!findNewsSource(g_cfg.news)) strlcpy(g_cfg.news, DEFAULT_NEWS, sizeof(g_cfg.news));
}

void saveConfig() {
  Preferences p;
  p.begin(PREF_NS, false);
  p.putString("city", g_cfg.city);
  p.putString("cname", g_cfg.cityName);
  p.putString("news", g_cfg.news);
  p.putFloat("lat", g_cfg.lat);
  p.putFloat("lon", g_cfg.lon);
  p.putUShort("interval", g_cfg.intervalMin);
  p.putBool("alwayson", g_cfg.alwaysOn);
  p.putBool("dark", g_cfg.darkMode);
  p.putUChar("profile", g_cfg.profile);
  p.putBool("night", g_cfg.nightMode);
  p.putFloat("mlat", g_cfg.marineLat);
  p.putFloat("mlon", g_cfg.marineLon);
  p.putUShort("cfgver", CFG_VERSION);
  p.end();
}

// ============================================================================
//  Wi-Fi
// ============================================================================
bool hasCredentials() {
  // esp_wifi_get_config() necesita el driver inicializado: arrancar la STA primero.
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setDebugOutput(false);
  return wm.getWiFiIsSaved();
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

void updateLinkInfo() {
  if (!isConnected()) return;
  strlcpy(g_state.ssid, WiFi.SSID().c_str(), sizeof(g_state.ssid));
  strlcpy(g_state.ip, WiFi.localIP().toString().c_str(), sizeof(g_state.ip));
  g_state.rssi = (int8_t)WiFi.RSSI();
}

bool connect(uint32_t timeoutMs) {
  if (isConnected()) return true;
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_13dBm);   // el router siempre está cerca: menos corriente al transmitir
  WiFi.setAutoReconnect(false);
  WiFi.begin();                       // usa la red guardada en NVS
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    if ((millis() / 250) % 2) Board::ledOn(); else Board::ledOff();
  }
  Board::ledOff();
  if (!isConnected()) {
    Serial.printf("[net] Wi-Fi: no se pudo conectar (status=%d)\n", WiFi.status());
    return false;
  }
  updateLinkInfo();
  Serial.printf("[net] Wi-Fi OK: %s  IP %s  RSSI %d dBm\n", g_state.ssid, g_state.ip, g_state.rssi);
  return true;
}

void disconnect() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
}

bool runPortal() {
  WiFiManager wm;
  wm.setShowInfoErase(false);   // no exponer "borrar Wi-Fi" a quien entre al portal
  wm.setDebugOutput(false);
  wm.setTitle("ePaper Monitor AR");
  wm.setClass("invert");
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);
  wm.setBreakAfterConfig(true);
  wm.setShowInfoUpdate(false);

  char intervalStr[6];
  snprintf(intervalStr, sizeof(intervalStr), "%u", g_cfg.intervalMin);

  WiFiManagerParameter pHead("<h3>Monitor</h3>");
  WiFiManagerParameter pCity("city", "Ciudad para el clima (auto = ubicaci&oacute;n por la conexi&oacute;n Wi-Fi)", g_cfg.city, 39);
  WiFiManagerParameter pInterval("interval", "Minutos entre actualizaciones (5-120)", intervalStr, 4,
                                 " type='number' min='5' max='120'");
  WiFiManagerParameter pNewsList(
      "<datalist id='newsList'><option value='clarin'><option value='ambito'><option value='perfil'>"
      "<option value='bbc'><option value='lanacion'></datalist>");
  WiFiManagerParameter pNews("news", "Noticias: clarin / ambito / perfil / bbc / lanacion", g_cfg.news, 11,
                             " list='newsList'");
  WiFiManagerParameter pAlways("alwayson", "Siempre encendido (sin ahorro de energ&iacute;a, usar con USB)", "1", 2,
                               g_cfg.alwaysOn ? " type='checkbox' checked" : " type='checkbox'", WFM_LABEL_AFTER);
  WiFiManagerParameter pDark("dark", "Modo oscuro (pantalla invertida: fondo negro)", "1", 2,
                             g_cfg.darkMode ? " type='checkbox' checked" : " type='checkbox'", WFM_LABEL_AFTER);
  char profStr[2] = {(char)('0' + (g_cfg.profile <= PWR_SAVER ? g_cfg.profile : DEFAULT_PROFILE)), 0};
  WiFiManagerParameter pProfList(
      "<datalist id='profList'><option value='0' label='rendimiento'><option value='1' label='equilibrado'>"
      "<option value='2' label='ahorro'></datalist>");
  WiFiManagerParameter pProfile("profile", "Energ&iacute;a: 0 = rendimiento (reloj 1 min) / 1 = equilibrado (2 min) / 2 = ahorro (5 min)",
                                profStr, 2, " type='number' min='0' max='2' list='profList'");
  WiFiManagerParameter pNight("night", "Ahorro nocturno (00-07 h: refresca y sincroniza mucho menos)", "1", 2,
                              g_cfg.nightMode ? " type='checkbox' checked" : " type='checkbox'", WFM_LABEL_AFTER);
  wm.addParameter(&pHead);
  wm.addParameter(&pCity);
  wm.addParameter(&pInterval);
  wm.addParameter(&pNewsList);
  wm.addParameter(&pNews);
  wm.addParameter(&pProfList);
  wm.addParameter(&pProfile);
  wm.addParameter(&pNight);
  wm.addParameter(&pAlways);
  wm.addParameter(&pDark);

  std::vector<const char*> menu = {"wifi", "param", "info", "exit"};
  wm.setMenu(menu);

  Serial.printf("[net] Portal de configuración: red '%s' -> http://192.168.4.1\n", AP_NAME);
  bool connected = wm.startConfigPortal(AP_NAME, apPassword());

  // Tomar los parámetros (si el usuario no guardó, quedan los valores previos)
  String city = pCity.getValue(); city.trim();
  if (city.length() == 0) city = DEFAULT_CITY;
  bool cityChanged = (city != String(g_cfg.city));
  strlcpy(g_cfg.city, city.c_str(), sizeof(g_cfg.city));
  if (cityChanged) { g_cfg.lat = NAN; g_cfg.lon = NAN; g_cfg.cityName[0] = 0; g_state.geoEpoch = 0; }  // fuerza nueva ubicación

  int iv = atoi(pInterval.getValue());
  if (iv < 5) iv = 5;
  if (iv > 120) iv = 120;
  g_cfg.intervalMin = (uint16_t)iv;

  String news = pNews.getValue(); news.trim(); news.toLowerCase();
  strlcpy(g_cfg.news, findNewsSource(news.c_str()) ? news.c_str() : DEFAULT_NEWS, sizeof(g_cfg.news));

  g_cfg.alwaysOn = (strlen(pAlways.getValue()) > 0);
  g_cfg.darkMode = (strlen(pDark.getValue()) > 0);
  g_cfg.nightMode = (strlen(pNight.getValue()) > 0);
  int prof = atoi(pProfile.getValue());
  g_cfg.profile = (prof >= 0 && prof <= PWR_SAVER) ? (uint8_t)prof : DEFAULT_PROFILE;
  saveConfig();

  if (connected) updateLinkInfo();
  Serial.printf("[net] Portal cerrado. conectado=%d ciudad='%s' intervalo=%u noticias=%s siempreOn=%d\n",
                connected, g_cfg.city, g_cfg.intervalMin, g_cfg.news, g_cfg.alwaysOn);
  return connected;
}

// ============================================================================
//  HTTP helper
// ============================================================================
const char* USER_AGENT = "ePaperMonitorAR/" FW_VERSION " (ESP32-S3; +https://github.com/delodg/epaper-monitor-ar)";

// Bundle de CAs raíz (Mozilla) embebido por PlatformIO desde certs/x509_crt_bundle
extern const uint8_t x509_crt_bundle_start[] asm("_binary_certs_x509_crt_bundle_start");

void tlsSetup(WiFiClientSecure& c) {
#ifdef TLS_INSECURE
  c.setInsecure();                            // sólo para depurar: -DTLS_INSECURE
#else
  c.setCACertBundle(x509_crt_bundle_start);   // valida la cadena de certificados del servidor
#endif
}

// Stream que acumula el cuerpo en una String hasta un tope; al superarlo hace "short write"
// y HTTPClient aborta la descarga (respuestas gigantes no agotan la RAM).
class BoundedStringSink : public Stream {
 public:
  BoundedStringSink(String& out, size_t maxLen) : _out(out), _limit(maxLen) { _out = ""; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    if (_out.length() + n > _limit) { overflow = true; return 0; }
    return _out.concat((const char*)buf, n) ? n : 0;
  }
  int  available() override { return 0; }
  int  read() override { return -1; }
  int  peek() override { return -1; }
  void flush() override {}
  bool overflow = false;
 private:
  String& _out;
  size_t  _limit;
};

bool httpGetString(const char* url, String& out) {
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = (strncmp(url, "https://", 8) == 0);
  if (https) tlsSetup(secure);
  HTTPClient http;
  http.setReuse(false);
  http.setUserAgent(USER_AGENT);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  bool began = https ? http.begin(secure, url) : http.begin(plain, url);
  if (!began) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[http] %d en %s\n", code, url);
    http.end();
    return false;
  }
  if (http.getSize() > (int)HTTP_MAX_BODY) {
    Serial.printf("[http] respuesta demasiado grande (%d B) en %s\n", http.getSize(), url);
    http.end();
    return false;
  }
  BoundedStringSink sink(out, HTTP_MAX_BODY);
  http.writeToStream(&sink);
  http.end();
  if (sink.overflow) { Serial.printf("[http] respuesta truncada (> %u B) en %s\n", (unsigned)HTTP_MAX_BODY, url); return false; }
  return out.length() > 0;
}

static String urlEncode(const char* s) {
  String o;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') o += (char)*p;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", *p); o += b; }
  }
  return o;
}

// ============================================================================
//  Hora: NTP -> reloj del sistema -> RTC externo
// ============================================================================
bool syncTime() {
  configTzTime(TZ_ARGENTINA, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  // OJO: sntp_get_sync_status() devuelve COMPLETED una sola vez y se resetea -> leer en una variable.
  uint32_t t0 = millis();
  bool synced = false;
  while (!synced && millis() - t0 < 15000) {
    synced = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
    if (!synced) delay(50);
  }
  if (!synced) {
    Serial.println("[ntp] sin respuesta");
    return false;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year < 125) return false;          // < 2025: no es una hora real
  g_now = t;
  g_state.timeValid = true;
  bool rtcOk = g_rtc.write(t);
  Serial.printf("[ntp] %02d/%02d/%04d %02d:%02d:%02d (RTC %s)\n", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
                t.tm_hour, t.tm_min, t.tm_sec, rtcOk ? "actualizado" : "ERROR");
  return true;
}

// ============================================================================
//  Ubicación
//   - ciudad "auto": geolocalización por la IP pública de la red Wi-Fi (ip-api.com, respaldo ipwho.is)
//   - ciudad escrita en el portal: geocodificación por nombre (Open-Meteo)
// ============================================================================
bool cityIsAuto() { return strcasecmp(g_cfg.city, CITY_AUTO) == 0 || g_cfg.city[0] == 0; }

static void applyLocation(float lat, float lon, const char* name) {
  g_cfg.lat = lat;
  g_cfg.lon = lon;
  strlcpy(g_cfg.cityName, (name && name[0]) ? name : FALLBACK_CITY_NAME, sizeof(g_cfg.cityName));
  saveConfig();
}

bool geolocateByIp() {
  String body;
  JsonDocument doc;
  // 1) ip-api.com (HTTP simple, sin key)
  if (httpGetString("http://ip-api.com/json/?fields=status,city,regionName,countryCode,lat,lon&lang=es", body) &&
      !deserializeJson(doc, body) && strcmp(doc["status"] | "", "success") == 0) {
    applyLocation(doc["lat"] | DEFAULT_LAT, doc["lon"] | DEFAULT_LON, doc["city"] | "");
    g_state.geoEpoch = time(nullptr);
    Serial.printf("[geo] IP -> %s, %s (%.4f, %.4f)\n", g_cfg.cityName, doc["regionName"] | "", g_cfg.lat, g_cfg.lon);
    return true;
  }
  // 2) ipwho.is (HTTPS, sin key)
  doc.clear();
  if (httpGetString("https://ipwho.is/?fields=success,city,region,country_code,latitude,longitude", body) &&
      !deserializeJson(doc, body) && (doc["success"] | false)) {
    applyLocation(doc["latitude"] | DEFAULT_LAT, doc["longitude"] | DEFAULT_LON, doc["city"] | "");
    g_state.geoEpoch = time(nullptr);
    Serial.printf("[geo] IP (ipwho) -> %s, %s (%.4f, %.4f)\n", g_cfg.cityName, doc["region"] | "", g_cfg.lat, g_cfg.lon);
    return true;
  }
  Serial.println("[geo] geolocalización por IP falló; uso la última ubicación conocida");
  if (isnan(g_cfg.lat) || isnan(g_cfg.lon)) applyLocation(DEFAULT_LAT, DEFAULT_LON, FALLBACK_CITY_NAME);
  return false;
}

bool geocodeCity() {
  if (cityIsAuto()) return geolocateByIp();
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(g_cfg.city) +
               "&count=1&language=es&countryCode=AR";
  String body;
  if (!httpGetString(url.c_str(), body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject r = doc["results"][0];
  if (r.isNull()) {
    Serial.printf("[geo] '%s' no encontrada; uso %s\n", g_cfg.city, FALLBACK_CITY_NAME);
    applyLocation(DEFAULT_LAT, DEFAULT_LON, FALLBACK_CITY_NAME);
    return false;
  }
  applyLocation(r["latitude"] | DEFAULT_LAT, r["longitude"] | DEFAULT_LON, r["name"] | g_cfg.city);
  Serial.printf("[geo] %s -> %.4f, %.4f\n", g_cfg.cityName, g_cfg.lat, g_cfg.lon);
  return true;
}

static void ensureLocation() {
  bool noCoords = isnan(g_cfg.lat) || isnan(g_cfg.lon) || g_cfg.cityName[0] == 0;
  if (cityIsAuto()) {
    time_t now = time(nullptr);
    bool stale = (g_state.geoEpoch == 0) || (now - g_state.geoEpoch >= GEO_REFRESH_S);
    if (noCoords || stale) geolocateByIp();
  } else if (noCoords) {
    geocodeCity();
  }
}

// ============================================================================
//  Clima — Open-Meteo (sin API key)
// ============================================================================
bool fetchWeather() {
  ensureLocation();
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(g_cfg.lat, 4) +
               "&longitude=" + String(g_cfg.lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,is_day"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
               "&timezone=America%2FArgentina%2FBuenos_Aires&forecast_days=4";
  String body;
  if (!httpGetString(url.c_str(), body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;

  WeatherData w = {};
  w.temp  = cur["temperature_2m"] | 0.0f;
  w.hum   = cur["relative_humidity_2m"] | 0.0f;
  w.feels = cur["apparent_temperature"] | 0.0f;
  w.wind  = cur["wind_speed_10m"] | 0.0f;
  w.gust  = cur["wind_gusts_10m"] | 0.0f;
  w.windDir = cur["wind_direction_10m"] | 0;
  w.code  = cur["weather_code"] | 0;
  w.isDay = (cur["is_day"] | 1) != 0;
  JsonObject d = doc["daily"];
  for (int i = 0; i < 4; i++) {
    int y = 0, m = 0, dd = 0;
    const char* ds = d["time"][i] | "";
    sscanf(ds, "%d-%d-%d", &y, &m, &dd);
    w.day[i].code     = d["weather_code"][i] | 0;
    w.day[i].tmax     = (int8_t)lroundf(d["temperature_2m_max"][i] | 0.0f);
    w.day[i].tmin     = (int8_t)lroundf(d["temperature_2m_min"][i] | 0.0f);
    w.day[i].rainProb = d["precipitation_probability_max"][i] | 0;
    w.day[i].mday     = dd;
    w.day[i].wday     = (y > 0) ? dayOfWeek(y, m, dd) : 0;
  }
  w.valid = true;
  w.updated = time(nullptr);
  g_weather = w;
  Serial.printf("[clima] %s: %.1f°C hum %.0f%% cod %d\n", g_cfg.cityName, w.temp, w.hum, w.code);
  return true;
}

// ============================================================================
//  Dólar — dolarapi.com
// ============================================================================
bool fetchDolar() {
  String body;
  if (!httpGetString("https://dolarapi.com/v1/dolares", body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  struct { const char* casa; const char* nombre; } casas[] = {
    {"oficial", "Oficial"}, {"blue", "Blue"}, {"bolsa", "MEP"},
    {"contadoconliqui", "CCL"}, {"tarjeta", "Tarjeta"}, {"cripto", "Cripto"},
  };
  DolarData dd = {};
  for (auto& c : casas) {
    for (JsonObject o : doc.as<JsonArray>()) {
      const char* casa = o["casa"] | "";
      if (strcmp(casa, c.casa) == 0 && dd.n < 6) {
        strlcpy(dd.item[dd.n].nombre, c.nombre, sizeof(dd.item[dd.n].nombre));
        dd.item[dd.n].compra = o["compra"] | 0.0f;
        dd.item[dd.n].venta  = o["venta"] | 0.0f;
        dd.n++;
        break;
      }
    }
  }
  if (dd.n == 0) return false;
  dd.valid = true;
  dd.updated = time(nullptr);
  g_dolar = dd;
  Serial.printf("[dolar] %u cotizaciones, blue venta %.0f\n", dd.n, dd.n > 1 ? dd.item[1].venta : 0.0f);
  return true;
}

// ============================================================================
//  Feriados — api.argentinadatos.com
// ============================================================================
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

static void appendHolidays(int year, long today, HolidayData& hd) {
  String url = "https://api.argentinadatos.com/v1/feriados/" + String(year);
  String body;
  if (!httpGetString(url.c_str(), body)) return;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (hd.n >= 4) break;
    int y = 0, m = 0, d = 0;
    const char* f = o["fecha"] | "";
    if (sscanf(f, "%d-%d-%d", &y, &m, &d) != 3) continue;
    long diff = daysFromCivil(y, m, d) - today;
    if (diff < 0) continue;
    Holiday& h = hd.item[hd.n];
    strlcpy(h.nombre, o["nombre"] | "Feriado", sizeof(h.nombre));
    strlcpy(h.tipo, o["tipo"] | "", sizeof(h.tipo));
    h.month = m; h.mday = d; h.wday = dayOfWeek(y, m, d); h.daysLeft = (int16_t)diff;
    hd.n++;
  }
}

bool fetchHolidays() {
  if (!g_state.timeValid) return false;
  long today = daysFromCivil(g_now.tm_year + 1900, g_now.tm_mon + 1, g_now.tm_mday);
  HolidayData hd = {};
  appendHolidays(g_now.tm_year + 1900, today, hd);
  if (hd.n < 4) appendHolidays(g_now.tm_year + 1901, today, hd);
  if (hd.n == 0) return false;
  hd.valid = true;
  hd.fetchedYday = g_now.tm_yday;
  hd.updated = time(nullptr);
  g_holidays = hd;
  Serial.printf("[feriados] próximo: %s en %d días\n", hd.item[0].nombre, hd.item[0].daysLeft);
  return true;
}

// ============================================================================
//  Noticias — RSS con parser incremental (no carga el XML completo en RAM)
// ============================================================================
static void utf8SafeCopy(char* dst, size_t size, const char* src) {
  size_t n = strlen(src);
  if (n >= size) n = size - 1;
  // no cortar en el medio de un carácter multibyte
  while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = 0;
}

static void appendUtf8(String& o, uint32_t cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

static String decodeEntities(const String& in) {
  static const struct { const char* name; const char* utf8; } named[] = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
    {"ntilde", "ñ"}, {"Ntilde", "Ñ"}, {"aacute", "á"}, {"eacute", "é"}, {"iacute", "í"}, {"oacute", "ó"}, {"uacute", "ú"},
    {"Aacute", "Á"}, {"Eacute", "É"}, {"Iacute", "Í"}, {"Oacute", "Ó"}, {"Uacute", "Ú"}, {"uuml", "ü"},
    {"iexcl", "¡"}, {"iquest", "¿"}, {"laquo", "«"}, {"raquo", "»"}, {"ndash", "–"}, {"mdash", "—"},
    {"ldquo", "“"}, {"rdquo", "”"}, {"lsquo", "‘"}, {"rsquo", "’"}, {"hellip", "…"}, {"deg", "°"},
  };
  String o;
  o.reserve(in.length());
  for (unsigned i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c != '&') { o += c; continue; }
    int end = in.indexOf(';', i);
    if (end < 0 || end - i > 10) { o += c; continue; }
    String ent = in.substring(i + 1, end);
    bool ok = false;
    if (ent.length() > 1 && ent[0] == '#') {
      uint32_t cp = (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
      if (cp > 0) { appendUtf8(o, cp); ok = true; }
    } else {
      for (auto& e : named) if (ent == e.name) { o += e.utf8; ok = true; break; }
    }
    if (ok) i = end; else o += c;
  }
  return o;
}

class RssTitleSink : public Stream {
 public:
  explicit RssTitleSink(NewsData& d) : _d(d) { _d.n = 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    if (_done) return 0;                  // "short write": HTTPClient aborta la descarga
    for (size_t i = 0; i < n; i++) feed((char)buf[i]);
    _bytes += n;
    if (_d.n >= NEWS_MAX_TITLES || _bytes >= NEWS_MAX_BYTES) _done = true;
    return n;
  }
  int  available() override { return 0; }
  int  read() override { return -1; }
  int  peek() override { return -1; }
  void flush() override {}
  size_t bytes() const { return _bytes; }

 private:
  NewsData& _d;
  size_t _bytes = 0;
  bool   _done = false;
  enum Mode { TEXT, TAG, CDATA } _mode = TEXT;
  bool   _inItem = false, _capturing = false;
  int    _cdataMatch = 0;
  String _tag, _title;

  void feed(char c) {
    switch (_mode) {
      case TEXT:
        if (c == '<') { _mode = TAG; _tag = ""; }
        else if (_capturing && _title.length() < 240) _title += c;
        break;
      case TAG:
        if (c == '>') { handleTag(); _mode = TEXT; }
        else if (_tag.length() < 48) {
          _tag += c;
          if (_tag == "![CDATA[") { _mode = CDATA; _cdataMatch = 0; }
        }
        break;
      case CDATA:
        if (c == ']') { _cdataMatch++; }
        else if (c == '>' && _cdataMatch >= 2) { _mode = TEXT; _cdataMatch = 0; }
        else {
          while (_cdataMatch > 0) { if (_capturing) _title += ']'; _cdataMatch--; }
          if (_capturing && _title.length() < 240) _title += c;
        }
        break;
    }
  }

  static bool isTag(const String& t, const char* name) {
    size_t n = strlen(name);
    return t.startsWith(name) && (t.length() == n || t[n] == ' ' || t[n] == '\t' || t[n] == '\n' || t[n] == '\r');
  }

  void handleTag() {
    if (isTag(_tag, "item")) { _inItem = true; }
    else if (_tag.startsWith("/item")) { _inItem = false; _capturing = false; }
    else if (_inItem && isTag(_tag, "title")) { _capturing = true; _title = ""; }
    else if (_inItem && _tag.startsWith("/title")) { if (_capturing) finishTitle(); _capturing = false; }
  }

  void finishTitle() {
    String t = decodeEntities(_title);
    // colapsar espacios/saltos
    String s; s.reserve(t.length());
    bool sp = true;
    for (unsigned i = 0; i < t.length(); i++) {
      char c = t[i];
      if (c == '\n' || c == '\r' || c == '\t') c = ' ';
      if (c == ' ') { if (!sp) s += c; sp = true; } else { s += c; sp = false; }
    }
    s.trim();
    if (s.length() == 0 || _d.n >= NEWS_MAX_TITLES) return;
    const size_t maxLen = sizeof(_d.title[0]) - 1;
    if (s.length() > maxLen) {                       // cortar en un espacio y marcar con "..."
      int cut = s.lastIndexOf(' ', maxLen - 4);
      if (cut < (int)maxLen / 2) cut = maxLen - 4;
      s = s.substring(0, cut) + "...";
    }
    utf8SafeCopy(_d.title[_d.n], sizeof(_d.title[0]), s.c_str());
    _d.n++;
  }
};

bool fetchNews() {
  const NewsSource* src = findNewsSource(g_cfg.news);
  if (!src) src = &NEWS_SOURCES[0];

  WiFiClientSecure client;
  tlsSetup(client);
  HTTPClient http;
  http.setReuse(false);
  http.setUserAgent(USER_AGENT);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, src->url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[news] %s -> HTTP %d\n", src->name, code);
    http.end();
    return false;
  }
  NewsData nd = {};
  RssTitleSink sink(nd);
  http.writeToStream(&sink);            // el "short write" corta la lectura al tener suficientes títulos
  http.end();
  if (nd.n == 0) {
    Serial.printf("[news] %s: sin títulos (%u bytes leídos)\n", src->name, (unsigned)sink.bytes());
    return false;
  }
  strlcpy(nd.source, src->name, sizeof(nd.source));
  nd.valid = true;
  nd.updated = time(nullptr);
  g_news = nd;
  Serial.printf("[news] %s: %u títulos (%u bytes)\n", src->name, nd.n, (unsigned)sink.bytes());
  return true;
}

}  // namespace Net
