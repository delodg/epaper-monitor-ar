# Changelog

## 1.3.0 — 2026-09-23

Optimización de consumo a partir de mediciones en la placa (ver `docs/energia.md`).

- **Perfiles de energía** (rendimiento / equilibrado / ahorro) y **ahorro nocturno**
  configurables en el portal; paso automático a ahorro con la batería ≤ 15 %.
- `planSync()`: si no hay nada que bajar, **no se enciende la radio** (0,31 s contra 5,6-7,1 s).
- Economía: cada campo con su propia antigüedad (la inflación es mensual y costaba dos
  descargas de ~50 KB por hora): 14,3 s → ~4 s.
- Mar: la celda marina que funciona se recuerda en NVS: 12,4 s → 2,7 s.
- NTP una vez por día (el RTC deriva 1-2 s/día); dólar sólo en horario de mercado.
- Potencia de transmisión Wi-Fi a 13 dBm.
- Pantalla Sistema: perfil, cadencia y autonomía estimada con los tiempos reales medidos.
- Herramientas de medición: comandos `B`/`P`/`T` y `tools/measure_power.py`.

## 1.2.0 — 2026-09-19

- **Seguridad**: HTTPS con validación de certificados (bundle de CAs de Mozilla embebido),
  clave del portal única por placa (derivada del MAC), descargas acotadas a 64 KB, pila del
  loop de 16 KB, backoff tras reinicios por error, saneo de RTC-RAM, binario sin rutas
  personales. `SECURITY.md` con modelo de amenazas.

- **Tema claro / oscuro** seleccionable (portal o comando `t`), con inversión en el buffer;
  la luna conserva colores físicos.
- Portada rediseñada (tipográfica, logo DELO chico como firma).
- Hero del README en claro y oscuro (`tools/make_hero.py`) y capturas en modo oscuro.

## 1.1.0 — 2026-09-18

- Portada tipográfica moderna: nombre de la app, overline "Argentina", tagline, estado y el logo DELO chico como firma (bitmap desde Figma); comando `S`.

- Nuevas secciones: **Mar** (curva de marea 24 h, pleamares/bajamares, olas, agua, viento),
  **Sol y Luna** (amanecer/atardecer, duración del día, UV, fase lunar hemisferio sur),
  **Economía** (riesgo país, inflación, euro/real, BTC/ETH) e **Interior** (historial 24 h).
- Cadencia de descarga por dato (clima 30 min, mar 3 h, economía 1 h, sol/feriados diario).
- Portal Wi-Fi con clave (`epaper-ar`), incluida en el QR.
- Detección de host USB robusta tras deep-sleep; sin escalado de CPU (re-enumeraba el USB).
- "Apagar" recuerda el estado: el próximo PWR sólo enciende.
- Corrección del cálculo de sueño (no saltea minutos); viento con dirección en el reloj.
- `tools/flash_catch.py` para grabar placas dormidas; comando `h` de historial demo.

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
