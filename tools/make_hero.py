#!/usr/bin/env python3
"""Compone la imagen "hero" del README a partir de las capturas reales de la placa:
cada pantalla dentro de un bisel de dispositivo con sombra y leve rotación, sobre un fondo
con degradado, más el título de la app y el logo.

Uso:
  python tools/make_hero.py light   -> docs/img/hero-light.png  (capturas de docs/img)
  python tools/make_hero.py dark    -> docs/img/hero-dark.png   (capturas de docs/img/dark)
Requiere Pillow. Usa Segoe UI / Arial si están instaladas.
"""
import os, sys, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

theme = sys.argv[1] if len(sys.argv) > 1 else "dark"
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "..", "docs", "img")
SRC = IMG if theme == "light" else os.path.join(IMG, "dark")
OUT = os.path.join(IMG, f"hero-{theme}.png")

SCREENS = ["00-portada", "01-reloj", "02-clima", "03-mar", "04-sol", "05-dolar",
           "06-economia", "07-noticias", "08-feriados", "09-interior", "10-wifi", "11-sistema"]
W, H = 1800, 1500
COLS = 4
SCREEN, BEZEL, RADIUS = 240, 26, 34
random.seed(7)

if theme == "dark":
    BG_TOP, BG_BOT, GLOW = (13, 17, 23), (24, 30, 42), (30, 44, 70)
    TITLE, ACCENT, SUB, MUTED = (245, 245, 240), (120, 170, 255), (170, 178, 190), (120, 128, 140)
    BODY, EDGE, RELIEF, BTN, BTN_EDGE = (44, 47, 54), (24, 26, 30), (70, 74, 82), (58, 62, 70), (30, 32, 36)
    SCREEN_FRAME, SHADOW_A, LOGO_COLOR = (12, 12, 14), 200, (235, 235, 230)
else:
    BG_TOP, BG_BOT, GLOW = (247, 247, 245), (228, 229, 226), (255, 255, 255)
    TITLE, ACCENT, SUB, MUTED = (24, 27, 33), (30, 90, 200), (70, 76, 86), (120, 126, 136)
    BODY, EDGE, RELIEF, BTN, BTN_EDGE = (236, 236, 231), (205, 205, 198), (250, 250, 247), (214, 214, 208), (190, 190, 184)
    SCREEN_FRAME, SHADOW_A, LOGO_COLOR = (120, 120, 116), 110, (24, 27, 33)


def font(size, bold=False):
    names = ["segoeuib.ttf", "arialbd.ttf"] if bold else ["segoeui.ttf", "arial.ttf"]
    for name in names:
        p = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def gradient(w, h, top, bot):
    col = Image.new("RGB", (1, h))
    px = col.load()
    for y in range(h):
        t = y / (h - 1)
        px[0, y] = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
    return col.resize((w, h))


def device(screen_img):
    """Dispositivo: bisel con relieve, botones laterales y pantalla e-paper."""
    size = SCREEN + 2 * BEZEL
    dev = Image.new("RGBA", (size + 20, size + 20), (0, 0, 0, 0))
    d = ImageDraw.Draw(dev)
    d.rounded_rectangle([10, 10, 10 + size, 10 + size], RADIUS, fill=BODY, outline=EDGE, width=2)
    d.rounded_rectangle([14, 14, 6 + size, 6 + size], RADIUS - 4, outline=RELIEF, width=1)
    for by in (size * 0.45, size * 0.62):
        d.rounded_rectangle([10 + size - 3, 10 + by, 10 + size + 4, 10 + by + 26], 3, fill=BTN, outline=BTN_EDGE)
    sx, sy = 10 + BEZEL, 10 + BEZEL
    d.rectangle([sx - 3, sy - 3, sx + SCREEN + 2, sy + SCREEN + 2], fill=SCREEN_FRAME)
    scr = screen_img.convert("L").resize((SCREEN, SCREEN), Image.LANCZOS)
    scr = Image.eval(scr, lambda v: 30 + int(v * (236 - 30) / 255))          # tinta electrónica: no es blanco/negro puro
    scr_rgb = Image.merge("RGB", (scr, scr, Image.eval(scr, lambda v: max(0, v - 6))))
    dev.paste(scr_rgb, (sx, sy))
    return dev


def with_shadow(dev, angle):
    rot = dev.rotate(angle, resample=Image.BICUBIC, expand=True)
    shadow = Image.new("RGBA", rot.size, (0, 0, 0, 0))
    shadow.paste((0, 0, 0, SHADOW_A), (0, 0), rot.split()[3])
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    canvas = Image.new("RGBA", (rot.width + 60, rot.height + 60), (0, 0, 0, 0))
    canvas.alpha_composite(shadow, (36, 52))
    canvas.alpha_composite(rot, (30, 30))
    return canvas


# fondo con degradado y un resplandor suave arriba
bg = gradient(W, H, BG_TOP, BG_BOT)
glow = Image.new("RGB", (W, H), (0, 0, 0))
ImageDraw.Draw(glow).ellipse([W * 0.1, -H * 0.25, W * 0.9, H * 0.45], fill=GLOW if theme == "dark" else (8, 8, 8))
glow = glow.filter(ImageFilter.GaussianBlur(200))
bg = ImageChops.add(bg, glow) if theme == "dark" else ImageChops.subtract(bg, glow)
scene = bg.convert("RGBA")

# título
d = ImageDraw.Draw(scene)
d.text((90, 70), "ePaper Monitor", font=font(84, bold=True), fill=TITLE)
d.text((92, 172), "ARGENTINA", font=font(26, bold=True), fill=ACCENT)
d.text((90, 214), "Hora · Clima · Mareas · Sol y Luna · Dólar · Economía · Noticias · Feriados · Interior",
       font=font(28), fill=SUB)
d.text((90, 254), f"Waveshare ESP32-S3-ePaper-1.54  ·  firmware open source  ·  sin API keys  ·  modo {'oscuro' if theme == 'dark' else 'claro'}",
       font=font(24), fill=MUTED)

# grilla de dispositivos
cell_w, cell_h = 400, 380
x0, y0 = (W - COLS * cell_w) // 2, 330
for i, name in enumerate(SCREENS):
    path = os.path.join(SRC, name + ".png")
    if not os.path.exists(path):
        continue
    dev = with_shadow(device(Image.open(path)), random.uniform(-4, 4))
    col, row = i % COLS, i // COLS
    cx = x0 + col * cell_w + cell_w // 2 + random.randint(-8, 8)
    cy = y0 + row * cell_h + cell_h // 2 + random.randint(-8, 8)
    scene.alpha_composite(dev, (cx - dev.width // 2, cy - dev.height // 2))

# pie: URL y logo
d = ImageDraw.Draw(scene)
d.text((90, H - 70), "github.com/delodg/epaper-monitor-ar", font=font(24), fill=MUTED)
logo = os.path.join(HERE, "logo", "logo_figma.png")
if os.path.exists(logo):
    lg = Image.open(logo).convert("RGBA")
    tinted = Image.new("RGBA", lg.size, LOGO_COLOR + (0,))
    tinted.putalpha(lg.split()[3])
    lw = 190
    tinted = tinted.resize((lw, int(lg.height * lw / lg.width)), Image.LANCZOS)
    scene.alpha_composite(tinted, (W - 90 - lw, H - 72))

scene.convert("RGB").save(OUT, optimize=True)
print("guardado", os.path.normpath(OUT), scene.size)
