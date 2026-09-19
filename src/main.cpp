// ============================================================================
//  ePaper Monitor AR
//  Monitor argentino para Waveshare ESP32-S3-ePaper-1.54 (V2):
//  hora NTP (Argentina), clima, dólar, noticias, feriados, sensores internos.
//
//  Navegación (estilo M5StickC):  BOOT = siguiente sección · PWR = anterior
//    BOOT 2 s: portal Wi-Fi  ·  PWR 2 s: sincronizar ahora  ·  PWR 6 s: apagar
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <esp_system.h>

#include "config.h"
#include "appdata.h"
#include "board.h"
#include "rtc_pcf85063.h"
#include "shtc3.h"
#include "net.h"
#include "ui.h"

// ---------------------------------------------------------------------------
//  Globales (las marcadas RTC_DATA_ATTR sobreviven al deep-sleep)
// ---------------------------------------------------------------------------
Config                   g_cfg;
RTC_DATA_ATTR AppState   g_state;
RTC_DATA_ATTR WeatherData g_weather;
RTC_DATA_ATTR DolarData  g_dolar;
RTC_DATA_ATTR NewsData   g_news;
RTC_DATA_ATTR HolidayData g_holidays;
RTC_DATA_ATTR MarineData g_marine;
RTC_DATA_ATTR SunData    g_sun;
RTC_DATA_ATTR EconData   g_econ;
RTC_DATA_ATTR IndoorHistory g_hist;
IndoorData               g_indoor;
int                      g_batteryMv = 0;
struct tm                g_now;
bool                     g_alwaysOn = false;
bool                     g_usbHost  = false;
PCF85063                 g_rtc(Wire);
SHTC3                    g_shtc3(Wire);

static bool s_coldBoot  = false;
static bool s_forceFull = false;
static int  s_lastMinute = -1;

// Pila del loop más holgada (parsers JSON/TLS anidados).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Reinicios por error consecutivos (sobrevive a resets, no a un apagado): si un dato hostil
// hiciera crashear un parser, no se reintenta en bucle sino con espera.
RTC_NOINIT_ATTR uint32_t g_crashCount;

// Datos que sobrevivieron al deep-sleep: acotar por si la RTC-RAM se corrompió.
static void sanitizeRtcData() {
  if (g_state.page >= PAGE_COUNT) g_state.page = 0;
  if (g_news.n > NEWS_MAX_TITLES) g_news.n = 0;
  if (g_dolar.n > 6) g_dolar.n = 0;
  if (g_holidays.n > 4) g_holidays.n = 0;
  if (g_marine.nExt > 8) g_marine.nExt = 0;
  if (g_hist.count > INDOOR_SAMPLES) g_hist.count = 0;
  if (g_hist.head >= INDOOR_SAMPLES) g_hist.head = 0;
  g_state.ssid[sizeof(g_state.ssid) - 1] = 0;
  g_state.ip[sizeof(g_state.ip) - 1] = 0;
  g_news.source[sizeof(g_news.source) - 1] = 0;
  for (int i = 0; i < NEWS_MAX_TITLES; i++) g_news.title[i][sizeof(g_news.title[0]) - 1] = 0;
  for (int i = 0; i < 4; i++) { g_holidays.item[i].nombre[sizeof(g_holidays.item[0].nombre) - 1] = 0; g_holidays.item[i].tipo[sizeof(g_holidays.item[0].tipo) - 1] = 0; }
  for (int i = 0; i < 6; i++) g_dolar.item[i].nombre[sizeof(g_dolar.item[0].nombre) - 1] = 0;
  g_econ.riesgoFecha[sizeof(g_econ.riesgoFecha) - 1] = 0;
  g_econ.inflMes[sizeof(g_econ.inflMes) - 1] = 0;
}

