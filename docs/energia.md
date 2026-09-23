# Energía: qué consume y cómo se optimizó

Todos los tiempos de esta página están **medidos en la placa** (comandos `B`, `P` y `T` por
serie, ver [`desarrollo.md`](desarrollo.md)). Las corrientes son valores **típicos** del
ESP32-S3 y del panel, no medidas con amperímetro: sirven para comparar configuraciones, no
como especificación.

## Dónde se va la energía

La placa pasa casi todo el tiempo en deep-sleep. Sólo gasta cuando:

1. **Despierta a actualizar el reloj.** Medido: **821 ms** por despertar.

   | Etapa | ms |
   |---|---|
   | ROM + bootloader + init del core | 66 |
   | Sensores (SHTC3) + batería (ADC) | 18 |
   | `UI::begin()` (init del panel) | 30 |
   | Render + refresco parcial del panel | 461 |
   | `hibernate()` del panel | 141 |
   | Resto de `setup()` (config, detección de USB, RTC) | ~105 |

   Nota: el test de memoria de la PSRAM (~430 ms) **sólo corre en arranque en frío**, no en
   cada despertar, así que no afecta la batería.

2. **Sincroniza por Wi-Fi.** Cada petición HTTPS cuesta ~2 s (domina el handshake TLS).
   Medición del firmware v1.2.0:

   | Etapa | Sync completa | Sync típica (v1.2.0) |
   |---|---|---|
   | Conectar Wi-Fi | 0,7 s | 0,6 s |
   | NTP | 3,9 s | 0,2–1,5 s |
   | Clima | 3,6 s | — |
   | **Mar** | **12,4 s** | — |
   | Sol | 3,6 s | — |
   | Dólar | 2,2 s | 2,0 s |
   | **Economía** | **14,3 s** | — |
   | Feriados | 2,1 s | — |
   | Noticias | 3,3 s | 2,6 s |
   | **Total** | **45,6 s** | **5,6–7,1 s** |

## Qué se cambió (v1.3.0)

| Problema encontrado | Solución | Resultado medido |
|---|---|---|
| **Economía** bajaba cada hora dos históricos de ~50 KB (inflación **mensual**) | Cada campo con su propia antigüedad: inflación 1/día, riesgo país 6 h, cotizaciones y cripto 1 h | 14,3 s → ~4 s por hora |
| **Mar** sondeaba hasta 5 celdas del modelo en cada corte de energía | La celda que funciona se guarda en NVS, no sólo en RTC-RAM | 12,4 s → **2,7 s** |
| **NTP** en cada sincronización | El RTC PCF85063 deriva 1-2 s/día: alcanza 1 vez por día | −2 a −4 s por sync |
| **Dólar** se bajaba de madrugada y los domingos | Sólo en horario de mercado (lun-vie 10-19); fuera de eso cada 6 h | −2 s en la mayoría de las syncs |
| Se encendía la radio aunque no hubiera nada que bajar | `planSync()` decide **antes** de conectar; si no hay nada pendiente no enciende el Wi-Fi | sync en régimen: **0,31 s sin radio** |
| Reloj cada minuto las 24 h | Perfiles de energía + ahorro nocturno | menos de la mitad de despertares |
| Transmisión Wi-Fi a máxima potencia | `setTxPower(13 dBm)` (el router está cerca) | menos corriente en cada ráfaga |

## Perfiles

Se eligen en el portal ("Energía"), y se ven en la pantalla Sistema.

| Perfil | Reloj | Sincronización | Datos lentos |
|---|---|---|---|
| 0 · Rendimiento | 1 min | según intervalo (mín. 5) | cadencia base |
| 1 · Equilibrado *(por defecto)* | 2 min | mín. 30 min | ×2 |
| 2 · Ahorro | 5 min | mín. 60 min | ×3 |

Además, automáticamente:
- **Ahorro nocturno** (00:00–07:00, activable): reloj cada 10 min, sync cada 2 h.
- **Batería ≤ 15 %**: pasa a la cadencia de ahorro aunque el perfil sea otro.
- **Con USB**: la placa no duerme y el reloj se actualiza cada minuto (no hay batería que cuidar).

## Estimación de autonomía

Con los tiempos medidos y corrientes típicas (despierto 42 mA, Wi-Fi 110 mA, deep-sleep
0,15 mA), para una batería de 1000 mAh:

| Configuración | Consumo estimado | Autonomía estimada |
|---|---|---|
| v1.2.0 (reloj 1 min, sync 15 min) | ~56 mAh/día | ~18 días |
| v1.3.0 equilibrado + noche | ~23 mAh/día | **~43 días** |
| v1.3.0 ahorro + noche | ~13 mAh/día | ~75 días |

La pantalla Sistema muestra la estimación calculada con los tiempos reales de *esa* placa.

## Ideas que quedaron pendientes

- **Refrescar sólo la zona del reloj** (ventana parcial) en vez de las 200×200: el refresco
  del panel baja de ~460 ms a ~150 ms estimados. Ahorra ~1,5 mAh/día en equilibrado; no se
  implementó todavía por el riesgo de artefactos en la tinta electrónica.
- **Cortar el rail de la pantalla en deep-sleep** (`EPD_PWR`): la imagen es biestable y se
  conserva sin alimentación, pero se pierde la imagen base del refresco parcial (el siguiente
  refresco tendría que ser completo, con parpadeo). Habría que medir con amperímetro si el
  módulo consume lo suficiente para que convenga.
- **Medir la corriente real** con un amperímetro o un USB power meter para reemplazar las
  corrientes típicas por valores reales.
