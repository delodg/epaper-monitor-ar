# Seguridad

## Reportar una vulnerabilidad

Si encontrás un problema de seguridad, por favor **no abras un issue público**: usá
[GitHub Security Advisories](https://github.com/delodg/epaper-monitor-ar/security/advisories/new)
("Report a vulnerability"). Respondo lo antes posible y publico el arreglo en un release.

Versiones con soporte: la última del branch `master` / el último release.

## Modelo de amenazas (qué protege y qué no)

Es un dispositivo de escritorio que **sólo consume** datos públicos por Internet. No expone
servicios de red en operación normal, no tiene OTA ni puertos abiertos, y no guarda ninguna
credencial en el código ni en el binario.

### Medidas implementadas

| Riesgo | Medida |
|---|---|
| Suplantación / manipulación de los datos (MITM) | Todas las conexiones HTTPS **validan la cadena de certificados** contra el bundle de CAs raíz de Mozilla embebido en el firmware (`certs/x509_crt_bundle`). No hay `setInsecure()` en el firmware de producción (existe un flag de compilación `-DTLS_INSECURE` sólo para depurar). |
| Acceso al portal de configuración | El portal Wi-Fi sólo se abre por acción física (BOOT 2 s) o si no hay red configurada, se cierra solo a los 5 min y la red `ePaperAR-Setup` está protegida con **WPA2 y una clave única por placa** (derivada del MAC de fábrica), visible únicamente en la pantalla y en el QR. El portal no ofrece "borrar Wi-Fi". |
| Respuestas hostiles o gigantes de un servidor | Descargas acotadas (`HTTP_MAX_BODY`, 64 KB; RSS parseado por streaming con tope de bytes y títulos), parsers con buffers fijos y `strlcpy`/`snprintf`, JSON con ArduinoJson. |
| Bucles de crash por datos maliciosos | Contador de reinicios por error en RTC-RAM: tras 3 seguidos se pospone la sincronización. Pila del loop de 16 KB. |
| Datos corruptos en RTC-RAM tras deep-sleep | Saneo de índices y terminadores de cadena al despertar. |
| Fugas en el repositorio y el binario | Sin credenciales, tokens ni datos de red; rutas de compilación anonimizadas (`-fmacro-prefix-map`); capturas con SSID/IP/MAC ocultos; historial de git revisado. |

### Limitaciones conocidas

- **Acceso físico = acceso total.** La clave Wi-Fi se guarda en la NVS del ESP32 sin cifrar
  (como en cualquier proyecto Arduino): quien tenga la placa puede leerla con `esptool`.
  No se activan *flash encryption* ni *secure boot* (son irreversibles y complican el
  desarrollo). Los comandos por USB no piden autenticación.
- Durante los 5 minutos en que el portal está abierto, el servidor web de WiFiManager también
  responde en la IP de la red doméstica (modo AP+STA).
- La geolocalización por IP (`ip-api.com`) usa HTTP plano; alguien en tu red podría hacer que
  la placa "crea" estar en otra ciudad. Se puede fijar la ciudad a mano en el portal.
- NTP no está autenticado (estándar); la hora se corrige en cada sincronización.
- La placa muestra en pantalla el nombre de tu red y su IP local (sección Wi-Fi).
