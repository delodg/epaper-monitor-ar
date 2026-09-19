#!/usr/bin/env python3
"""Captura por USB el contenido de la pantalla e-paper y lo guarda como PNG.

Uso:  python tools/fbdump.py [COM4] [a|d]
  a = volcar las 7 secciones (por defecto)   d = sólo la sección actual
Requiere: pyserial, Pillow.  El firmware debe estar en modo "siempre encendido" (USB).
"""
import base64, sys, time, os
import serial
from PIL import Image

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
cmd = sys.argv[2] if len(sys.argv) > 2 else "a"
out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(out_dir, exist_ok=True)

W, H, SCALE = 200, 200, 2
NAMES = ["reloj", "clima", "mar", "sol", "dolar", "economia", "noticias", "feriados", "interior", "wifi", "sistema"]

# OJO: en el USB-Serial/JTAG del ESP32-S3, RTS activo resetea la placa; DTR activo hace falta
# para que el driver HWCDC transmita (sin DTR el volcado se queda trabado). => DTR=1, RTS=0.
ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = port, 115200, 1
ser.dtr = True
ser.rts = False
ser.open()
with ser as s:
    s.reset_input_buffer()
    s.write(cmd.encode())
    pages, cur, collecting, t0 = {}, None, False, time.time()
    while time.time() - t0 < 40:
        line = s.readline().decode("utf-8", "replace").strip()
        if not line:
            if pages and not collecting and (cmd == "d" or len(pages) >= len(NAMES)):
                break
            continue
        if line.startswith("[fb-begin"):
            cur, collecting, buf = int(line.split()[1].rstrip("]")), True, []
        elif line == "[fb-end]" and collecting:
            data = base64.b64decode("".join(buf))
            if len(data) == W * H // 8:
                pages[cur] = data
            collecting = False
            if cmd == "d" or len(pages) >= len(NAMES):
                break
        elif collecting:
            buf.append(line)

for n, data in sorted(pages.items()):
    img = Image.new("1", (W, H), 1)
    px = img.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = 1 if data[y * (W // 8) + x // 8] & (1 << (7 - x % 8)) else 0
    img = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
    name = NAMES[n] if n < len(NAMES) else str(n)
    path = os.path.join(out_dir, f"page_{n}_{name}.png")
    img.save(path)
    print("guardado", path)
if not pages:
    print("no se recibió ningún volcado (¿la placa está en modo siempre encendido?)")
