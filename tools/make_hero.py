#!/usr/bin/env python3
"""Compone la imagen "hero" del README a partir de las capturas reales de la placa.
Diseño plano y editorial: retícula exacta, sin sombras ni rotaciones, tipografía grande,
captions numerados por sección y el logo DELO.

Uso:
  python tools/make_hero.py light   -> docs/img/hero-light.png  (capturas de docs/img)
  python tools/make_hero.py dark    -> docs/img/hero-dark.png   (capturas de docs/img/dark)
Requiere Pillow. Usa Segoe UI (Windows) si está; si no, la fuente por defecto.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

theme = sys.argv[1] if len(sys.argv) > 1 else "light"
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "..", "docs", "img")
SRC = IMG if theme == "light" else os.path.join(IMG, "dark")
OUT = os.path.join(IMG, f"hero-{theme}.png")

SCREENS = [("00-portada", "Portada"), ("01-reloj", "Reloj"), ("02-clima", "Clima"), ("03-mar", "Mar"),
           ("04-sol", "Sol y Luna"), ("05-dolar", "Dólar"), ("06-economia", "Economía"), ("07-noticias", "Noticias"),
           ("08-feriados", "Feriados"), ("09-interior", "Interior"), ("10-wifi", "Wi-Fi"), ("11-sistema", "Sistema")]

# ---- paleta ----
if theme == "dark":
    BG, TITLE, ACCENT, TEXT, MUTED, RULE = (11, 13, 16), (245, 245, 242), (110, 165, 255), (190, 196, 205), (110, 116, 126), (40, 44, 52)
    BODY, BODY_EDGE, SCREEN_EDGE, BTN = (28, 31, 37), (48, 52, 60), (8, 9, 11), (52, 56, 64)
    INK_LO, INK_HI, LOGO = 26, 232, (245, 245, 242)
else:
    BG, TITLE, ACCENT, TEXT, MUTED, RULE = (246, 246, 244), (20, 22, 26), (28, 80, 200), (70, 75, 84), (140, 146, 156), (218, 219, 216)
    BODY, BODY_EDGE, SCREEN_EDGE, BTN = (238, 238, 234), (208, 208, 202), (96, 96, 92), (214, 214, 208)
    INK_LO, INK_HI, LOGO = 28, 236, (20, 22, 26)

# ---- retícula ----
M = 120                     # margen
COLS, ROWS = 4, 3
SCREEN, BEZEL, RADIUS = 240, 22, 22
DEV = SCREEN + 2 * BEZEL    # 284
GUT = 64                    # separación horizontal
CAPTION_H = 44
ROW_PITCH = DEV + CAPTION_H + 40
GRID_W = COLS * DEV + (COLS - 1) * GUT
W = GRID_W + 2 * M
GRID_Y = 470
H = GRID_Y + ROWS * ROW_PITCH + 60


def font(size, weight="regular"):
    files = {"light": ["segoeuil.ttf", "segoeuisl.ttf"], "regular": ["segoeui.ttf", "arial.ttf"],
             "semibold": ["seguisb.ttf", "segoeuib.ttf"], "bold": ["segoeuib.ttf", "arialbd.ttf"]}[weight]
    for name in files:
        p = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def tracked(d, xy, s, f, fill, tracking):
    """Texto con espaciado entre letras."""
    x, y = xy
    for ch in s:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + tracking
    return x


def device(screen_img):
    """Dispositivo plano: bisel con hairline, botones laterales y pantalla e-paper."""
    dev = Image.new("RGBA", (DEV + 8, DEV), (0, 0, 0, 0))
    d = ImageDraw.Draw(dev)
    d.rounded_rectangle([0, 0, DEV - 1, DEV - 1], RADIUS, fill=BODY, outline=BODY_EDGE, width=1)
    for by in (int(DEV * 0.42), int(DEV * 0.58)):
        d.rounded_rectangle([DEV - 2, by, DEV + 4, by + 30], 3, fill=BTN, outline=BODY_EDGE, width=1)
    sx = sy = BEZEL
    d.rectangle([sx - 2, sy - 2, sx + SCREEN + 1, sy + SCREEN + 1], fill=SCREEN_EDGE)
    scr = screen_img.convert("L").resize((SCREEN, SCREEN), Image.LANCZOS)
    scr = Image.eval(scr, lambda v: INK_LO + int(v * (INK_HI - INK_LO) / 255))   # e-paper: sin blanco/negro puros
    scr_rgb = Image.merge("RGB", (scr, scr, Image.eval(scr, lambda v: max(0, v - 5))))
    dev.paste(scr_rgb, (sx, sy))
    return dev


def logo(width, color):
    """Wordmark DELO teñido; el PNG de Figma tiene fondo blanco opaco: el alfa sale de la tinta."""
    p = os.path.join(HERE, "logo", "logo_figma.png")
    if not os.path.exists(p):
        return None
    lg = Image.open(p).convert("L")
    alpha = Image.eval(lg, lambda v: 255 - v)                       # negro -> opaco
    out = Image.new("RGBA", lg.size, color + (0,))
    out.putalpha(alpha)
    return out.resize((width, max(1, round(lg.height * width / lg.width))), Image.LANCZOS)


scene = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(scene)

# ---- cabecera ----
tracked(d, (M, 118), "ARGENTINA", font(22, "semibold"), ACCENT, 6)
d.text((M - 4, 150), "ePaper Monitor", font=font(112, "bold"), fill=TITLE)
d.text((M, 302), "Hora · Clima · Mareas · Sol y Luna · Dólar · Economía · Noticias · Feriados · Interior",
       font=font(30, "regular"), fill=TEXT)
d.text((M, 348), f"Waveshare ESP32-S3-ePaper-1.54  ·  firmware open source  ·  sin API keys  ·  tema {'oscuro' if theme == 'dark' else 'claro'}",
       font=font(24, "regular"), fill=MUTED)
lg = logo(230, LOGO)
if lg:
    scene.paste(lg, (W - M - lg.width, 118 - 4), lg)   # arriba a la derecha, a la altura de "ARGENTINA"
d.line([M, GRID_Y - 46, W - M, GRID_Y - 46], fill=RULE, width=2)

# ---- retícula de dispositivos con captions ----
cap_f, num_f = font(24, "semibold"), font(20, "regular")
for i, (name, caption) in enumerate(SCREENS):
    path = os.path.join(SRC, name + ".png")
    if not os.path.exists(path):
        continue
    col, row = i % COLS, i // COLS
    x = M + col * (DEV + GUT)
    y = GRID_Y + row * ROW_PITCH
    dev = device(Image.open(path))
    scene.paste(dev, (x, y), dev)
    d.text((x, y + DEV + 14), f"{i:02d}", font=num_f, fill=MUTED)
    d.text((x + 40, y + DEV + 12), caption, font=cap_f, fill=TEXT)

# ---- pie ----
d.line([M, H - 58, W - M, H - 58], fill=RULE, width=2)
d.text((M, H - 44), "github.com/delodg/epaper-monitor-ar", font=font(22, "regular"), fill=MUTED)
d.text((W - M - d.textlength("MIT · v1.2.0", font=font(22, "regular")), H - 44), "MIT · v1.2.0", font=font(22, "regular"), fill=MUTED)

scene.save(OUT, optimize=True)
print("guardado", os.path.normpath(OUT), scene.size)
