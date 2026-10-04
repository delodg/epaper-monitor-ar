# Instalación por perfil y rendimiento de cuatro colores

El parche recibido agrega el driver correcto para la 1.54G y un piso de 5 min para el reloj.
Se incorporó como código a revisar. El catálogo separa además memoria, chip y cableado: no existe
una imagen que sirva para cualquier ESP32 y pantalla.

## Qué puede detectar el instalador

El comando `I` a 115200 baud produce una línea como:

```text
[hardware]{"schemaVersion":1,"profileId":"waveshare_epaper154g","chipFamily":"ESP32-S3","flashBytes":8388608,"version":"1.4.0"}
```

El ID sale del entorno de compilación y la memoria de `ESP.getFlashChipSize()`. La web compara
ID/chip/memoria con el catálogo, libera el puerto y selecciona su manifiesto. Se consulta al
conectar un puerto previamente autorizado; el primer permiso USB requiere un clic del usuario.
También responde mientras el panel está ocupado y durante las esperas del portal, Wi-Fi y NTP.
Una descarga HTTP bloqueante puede demorar la respuesta: reintentar cuando termine de sincronizar.

Esto identifica el **perfil del firmware instalado**, no mide eléctricamente la pantalla ni
verifica el cableado. Si antes se grabó un perfil incorrecto, hay que corregir la selección manual.
Un dispositivo nuevo, en bootloader, con firmware anterior o de fábrica no informa ese perfil:
se pide elegir modelo/pantalla y no se adivina por USB VID/PID o por el chip.

## Cambios de rendimiento

- GxEPD2 1.6.9 ya usa el modo rápido completo; se fija explícitamente por su API base.
  Coincide con E0=02 / E6=5D / A5=00 del ejemplo oficial de Waveshare. No se acorta BUSY.
- Se evita el refresco de portada en 4C antes de la pantalla útil: ahorra un ciclo físico al arrancar.
- Una imagen idéntica evita transferencia/refresco en modo normal. Los refrescos completos
  explícitos y periódicos siguen ejecutándose. La comparación ocupa 5 KB adicionales sólo en 4C.
- Durante BUSY se recopilan pulsaciones con debounce, sin renderizar ni ejecutar red de forma
  reentrante. Pulsaciones rápidas se agrupan hasta el destino final; también se atienden a batería
  antes de dormir. Un ciclo ya iniciado debe terminar.
- En USB la cadencia automática se mide desde el final del último refresco, evitando otro ciclo
  inmediato al cruzar un minuto durante el refresco anterior.
- `[display]` informa dibujo y transferencia+panel por separado. Un refresco idéntico omitido
  tiene su propio mensaje. El comando `T` conserva el desglose del ciclo completo.

[Waveshare publica 15 s rápido / 20 s completo](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G).
No hay medición propia de esta nueva versión en la 1.54G: no afirmar navegación subsegundo ni
aplicar los consumos/autonomía medidos en V2 B/N al panel de color.

## Validación física pendiente

1. Grabar `waveshare_epaper154g`, comprobar el QR de primer uso y que no aparezca la portada extra.
2. Con Wi-Fi configurado, abrir consola 115200 y anotar diez líneas `[display]` al navegar;
   separar arranque/red de refresco. Comparar con el firmware anterior en la misma placa y temperatura.
3. Pulsar BOOT tres veces durante BUSY: al terminar debe ir tres secciones adelante con un único
   refresco posterior. Probar ida/vuelta, pulsaciones largas y navegación a batería.
4. Verificar que el reloj en USB no refresque al minuto siguiente de navegar, y que `s` no
   refresque una pantalla de datos sin cambios salvo que corresponda un refresco completo.
5. Consultar `I` en reposo, durante BUSY y con el portal abierto; probar detección en Chrome/Edge.
   Probar firmware anterior/bootloader y confirmar que requieren elegir pantalla.
6. Validar cada perfil genérico con su cableado, memoria, periféricos ausentes y conector USB.

CI compila los siete entornos y comprueba el empaquetado. Estas comprobaciones no sustituyen
las pruebas de pantalla, botones, alimentación y tiempo real en cada placa.
