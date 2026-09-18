# Desarrollo

## Entorno

- [PlatformIO](https://platformio.org/) (CLI o extensión de VS Code).
- Plataforma `espressif32@6.13.0` → Arduino core **2.0.17** (ESP-IDF 4.4). Se instala sola
  al compilar. El proyecto también debería compilar con Arduino core 3.x (pioarduino), pero
  no está probado.
- Configuración de placa en `platformio.ini`: `esp32-s3-devkitc-1` con
  `board_build.arduino.memory_type = qio_opi` (flash QIO + PSRAM octal, necesario para el
  módulo N8R8), `ARDUINO_USB_MODE=1` y `ARDUINO_USB_CDC_ON_BOOT=1` (consola por el USB nativo).

```bash
pio run                 # compilar
pio run -t upload       # grabar por COM4 (ver upload_port en platformio.ini)
pio device monitor      # consola a 115200
```

## Ciclo de vida del firmware

```
setup()
 ├─ Board::earlyInit()        latch de batería, rails, LED, botones
 ├─ ¿arranque en frío?        → estado y cachés en RTC memory a cero
 ├─ Net::loadConfig()         Preferences (NVS)
 ├─ detección de host USB     → g_alwaysOn
 ├─ RTC + SHTC3 + batería     hora del RTC → reloj del sistema (TZ Argentina)
 ├─ botón que despertó        BOOT/PWR corto o largo → sección / portal / sync / apagar
 ├─ UI::begin(cold)           init del panel (refresco inicial sólo en frío)
 ├─ portal o sync si toca     doSync(): Wi-Fi → NTP → ubicación → clima → dólar → feriados → noticias
 ├─ renderCurrent()           refresco parcial o completo
 └─ deep-sleep hasta el próximo minuto   (o loop() en modo siempre encendido)
```

En modo siempre encendido, `loop()` hace polling de botones, actualiza el reloj cada minuto,
sincroniza cuando vence el intervalo y vuelve a deep-sleep si se desconecta la PC.

Todo lo que se descarga vive en `RTC_DATA_ATTR` (sobrevive al deep-sleep, ~2 KB): si una
descarga falla se conserva el dato anterior con su hora de actualización.

## Comandos por serie (modo siempre encendido)

| Tecla | Acción |
|---|---|
| `n` / `p` | sección siguiente / anterior |
| `s` | sincronizar ahora |
| `f` | forzar refresco completo |
| `w` | abrir el portal Wi-Fi |
| `d` | volcar la pantalla actual (base64) |
| `a` | dibujar y volcar las 7 secciones sin refrescar el panel |

## Ver la pantalla desde la PC

`tools/fbdump.py` manda `a` (o `d`), captura los volcados y genera PNG a escala 2× en
`tools/out/`:

```bash
python tools/fbdump.py COM4 a
```

Requiere `pyserial` y `Pillow`. Es la forma más rápida de iterar la UI sin mirar la placa:
la clase `ShadowDisplay` en `src/ui.cpp` mantiene una copia 1-bit de todo lo dibujado.

## Grabar cuando la placa está en deep-sleep

Con el USB nativo del ESP32-S3, en deep-sleep el puerto COM desaparece y sólo reaparece
~1,5 s cada minuto. Opciones:

1. Conectar la PC **antes** de que arranque (o reiniciar con la PC conectada): detecta el host
   y no duerme.
2. Mantener **BOOT** 2 s: abre el portal y queda despierta hasta 5 min.
3. Mantener **BOOT** y reiniciar/reconectar: modo descarga del ROM.
4. `python tools/flash_catch.py COM4` — espera a que aparezca el puerto, lo toma, fuerza el
   bootloader ROM con la secuencia DTR/RTS y recién entonces corre esptool (una vez en el
   bootloader, ya no se duerme). Las ventanas de un despertar "de reloj" (~1 s) suelen ser
   demasiado cortas para Windows; conviene provocar una ventana larga con **PWR** 2 s
   (sincronización, ~10 s) o esperar la sincronización periódica.

## Cosas que NO hacer (aprendidas a golpes)

- **No usar `setCpuFrequencyMhz()`**: en el ESP32-S3 el cambio de reloj re-enumera el USB
  nativo, el host deja de mandar SOF unos segundos y la detección de PC lo interpreta como
  "desconectado" (y la placa se duerme). El ahorro de energía se logra apagando el Wi-Fi
  entre sincronizaciones.
- **No leer `USB_SERIAL_JTAG.int_raw.sof_int_raw` a mano**: el tick-hook de HWCDC lo borra
  cada 1 ms y la lectura se vuelve una lotería. Usar `HWCDC::isPlugged()`.
- **No decidir el modo de energía en los primeros ms tras un deep-sleep**: la PC tarda
  ~1-2 s en re-enumerar el USB; por eso el chequeo definitivo se hace al final del ciclo.
- `sntp_get_sync_status()` devuelve `COMPLETED` **una sola vez** y se resetea: guardar el
  resultado en una variable.
- Las fuentes Helvetica de U8g2 no tienen `…` ni `→`.

## Agregar una fuente de noticias

Editar la tabla `NEWS_SOURCES` en `src/appdata.cpp` (clave, nombre, URL del RSS 2.0) y el
`datalist` del portal en `src/net.cpp`. El parser (`RssTitleSink`) extrae `<item><title>`,
soporta CDATA y entidades HTML, y aborta la descarga al juntar `NEWS_MAX_TITLES` títulos o
`NEWS_MAX_BYTES` bytes. Algunos medios (Infobae, Página/12, TN, La Voz) bloquean clientes no
navegador y no funcionan.

## Agregar una sección

1. Sumar el valor al `enum Page` en `include/config.h` (antes de `PAGE_COUNT`).
2. Escribir `pageXxx()` en `src/ui.cpp` y agregarla al `switch` de `drawPage()`.
3. Si necesita datos nuevos: struct en `include/appdata.h` (RTC_DATA_ATTR en `main.cpp`) y
   un `fetchXxx()` en `src/net.cpp` llamado desde `doSync()`.

## Convenciones de la UI

- Área útil 200×200; cabecera 0–17 (`drawHeader`), pie de navegación desde 188 (`drawNav`).
- Fuentes U8g2 Latin-1 (`helvB/R 08/10/12/14/24`, `logisoso50` para el reloj): tienen acentos y
  `°`, pero **no** `…` ni `→` (usar `...` y `->`).
- `fit()` recorta con "..." respetando UTF-8; `wrap()` hace word-wrap por ancho medido.
- Íconos de clima en `drawWeatherIcon(x, y, tamaño, icono)`, escalables (40 px base).
