# Changelog

## 1.0.0 — 2026-09-18

Primera versión.

- 7 secciones: Reloj, Clima, Dólar, Noticias, Feriados, Wi-Fi, Sistema.
- Hora NTP (Argentina, UTC-3) guardada en el RTC PCF85063.
- Ubicación automática por la IP de la red Wi-Fi (ip-api.com / ipwho.is) o ciudad fija.
- Clima y pronóstico de 3 días (Open-Meteo), dólar (DolarApi), feriados (ArgentinaDatos),
  noticias por RSS (Clarín, Ámbito, Perfil, BBC Mundo, La Nación).
- Sensor interior SHTC3 y batería.
- Portal Wi-Fi con QR (WiFiManager) y parámetros de configuración.
- Navegación con BOOT/PWR (corto, largo y muy largo), estilo M5StickC.
- Deep-sleep con despertar por minuto/botones; modo siempre encendido automático con PC por USB.
- Volcado del framebuffer por serie y herramienta `tools/fbdump.py`.
