# Firmware precompilado

`epaper-monitor-ar-vX.Y.Z-merged.bin` es una imagen única (bootloader + tabla de particiones +
boot_app0 + aplicación) para grabar en `0x0`:

```bash
esptool --chip esp32s3 --port COM4 --baud 460800 write-flash 0x0 epaper-monitor-ar-v1.1.0-merged.bin
```

Generada con `esptool merge-bin` a partir de la salida de PlatformIO
(`.pio/build/waveshare_epaper154/`).
