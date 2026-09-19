#pragma once
// ============================================================================
//  ePaper Monitor AR — configuración de hardware y constantes
//  Placa: Waveshare ESP32-S3-ePaper-1.54 (V2) — ESP32-S3-PICO-1-N8R8
//  Pantalla: 1.54" 200x200 B/N (GDEH0154D67 / SSD1681)
// ============================================================================

#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif

// ---- Pantalla e-paper (SPI2) ----
#define PIN_EPD_DC     10
#define PIN_EPD_CS     11
#define PIN_EPD_SCK    12
#define PIN_EPD_MOSI   13
#define PIN_EPD_RST     9
#define PIN_EPD_BUSY    8
#define EPD_SPI_HZ     4000000

// ---- Rails de energía ----
#define PIN_EPD_PWR     6   // activo en BAJO  (LOW = pantalla encendida)
#define PIN_AUDIO_PWR  42   // activo en BAJO  (codec ES8311, no se usa)
#define PIN_VBAT_EN    17   // ALTO = rail de batería encendido (latch de encendido)

// ---- LED / botones / RTC ----
#define PIN_LED         3   // activo en BAJO
#define PIN_BTN_BOOT    0   // botón BOOT (activo en bajo)  -> sección siguiente
#define PIN_BTN_PWR    18   // botón PWR  (activo en bajo)  -> sección anterior
#define PIN_RTC_INT     5   // INT del PCF85063 (no se usa)

// ---- I2C (RTC PCF85063 @0x51, sensor SHTC3 @0x70) ----
#define PIN_I2C_SDA    47
#define PIN_I2C_SCL    48

// ---- Batería: ADC1_CH3 = GPIO4, divisor 200k/200k (x2) ----
#define PIN_BAT_ADC     4

// ---- Zona horaria Argentina (UTC-3, sin horario de verano) ----
#define TZ_ARGENTINA   "<-03>3"
#define NTP_SERVER_1   "ar.pool.ntp.org"
#define NTP_SERVER_2   "south-america.pool.ntp.org"
#define NTP_SERVER_3   "pool.ntp.org"

// ---- Portal de configuración Wi-Fi ----
#define AP_NAME            "ePaperAR-Setup"
#define AP_PASSWORD        "epaper-ar"       // >= 8 caracteres; se muestra en pantalla y va en el QR
#define PORTAL_TIMEOUT_S   300

// ---- Valores por defecto (configurables desde el portal) ----
// Ciudad "auto" = geolocalización por la conexión (IP pública de la red Wi-Fi), a nivel ciudad.
// Si en el portal se escribe una ciudad, se usa esa (geocodificada por nombre).
// Al cambiar estos valores subir CFG_VERSION: la placa descarta la ciudad guardada y adopta la nueva.
#define CFG_VERSION          3
#define CITY_AUTO            "auto"
#define DEFAULT_CITY         CITY_AUTO
#define FALLBACK_CITY_NAME   "Comodoro Rivadavia"   // si la geolocalización falla
#define DEFAULT_LAT          -45.8626f
#define DEFAULT_LON          -67.4940f
#define GEO_REFRESH_S        (6 * 3600)             // re-geolocalizar cada 6 h (y en cada arranque en frío)
#define DEFAULT_INTERVAL_MIN 15
#define DEFAULT_NEWS         "clarin"

// ---- Cadencia de descarga por dato (además del intervalo general del portal) ----
#define WEATHER_MAX_AGE_S   (30 * 60)     // clima: cada 30 min
#define MARINE_MAX_AGE_S    (3 * 3600)    // mar (mareas/olas): cada 3 h
#define ECON_MAX_AGE_S      (60 * 60)     // economía: cada hora
#define GEO_TRIES_EAST      5             // celdas marinas a probar hacia el este (0, +0.25°, ... +1.0°)

// ---- Tiempos ----
#define LONG_PRESS_MS            2000   // BOOT largo = portal Wi-Fi, PWR largo = sincronizar
#define POWEROFF_PRESS_MS        6000   // PWR muy largo = apagar
#define FULL_REFRESH_EVERY_MIN   60     // refresco completo periódico (evita ghosting)
#define WIFI_CONNECT_TIMEOUT_MS  20000
#define HTTP_TIMEOUT_MS          12000
#define NEWS_MAX_TITLES          6
#define NEWS_MAX_BYTES           150000 // tope de lectura del RSS

// ---- Secciones (orden de navegación) ----
enum Page : uint8_t {
  PAGE_CLOCK = 0,
  PAGE_WEATHER,
  PAGE_MARINE,
  PAGE_SUN,
  PAGE_DOLAR,
  PAGE_ECON,
  PAGE_NEWS,
  PAGE_HOLIDAYS,
  PAGE_INDOOR,
  PAGE_WIFI,
  PAGE_SYSTEM,
  PAGE_COUNT
};
