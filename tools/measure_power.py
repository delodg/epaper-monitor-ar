#!/usr/bin/env python3
"""Mide el consumo de tiempo del firmware: fuerza N ciclos de deep-sleep (comando 'B')
y lee el informe ('P'). Uso: python tools/measure_power.py [COM9] [etiqueta]"""
import serial, time, sys, re
port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
label = sys.argv[2] if len(sys.argv) > 2 else ""

def openp():
    s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2
    s.dtr = True; s.rts = False; s.open(); return s

s = openp(); t0 = time.time(); buf = b""
while time.time() - t0 < 90:                      # esperar a que termine el arranque
    buf += s.read(4096)
    if b"modo siempre encendido" in buf: break
s.reset_input_buffer(); s.write(b"B"); time.sleep(0.8)
try: s.close()
except Exception: pass
print(f"midiendo 10 ciclos {label}...", flush=True)

t0 = time.time(); report = []
while time.time() - t0 < 240:
    try: s = openp()
    except Exception: time.sleep(0.1); continue
    s.write(b"P"); t1 = time.time(); out = b""
    while time.time() - t1 < 3:
        try: out += s.read(2048)
        except Exception: break
    try: s.close()
    except Exception: pass
    txt = out.decode("utf-8", "replace")
    m = re.search(r"\[pwr\] (\d+) ciclos", txt)
    if m and int(m.group(1)) >= 10:
        report = [l.strip() for l in txt.splitlines() if "[pwr]" in l]
        break
    time.sleep(0.05)
print("\n".join(report) if report else "(sin informe)")
