#!/usr/bin/env python3
"""Convierte logo_figma.png (wordmark DELO exportado de Figma, negro sobre transparente)
en un bitmap de 1 bit para GxEPD2 (include/logo_delo.h).

Uso: python tools/logo/make_logo.py [ancho_px]   (por defecto 188)
Formato: 1 bit por píxel, MSB primero, filas alineadas a byte; bit=1 -> píxel negro.
"""
import os, sys
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "logo_figma.png")
out = os.path.join(here, "..", "..", "include", "logo_delo.h")
target_w = int(sys.argv[1]) if len(sys.argv) > 1 else 188

im = Image.open(src).convert("RGBA")
# "tinta" = alfa * oscuridad (el logo es negro sobre transparente)
r, g, b, a = im.split()
gray = Image.merge("RGB", (r, g, b)).convert("L")
ink = Image.eval(gray, lambda v: 255 - v)              # negro -> 255
ink = Image.merge("L", (ink,))
ink = Image.composite(ink, Image.new("L", im.size, 0), a)  # fuera del alfa -> 0

w = target_w
h = max(1, round(im.height * w / im.width))
ink = ink.resize((w, h), Image.LANCZOS)
bw = ink.point(lambda v: 255 if v >= 128 else 0, "1")     # umbral 50 %

row_bytes = (w + 7) // 8
data = bytearray()
px = bw.load()
for y in range(h):
    for bx in range(row_bytes):
        byte = 0
        for bit in range(8):
            x = bx * 8 + bit
            if x < w and px[x, y] != 0:
                byte |= 0x80 >> bit
        data.append(byte)

lines = []
lines.append("#pragma once")
lines.append("#include <Arduino.h>")
lines.append(f"// Logo DELO (wordmark) — generado por tools/logo/make_logo.py a partir de logo_figma.png")
lines.append(f"// {w}x{h} px, 1 bit/píxel, MSB primero, bit=1 = negro. Usar con display.drawBitmap().")
lines.append(f"#define LOGO_DELO_W {w}")
lines.append(f"#define LOGO_DELO_H {h}")
lines.append(f"static const uint8_t LOGO_DELO[{len(data)}] PROGMEM = {{")
for i in range(0, len(data), 16):
    chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + 16])
    lines.append(f"  {chunk},")
lines.append("};")
open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")

# vista previa (4x) para revisar
bw.resize((w * 4, h * 4), Image.NEAREST).save(os.path.join(here, "logo_bitmap_preview.png"))
print(f"{w}x{h} -> {len(data)} bytes -> {os.path.normpath(out)}")
