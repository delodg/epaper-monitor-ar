#!/usr/bin/env python3
"""Graba el firmware en una placa que está en deep-sleep: espera a que aparezca el puerto,
lo toma, fuerza el bootloader ROM con DTR/RTS (secuencia USB-Serial/JTAG) y corre esptool.

Uso: python tools/flash_catch.py [COM4] [segundos_max]
"""
import serial, serial.tools.list_ports, subprocess, time, sys, os
port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
maxs = int(sys.argv[2]) if len(sys.argv) > 2 else 1100
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(root, ".pio", "build", "waveshare_epaper154")
BOOTAPP = os.path.expanduser(r"~\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin")
cmd = ["esptool", "--chip", "esp32s3", "--port", port, "--baud", "460800", "--before", "no-reset", "--after", "hard-reset",
       "write-flash", "--flash-mode", "keep", "--flash-size", "8MB",
       "0x0", f"{B}/bootloader.bin", "0x8000", f"{B}/partitions.bin", "0xe000", BOOTAPP, "0x10000", f"{B}/firmware.bin"]

def port_present():
    return any(p.device == port for p in serial.tools.list_ports.comports())

def enter_bootloader():
    p = serial.Serial(); p.port = port; p.baudrate = 115200; p.timeout = 0.1
    p.dtr = False; p.rts = False; p.open()
    time.sleep(0.05)
    p.dtr = True;  p.rts = False; time.sleep(0.1)
    p.rts = True;  p.dtr = False; p.rts = True; time.sleep(0.1)
    p.dtr = False; p.rts = False; time.sleep(0.2)
    data = p.read(300); p.close(); return data

t0 = time.time(); attempt = 0
while time.time() - t0 < maxs:
    if port_present():
        attempt += 1; ts = time.time() - t0
        try:
            data = enter_bootloader()
            print(f"[{ts:6.1f}s] puerto tomado, ROM: {data[:60]!r}", flush=True)
        except Exception as e:
            print(f"[{ts:6.1f}s] puerto listado pero no abrible aún ({str(e)[:50]})", flush=True)
            time.sleep(0.4); continue
        time.sleep(0.3)
        r = subprocess.run(cmd, capture_output=True, text=True); out = r.stdout + r.stderr
        if "Hash of data verified" in out and r.returncode == 0:
            print("FLASH OK"); sys.exit(0)
        print("esptool:", (out.strip().splitlines() or ["?"])[-1][:120], flush=True); time.sleep(1.5)
    time.sleep(0.03)
print("no se pudo grabar en el tiempo dado"); sys.exit(1)