// Año de compilación (de __DATE__ = "Sep 18 2026"): la hora del RTC se considera real
// sólo si cae entre ese año y 15 años después (los RTC vírgenes traen fechas como 2056).
static constexpr int BUILD_YEAR = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                                  (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
static bool plausible(const struct tm& t) {
  int y = t.tm_year + 1900;
  return y >= BUILD_YEAR && y <= BUILD_YEAR + 15;
}

// ---------------------------------------------------------------------------
//  Hora y sensores
// ---------------------------------------------------------------------------
static void refreshNow() {
  struct tm t;
  if (g_rtc.read(t) && plausible(t)) {
    g_now = t;
    g_state.timeValid = true;
  } else if (g_state.timeValid) {                   // RTC caído pero el reloj del sistema sigue vivo
    time_t n = time(nullptr);
    localtime_r(&n, &g_now);
  }
}

static void readSensorsAtBoot() {
  struct tm t;
  if (g_rtc.read(t) && plausible(t)) {
    g_now = t;
    g_state.timeValid = true;
    time_t epoch = mktime(&t);                      // TZ ya configurada -> epoch UTC correcto
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  } else {
    memset(&g_now, 0, sizeof(g_now));
    g_state.timeValid = false;
  }
  g_indoor.valid = g_shtc3.read(g_indoor.temp, g_indoor.hum);
  g_batteryMv = Board::batteryMilliVolts();
}

// Historial interior: una muestra en cada múltiplo de 15 min (sirve en ambos modos)
static void sampleIndoorIfDue() {
  if (!g_indoor.valid || !g_state.timeValid || (g_now.tm_min % 15) != 0) return;
  int32_t slot = (int32_t)g_now.tm_yday * 96 + g_now.tm_hour * 4 + g_now.tm_min / 15;
  if (slot == g_hist.lastSlot) return;
  g_hist.temp[g_hist.head] = (int16_t)lroundf(g_indoor.temp * 10.0f);
  g_hist.hum[g_hist.head]  = (uint8_t)lroundf(g_indoor.hum);
  g_hist.head = (g_hist.head + 1) % INDOOR_SAMPLES;
  if (g_hist.count < INDOOR_SAMPLES) g_hist.count++;
  g_hist.lastSlot = slot;
}

// ---------------------------------------------------------------------------
//  Sincronización de datos por Wi-Fi
// ---------------------------------------------------------------------------
static bool doSync() {
  g_state.lastAttempt = time(nullptr);
  if (!Net::hasCredentials()) {
    g_state.lastError = ERR_NO_CREDENTIALS;
    g_state.wifiOk = false;
    Net::disconnect();
    return false;
  }
  Board::ledOn();
  if (!Net::connect(WIFI_CONNECT_TIMEOUT_MS)) {
    g_state.lastError = ERR_WIFI_FAILED;
    g_state.wifiOk = false;
    Board::ledOff();
    Net::disconnect();
    return false;
  }
  g_state.wifiOk = true;
  g_state.lastError = ERR_NONE;

  bool timeOk = Net::syncTime();
  if (!timeOk && !g_state.timeValid) g_state.lastError = ERR_NTP_FAILED;

  // Cada dato tiene su cadencia: lo que cambia lento se baja menos seguido (batería).
  time_t now = time(nullptr);
  auto stale = [&](bool valid, time_t updated, long maxAge) { return !valid || updated == 0 || now - updated >= maxAge - 30; };
  if (stale(g_weather.valid, g_weather.updated, WEATHER_MAX_AGE_S)) Net::fetchWeather();
  if (stale(g_marine.valid, g_marine.updated, MARINE_MAX_AGE_S)) Net::fetchMarine();
  if (g_state.timeValid && (!g_sun.valid || g_sun.mday != g_now.tm_mday)) Net::fetchSun();
  Net::fetchDolar();
  if (stale(g_econ.valid, g_econ.updated, ECON_MAX_AGE_S)) Net::fetchEcon();
  if (g_state.timeValid && (!g_holidays.valid || g_holidays.fetchedYday != g_now.tm_yday)) Net::fetchHolidays();
  Net::fetchNews();

  g_state.lastSync = time(nullptr);
  Net::updateLinkInfo();
  Net::disconnect();                 // la radio apagada entre syncs: menos consumo y menos calor en el SHTC3
  Board::ledOff();
  return true;
}

static bool syncDue() {
  time_t now = time(nullptr);
  bool due     = (g_state.lastSync == 0) || (now - g_state.lastSync >= (time_t)g_cfg.intervalMin * 60 - 5);
  bool canTry  = (g_state.lastAttempt == 0) || (now - g_state.lastAttempt >= 300);   // reintentos cada 5 min
  return (due || !g_state.timeValid) && canTry;
}

static void portalFlow() {
  UI::renderPortal();
  bool connected = Net::runPortal();
  if (connected) {
    g_state.wifiOk = true;
    g_state.lastError = ERR_NONE;
    doSync();
  } else {
    g_state.wifiOk = false;
    g_state.lastError = Net::hasCredentials() ? ERR_WIFI_FAILED : ERR_NO_CREDENTIALS;
    Net::disconnect();
  }
  g_state.page = connected ? PAGE_CLOCK : PAGE_WIFI;
  s_forceFull = true;
}

static void gotoPage(int delta) {
  g_state.page = (uint8_t)((g_state.page + delta + PAGE_COUNT) % PAGE_COUNT);
  s_forceFull = true;
}

static void renderCurrent() {
  refreshNow();
  time_t now = time(nullptr);
  bool full = s_forceFull || s_coldBoot || g_state.lastFullRefresh == 0 ||
              (now - g_state.lastFullRefresh >= (time_t)FULL_REFRESH_EVERY_MIN * 60);
  UI::render(g_state.page, full);
  if (full) g_state.lastFullRefresh = now;
  s_forceFull = false;
  s_coldBoot = false;
  s_lastMinute = g_now.tm_min;
}

static void doPowerOff() {
  Serial.println("[main] apagando (PWR 6 s)");
  g_state.poweredOff = true;
  UI::renderMessage("Apagado", "Mantené PWR para", "volver a encender", true);
  UI::hibernate();
  Board::powerOff();
}

static uint64_t sleepMicros() {
  refreshNow();
  int wait = g_state.timeValid ? (60 - g_now.tm_sec) : 60;   // segundos hasta el próximo :00
  if (wait < 1) wait = 1;
  return (uint64_t)wait * 1000000ULL - 250000ULL;     // despierta un poco antes (el arranque tarda ~0,3 s)
}

// ---------------------------------------------------------------------------
//  Arranque
// ---------------------------------------------------------------------------
void setup() {
  Board::earlyInit();
  Serial.begin(115200);

  Board::WakeReason wake = Board::wakeReason();
  s_coldBoot = (wake == Board::WAKE_COLD || wake == Board::WAKE_OTHER) || g_state.magic != STATE_MAGIC;
  if (s_coldBoot) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.magic = STATE_MAGIC;
    memset(&g_weather, 0, sizeof(g_weather));
    memset(&g_dolar, 0, sizeof(g_dolar));
    memset(&g_news, 0, sizeof(g_news));
    memset(&g_holidays, 0, sizeof(g_holidays));
    memset(&g_marine, 0, sizeof(g_marine));
    memset(&g_sun, 0, sizeof(g_sun));
    memset(&g_econ, 0, sizeof(g_econ));
    memset(&g_hist, 0, sizeof(g_hist));
    g_hist.lastSlot = -1;
  }
  g_state.bootCount++;
  if (!s_coldBoot) sanitizeRtcData();

  // Contador de reinicios por error (PANIC / watchdogs)
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT || g_crashCount > 1000) g_crashCount = 0;
  if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) g_crashCount++;
  else if (rr != ESP_RST_DEEPSLEEP) g_crashCount = 0;

  setenv("TZ", TZ_ARGENTINA, 1);
  tzset();
  Net::loadConfig();

  // Con una PC conectada por USB no conviene el deep-sleep (el puerto COM desaparecería
  // cada minuto): en ese caso queda "siempre encendido" automáticamente.
  g_usbHost = Board::usbHostConnected() && Board::usbHostConnected();   // dos lecturas seguidas para evitar falsos positivos
  g_alwaysOn = g_cfg.alwaysOn || g_usbHost;

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  bool rtcOk = g_rtc.begin();
  bool shtOk = g_shtc3.begin();
  readSensorsAtBoot();
  sampleIndoorIfDue();

  static const char* RST[] = {"desconocido", "power-on", "externo", "software", "PANIC", "int-WDT", "task-WDT", "otro-WDT", "deep-sleep", "brownout", "SDIO"};
  Serial.printf("\n[main] ePaper Monitor AR v%s | boot #%lu | reset=%s wake=%d cold=%d | RTC %s | SHTC3 %s | bat %d mV | USB host %s | modo %s\n",
                FW_VERSION, (unsigned long)g_state.bootCount, ((int)rr >= 0 && (int)rr <= 10) ? RST[(int)rr] : "?", (int)wake, s_coldBoot,
                rtcOk ? "ok" : "NO", shtOk ? "ok" : "NO", g_batteryMv, g_usbHost ? "sí" : "no", g_alwaysOn ? "siempre-on" : "deep-sleep");
  if (g_crashCount >= CRASH_BACKOFF_COUNT) {
    Serial.printf("[main] %lu reinicios por error seguidos: pospongo la sincronización\n", (unsigned long)g_crashCount);
    g_state.lastAttempt = time(nullptr);    // syncDue() espera 5 min desde el último intento
    g_crashCount = 0;
  }
  if (g_state.timeValid)
    Serial.printf("[main] hora RTC: %02d/%02d/%04d %02d:%02d:%02d\n", g_now.tm_mday, g_now.tm_mon + 1,
                  g_now.tm_year + 1900, g_now.tm_hour, g_now.tm_min, g_now.tm_sec);

  // ---- Botones (la pulsación que nos despertó) ----
  bool forceSync = false, openPortal = false, powerOff = false;
  if (g_state.poweredOff) {                       // venimos de "apagar": encender sin cambiar de sección
    g_state.poweredOff = false;
    s_forceFull = true;
    wake = Board::WAKE_OTHER;
    Serial.println("[main] encendido");
  }
  if (wake == Board::WAKE_BTN_BOOT) {
    uint32_t held = Board::measureHold(PIN_BTN_BOOT, LONG_PRESS_MS + 100);
    if (held >= LONG_PRESS_MS) openPortal = true; else gotoPage(+1);
  } else if (wake == Board::WAKE_BTN_PWR) {
    uint32_t held = Board::measureHold(PIN_BTN_PWR, POWEROFF_PRESS_MS + 100);
    if (held >= POWEROFF_PRESS_MS) powerOff = true;
    else if (held >= LONG_PRESS_MS) forceSync = true;
    else gotoPage(-1);
  }

  UI::begin(s_coldBoot);
  if (powerOff) doPowerOff();

  if (s_coldBoot) UI::renderSplash(Net::hasCredentials() ? "Conectando a Wi-Fi..." : "Wi-Fi sin configurar");

  if (openPortal || (s_coldBoot && !Net::hasCredentials())) {
    portalFlow();
  } else if (forceSync || syncDue()) {
    doSync();
  }

  renderCurrent();
  UI::hibernate();

  // Tras un deep-sleep el USB se re-enumera y la PC tarda ~1-2 s en volver a mandar SOF:
  // re-chequear al final del ciclo (ya pasaron sensores + refresco) antes de decidir dormir.
  if (!g_alwaysOn && Board::usbHostConnected()) {
    g_usbHost = true;
    g_alwaysOn = true;
    Serial.println("[main] host USB detectado al final del ciclo -> siempre encendido");
  }
  if (!g_alwaysOn) Board::deepSleep(sleepMicros());
  // Nota: no usar setCpuFrequencyMhz(): en el S3 re-enumera el USB y rompe la detección de host.
  Serial.println("[main] modo siempre encendido");
}

