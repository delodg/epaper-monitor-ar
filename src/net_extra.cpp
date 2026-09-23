// Descargas adicionales: mar (mareas), sol y economía.
#include "net.h"
#include "config.h"
#include "appdata.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

namespace Net {

// ============================================================================
//  Utilidad: cola de un stream (para arreglos JSON enormes: sólo el último objeto)
// ============================================================================
class TailSink : public Stream {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    for (size_t i = 0; i < n; i++) {
      _buf[_pos] = (char)buf[i];
      _pos = (_pos + 1) % sizeof(_buf);
      if (_len < sizeof(_buf)) _len++;
    }
    return n;
  }
  int  available() override { return 0; }
  int  read() override { return -1; }
  int  peek() override { return -1; }
  void flush() override {}
  String tail() const {
    String o;
    o.reserve(_len);
    size_t start = (_pos + sizeof(_buf) - _len) % sizeof(_buf);
    for (size_t i = 0; i < _len; i++) o += _buf[(start + i) % sizeof(_buf)];
    return o;
  }
 private:
  char   _buf[512];
  size_t _pos = 0, _len = 0;
};

// Descarga un arreglo JSON grande (decenas de KB) y deja en `doc` sólo su último objeto.
static bool httpGetLastObject(const char* url, JsonDocument& doc) {
  WiFiClientSecure client;
  tlsSetup(client);
  HTTPClient http;
  http.setReuse(false);
  http.setUserAgent(USER_AGENT);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  TailSink sink;
  http.writeToStream(&sink);
  http.end();
  String t = sink.tail();
  int a = t.lastIndexOf('{'), b = t.lastIndexOf('}');
  if (a < 0 || b < a) return false;
  return deserializeJson(doc, t.substring(a, b + 1)) == DeserializationError::Ok;
}

// "2026-09-18T07:27" -> minutos desde medianoche (-1 si no parsea)
static int isoMinutes(const char* iso) {
  int h = 0, m = 0;
  const char* tpos = strchr(iso, 'T');
  if (!tpos || sscanf(tpos + 1, "%d:%d", &h, &m) != 2) return -1;
  return h * 60 + m;
}

// "2026-09-18T00:00" -> epoch (interpretado en la zona horaria configurada)
static time_t isoLocalEpoch(const char* iso) {
  struct tm t = {};
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3) return 0;
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = 0;
  return mktime(&t);
}

