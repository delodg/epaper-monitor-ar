# Hardware: Waveshare ESP32-S3-ePaper-1.54 (V2)

## Identificación

- Módulo: **ESP32-S3-PICO-1-N8R8** (LGA56) — 8 MB flash QIO (GD) + 8 MB PSRAM **octal** (AP_3v3).
- USB nativo USB-Serial/JTAG: Windows lo ve como "Dispositivo serie USB" (`VID_303A&PID_1001`).
- La **V1** (vendida hasta el 1/11/2025) usa el mismo mapa de pines pero PSRAM quad; la variante
  **ESP32-S3-Touch-ePaper-1.54** agrega un táctil FT6336 por I2C. Este firmware se probó en V2.
- Cómo saber cuál tenés: `esptool flash-id` reporta "Embedded PSRAM 8MB" en la V2.

## Mapa de pines

| Función | GPIO | Notas |
|---|---|---|
| EPD DC | 10 | |
| EPD CS | 11 | |
| EPD SCK | 12 | SPI2 |
| EPD MOSI | 13 | |
| EPD RST | 9 | |
| EPD BUSY | 8 | |
| **EPD_PWR** | 6 | **activo en bajo** (LOW = pantalla alimentada). Se mantiene con *hold* en deep-sleep |
| AUDIO_PWR | 42 | activo en bajo (codec ES8311 + mic + parlante; el firmware lo deja apagado) |
| **VBAT_EN** | 17 | HIGH = rail de batería encendido (latch). Se mantiene con *hold* en deep-sleep |
| LED | 3 | activo en bajo |
| BOOT | 0 | botón, activo en bajo, wake-up EXT1 |
| PWR | 18 | botón, activo en bajo, wake-up EXT1 |
| RTC INT | 5 | alarma del PCF85063 (no se usa) |
| I2C SDA / SCL | 47 / 48 | RTC PCF85063 (0x51), SHTC3 (0x70), FT6336 (variante Touch) |
| VBAT ADC | 4 | ADC1_CH3, divisor 200k/200k → Vbat = 2 × Vpin |
| microSD | — | ranura SDMMC, no se usa |

## Pantalla

Panel 1.54" 200×200 blanco/negro, controlador SSD1681 (Good Display GDEH0154D67). En GxEPD2 es
`GxEPD2_154_D67`. Soporta refresco parcial rápido (~0,5 s) y completo (~2 s, con parpadeo).
El firmware hace refresco parcial cada minuto y completo al cambiar de sección, al arrancar y
como máximo cada 60 min (`FULL_REFRESH_EVERY_MIN`) para evitar ghosting. La pantalla se deja en
*hibernate* (deep-sleep modo 1, conserva la RAM) y su rail permanece encendido durante el
deep-sleep del ESP32 para que el refresco parcial siguiente compare contra la imagen anterior.

Si tu placa muestra la imagen rotada, cambiá `EPD_ROTATION` (0..3) en `src/ui.cpp` o
definí `-DEPD_ROTATION=2` en `platformio.ini`.

## Energía

- **Encendido a batería**: al apretar PWR el hardware alimenta momentáneamente la placa; el
  firmware pone `VBAT_EN` en alto en los primeros milisegundos (`Board::earlyInit()`) para
  quedar encendida (latch). "Apagar" = `VBAT_EN` en bajo.
- **Deep-sleep**: se mantienen con `gpio_hold_en` los pines `VBAT_EN` y `EPD_PWR`. Los
  botones despiertan por EXT1 (`ANY_LOW`) con pull-ups RTC; el timer despierta al siguiente
  minuto. El orden en el arranque es importante: primero se configura el nivel del pin y
  después se libera el *hold*, así no hay glitches en los rails.
- **Detección de PC por USB**: el host manda un paquete SOF cada 1 ms; el core de Arduino lo
  vigila desde un tick-hook (`HWCDC::isPlugged()`). Con host presente no se usa deep-sleep.
- Consumo orientativo: deep-sleep ~0,2 mA (rail de pantalla incluido); despierto sin Wi-Fi
  ~40 mA; sincronizando ~120–150 mA durante 10–15 s cada intervalo.

## Batería

Curva Li-ion aproximada en `Board::batteryPercent()` (4,20 V = 100 %, 3,40 V = 0 %). Con
tensión > 4,35 V se asume alimentación USB y se dibuja el rayo en el ícono.

## Firmware de fábrica

Waveshare publica el programa de fábrica y una versión del asistente XiaoZhi en
`03_Firmware/` de [waveshareteam/ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54).
Para volver al de fábrica (V2):

```bash
esptool --chip esp32s3 --port COM4 --baud 460800 write-flash --flash-mode keep --flash-size keep 0x0 V2-FactoryProgram.bin
```

Para entrar en modo descarga manualmente: mantener **BOOT**, conectar/reiniciar, soltar BOOT.