// ---------------------------------------------------------------------------
//  Modo "siempre encendido" (sin deep-sleep): botones por polling
// ---------------------------------------------------------------------------
struct Button {
  uint8_t  pin;
  bool     wasDown = false;
  uint32_t t0 = 0;
  bool     ledLong = false, ledVery = false;
  explicit Button(uint8_t p) : pin(p) {}
  // Devuelve al soltar: 1 = corta, 2 = larga (>= LONG_PRESS_MS), 3 = muy larga (>= POWEROFF_PRESS_MS)
  int poll() {
    bool down = digitalRead(pin) == LOW;
    uint32_t now = millis();
    int ev = 0;
    if (down && !wasDown) { t0 = now; ledLong = ledVery = false; }
    if (down && wasDown) {
      uint32_t held = now - t0;
      if (!ledLong && held >= LONG_PRESS_MS) { Board::ledOn(); ledLong = true; }
      if (!ledVery && held >= POWEROFF_PRESS_MS) { Board::ledBlink(3, 40, 40); ledVery = true; }
    }
    if (!down && wasDown) {
      uint32_t held = now - t0;
      Board::ledOff();
      if (held >= POWEROFF_PRESS_MS) ev = 3;
      else if (held >= LONG_PRESS_MS) ev = 2;
      else if (held >= 30) ev = 1;
    }
    wasDown = down;
    return ev;
  }
};