// ============================================================================
//  Mar — Open-Meteo Marine (nivel del mar incl. marea, olas, temperatura del agua).
//  El modelo sólo tiene datos en celdas marinas: se prueba la ubicación y luego
//  hacia el este (Atlántico) en pasos de 0.25°; la celda que funciona se recuerda.
// ============================================================================
static bool tryMarine(float lat, float lon, MarineData& m) {
  String url = "https://marine-api.open-meteo.com/v1/marine?latitude=" + String(lat, 4) +
               "&longitude=" + String(lon, 4) +
               "&hourly=sea_level_height_msl,wave_height,sea_surface_temperature"
               "&timezone=America%2FArgentina%2FBuenos_Aires&forecast_days=2&cell_selection=sea";
  String body;
  if (!httpGetString(url.c_str(), body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonArray times = doc["hourly"]["time"];
  JsonArray lev   = doc["hourly"]["sea_level_height_msl"];
  if (times.isNull() || lev.isNull() || lev.size() == 0 || lev[0].isNull()) return false;

  memset(&m, 0, sizeof(m));
  m.t0 = isoLocalEpoch(times[0] | "");
  int n = min((int)lev.size(), (int)MARINE_HOURS);
  for (int i = 0; i < n; i++) m.level[i] = lev[i].isNull() ? LEVEL_NONE : (int16_t)lroundf((lev[i] | 0.0f) * 100.0f);
  for (int i = n; i < MARINE_HOURS; i++) m.level[i] = LEVEL_NONE;

  // olas y agua: valor de la hora actual y máximo de las próximas 24 h
  time_t now = time(nullptr);
  int hNow = (int)((now - m.t0) / 3600);
  if (hNow < 0) hNow = 0;
  if (hNow >= n) hNow = n - 1;
  JsonArray wave = doc["hourly"]["wave_height"];
  JsonArray sst  = doc["hourly"]["sea_surface_temperature"];
  m.waveNow   = wave[hNow].isNull() ? NAN : (wave[hNow] | 0.0f);
  m.sst       = sst[hNow].isNull()  ? NAN : (sst[hNow] | 0.0f);
  m.waveMax24 = NAN;
  for (int i = hNow; i < n && i < hNow + 24; i++) {
    if (wave[i].isNull()) continue;
    float v = wave[i] | 0.0f;
    if (isnan(m.waveMax24) || v > m.waveMax24) m.waveMax24 = v;
  }

  // extremos (pleamar / bajamar) con interpolación parabólica entre horas
  m.nExt = 0;
  for (int i = 1; i < n - 1 && m.nExt < 8; i++) {
    int16_t a = m.level[i - 1], b = m.level[i], c = m.level[i + 1];
    if (a == LEVEL_NONE || b == LEVEL_NONE || c == LEVEL_NONE) continue;
    bool high = (b > a && b >= c), low = (b < a && b <= c);
    if (!high && !low) continue;
    float denom = (float)(a - 2 * b + c);
    float off = (denom != 0) ? 0.5f * (a - c) / denom : 0;      // desplazamiento en horas respecto de i
    float val = b - 0.25f * (a - c) * off;
    time_t t = m.t0 + (time_t)((i + off) * 3600.0f);
    if (t < now - 1800) continue;                              // sólo los próximos (y el que está ocurriendo)
    m.ext[m.nExt].t = t;
    m.ext[m.nExt].cm = (int16_t)lroundf(val);
    m.ext[m.nExt].high = high;
    m.nExt++;
  }
  m.cellLat = doc["latitude"] | lat;
  m.cellLon = doc["longitude"] | lon;
  m.valid = true;
  m.updated = now;
  return true;
}

bool fetchMarine() {
  if (isnan(g_cfg.lat) || isnan(g_cfg.lon)) return false;
  MarineData m;
  // Primero la celda que ya funcionó (sondear cuesta hasta 5 peticiones HTTPS ~12 s): se
  // recuerda en RTC-RAM y también en NVS, así sobrevive a un corte de energía.
  if (g_marine.valid && tryMarine(g_marine.cellLat, g_marine.cellLon, m)) { g_marine = m; return true; }
  if (!isnan(g_cfg.marineLat) && g_cfg.marineLat != 0 && tryMarine(g_cfg.marineLat, g_cfg.marineLon, m)) {
    g_marine = m;
    Serial.printf("[mar] celda recordada %.3f,%.3f\n", m.cellLat, m.cellLon);
    return true;
  }
  MarineData first;
  bool haveFirst = false;
  for (int i = 0; i < GEO_TRIES_EAST; i++) {
    if (!tryMarine(g_cfg.lat, g_cfg.lon + 0.25f * i, m)) continue;
    if (!isnan(m.waveNow)) { first = m; haveFirst = true; break; }
    if (!haveFirst) { first = m; haveFirst = true; continue; }   // sin olas: probar una celda más
    break;
  }
  if (haveFirst) {
    g_marine = first;
    g_cfg.marineLat = g_marine.cellLat;      // recordar la celda para no volver a sondear
    g_cfg.marineLon = g_marine.cellLon;
    saveConfig();
    Serial.printf("[mar] celda %.3f,%.3f: %u extremos, ola %.1f m, agua %.1f°C\n",
                  g_marine.cellLat, g_marine.cellLon, g_marine.nExt, g_marine.waveNow, g_marine.sst);
    return true;
  }
  Serial.println("[mar] sin datos marinos cerca (¿ubicación lejos de la costa?)");
  return false;
}

// ============================================================================
//  Sol — Open-Meteo: amanecer, atardecer, duración del día, UV máximo (hoy y mañana)
// ============================================================================
bool fetchSun() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(g_cfg.lat, 4) +
               "&longitude=" + String(g_cfg.lon, 4) +
               "&daily=sunrise,sunset,daylight_duration,uv_index_max"
               "&timezone=America%2FArgentina%2FBuenos_Aires&forecast_days=2";
  String body;
  if (!httpGetString(url.c_str(), body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject d = doc["daily"];
  if (d.isNull()) return false;
  SunData sd = {};
  sd.sunrise   = isoMinutes(d["sunrise"][0] | "");
  sd.sunset    = isoMinutes(d["sunset"][0] | "");
  sd.sunriseT  = isoMinutes(d["sunrise"][1] | "");
  sd.sunsetT   = isoMinutes(d["sunset"][1] | "");
  sd.daylight  = (int32_t)(d["daylight_duration"][0] | 0.0f);
  sd.daylightT = (int32_t)(d["daylight_duration"][1] | 0.0f);
  sd.uvMax     = d["uv_index_max"][0] | 0.0f;
  sd.mday      = g_now.tm_mday;
  sd.valid     = (sd.sunrise >= 0 && sd.sunset >= 0);
  sd.updated   = time(nullptr);
  g_sun = sd;
  Serial.printf("[sol] amanece %02d:%02d, atardece %02d:%02d, UV máx %.1f\n",
                sd.sunrise / 60, sd.sunrise % 60, sd.sunset / 60, sd.sunset % 60, sd.uvMax);
  return sd.valid;
}

// ============================================================================
//  Economía — ArgentinaDatos (riesgo país, inflación), DolarApi (euro/real), CoinGecko (BTC/ETH)
// ============================================================================
static const char* MESES_ABR[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};

bool fetchEcon() {
  EconData e = g_econ;          // si una parte falla se conserva el valor anterior
  bool any = false;
  String body;
  JsonDocument doc;
  time_t now = time(nullptr);
  auto stale = [&](time_t updated, long maxAge) { return updated == 0 || now - updated >= maxAge - 30; };

  // Riesgo país: se publica un valor por día
  if (stale(e.riesgoUpdated, RIESGO_MAX_AGE_S)) {
    if (httpGetString("https://api.argentinadatos.com/v1/finanzas/indices/riesgo-pais/ultimo", body) &&
        !deserializeJson(doc, body)) {
      e.riesgoPais = doc["valor"] | 0;
      int y = 0, mo = 0, d = 0;
      if (sscanf(doc["fecha"] | "", "%d-%d-%d", &y, &mo, &d) == 3)
        snprintf(e.riesgoFecha, sizeof(e.riesgoFecha), "%02d/%02d", d, mo);
      e.riesgoUpdated = now;
      any = true;
    }
    doc.clear();
  }

  // Inflación: dato mensual y son dos descargas de ~50 KB -> una vez por día alcanza
  if (stale(e.inflUpdated, INFLATION_MAX_AGE_S)) {
    bool ok = false;
    if (httpGetLastObject("https://api.argentinadatos.com/v1/finanzas/indices/inflacion", doc)) {
      e.inflMensual = doc["valor"] | 0.0f;
      int y = 0, mo = 0, d = 0;
      if (sscanf(doc["fecha"] | "", "%d-%d-%d", &y, &mo, &d) == 3 && mo >= 1 && mo <= 12)
        strlcpy(e.inflMes, MESES_ABR[mo - 1], sizeof(e.inflMes));
      ok = any = true;
    }
    doc.clear();
    if (httpGetLastObject("https://api.argentinadatos.com/v1/finanzas/indices/inflacionInteranual", doc)) {
      e.inflInteranual = doc["valor"] | 0.0f;
      ok = any = true;
    }
    doc.clear();
    if (ok) e.inflUpdated = now;
  }
  if (httpGetString("https://dolarapi.com/v1/cotizaciones", body) && !deserializeJson(doc, body)) {
    for (JsonObject o : doc.as<JsonArray>()) {
      const char* mon = o["moneda"] | "";
      if (strcmp(mon, "EUR") == 0) { e.euroCompra = o["compra"] | 0.0f; e.euroVenta = o["venta"] | 0.0f; any = true; }
      if (strcmp(mon, "BRL") == 0) { e.realCompra = o["compra"] | 0.0f; e.realVenta = o["venta"] | 0.0f; any = true; }
    }
  }
  doc.clear();
  if (httpGetString("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum&vs_currencies=usd", body) &&
      !deserializeJson(doc, body)) {
    e.btcUsd = doc["bitcoin"]["usd"] | 0.0f;
    e.ethUsd = doc["ethereum"]["usd"] | 0.0f;
    any = true;
  }
  if (!any) return false;
  e.valid = true;
  e.updated = now;
  g_econ = e;
  Serial.printf("[econ] riesgo país %d, inflación %s %.1f%% (i.a. %.1f%%), euro %.0f, BTC %.0f\n",
                e.riesgoPais, e.inflMes, e.inflMensual, e.inflInteranual, e.euroVenta, e.btcUsd);
  return true;
}

}  // namespace Net