void loop() {
  static Button bBoot(PIN_BTN_BOOT), bPwr(PIN_BTN_PWR);
  static uint32_t lastTick = 0;
  bool changed = false;

  // Comandos por USB (útiles para desarrollo): n/p sección, s sync, f refresco completo,
  // d volcar pantalla actual, a volcar todas las secciones, w portal Wi-Fi
  while (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'n': gotoPage(+1); changed = true; break;
      case 'p': gotoPage(-1); changed = true; break;
      case 's': doSync(); changed = true; break;
      case 'f': s_forceFull = true; changed = true; break;
      case 'd': UI::dumpBuffer(g_state.page); break;
      case 'a': UI::dumpAllPages(g_state.page); break;
      case 'w': portalFlow(); changed = true; break;
      case 'S': UI::renderSplash("Conectando a Wi-Fi..."); s_forceFull = true; break;   // ver la portada
      case 't':   // alternar tema claro/oscuro (queda guardado)
        g_cfg.darkMode = !g_cfg.darkMode;
        Net::saveConfig();
        Serial.printf("[main] tema %s\n", g_cfg.darkMode ? "oscuro" : "claro");
        s_forceFull = true; changed = true;
        break;
      case 'h': {   // demo: llena el historial interior con datos sintéticos (para probar la UI)
        g_hist.count = INDOOR_SAMPLES; g_hist.head = 0;
        for (int i = 0; i < INDOOR_SAMPLES; i++) {
          g_hist.temp[i] = (int16_t)(220 + 40 * sinf(i * 0.13f) + (i % 7));
          g_hist.hum[i]  = (uint8_t)(45 + 12 * cosf(i * 0.09f));
        }
        changed = true;
        break;
      }
      default: break;
    }
  }

  int eb = bBoot.poll();
  if (eb == 1) { gotoPage(+1); changed = true; }
  else if (eb >= 2) { portalFlow(); changed = true; }

  int ep = bPwr.poll();
  if (ep == 1) { gotoPage(-1); changed = true; }
  else if (ep == 2) { doSync(); changed = true; }
  else if (ep == 3) { doPowerOff(); }

  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    refreshNow();
    if (g_now.tm_min != s_lastMinute) {
      g_indoor.valid = g_shtc3.read(g_indoor.temp, g_indoor.hum);
      g_batteryMv = Board::batteryMilliVolts();
      sampleIndoorIfDue();
      changed = true;
      Serial.printf("[main] %02d:%02d heap %u KB (mín %u KB) pila libre %u B\n", g_now.tm_hour, g_now.tm_min,
                    (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
                    (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    if (syncDue()) { doSync(); changed = true; }
    // Si estábamos "siempre encendidos" sólo por el USB y lo desconectaron (10 s seguidos
    // sin paquetes SOF): pasar a bajo consumo
    static uint8_t noHostSecs = 0;
    if (!g_cfg.alwaysOn && g_usbHost) {
      noHostSecs = Board::usbHostConnected() ? 0 : noHostSecs + 1;
      if (noHostSecs >= 10) {
        Serial.println("[main] USB desconectado -> deep-sleep");
        g_usbHost = false;
        g_alwaysOn = false;
        Net::disconnect();
        UI::hibernate();
        Board::deepSleep(sleepMicros());
      }
    }
  }

  if (changed) {
    renderCurrent();
    UI::hibernate();
  }
  delay(20);
}
