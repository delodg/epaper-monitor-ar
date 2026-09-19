#!/usr/bin/env python3
"""Genera certs/x509_crt_bundle en el formato v1 que entiende el core de Arduino-ESP32 2.0.x
(libraries/WiFiClientSecure/src/esp_crt_bundle.c): [num_certs BE16] + por certificado
[name_len BE16][key_len BE16][subject DER][SubjectPublicKeyInfo DER], ordenados por subject.

Entrada: el listado raíz de Mozilla que trae ESP-IDF (cacrt_all.pem) o cualquier PEM.
Uso: python tools/gen_crt_bundle_v1.py [ruta.pem]
Requiere el paquete `cryptography`.
"""
import os, sys, struct, glob
from cryptography import x509
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "certs", "x509_crt_bundle")

if len(sys.argv) > 1:
    pem_path = sys.argv[1]
else:
    cands = glob.glob(os.path.expanduser("~/.platformio/packages/framework-espidf*/components/mbedtls/esp_crt_bundle/cacrt_all.pem"))
    if not cands:
        sys.exit("No encuentro cacrt_all.pem; pasá la ruta de un PEM con las CAs raíz (p. ej. el de Mozilla).")
    pem_path = cands[0]

pem = open(pem_path, "rb").read()
certs = x509.load_pem_x509_certificates(pem)
entries = []
for c in certs:
    name = c.subject.public_bytes()
    key = c.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    entries.append((name, key))
entries.sort(key=lambda e: e[0])               # búsqueda binaria por subject en el firmware

bundle = struct.pack(">H", len(entries))
for name, key in entries:
    bundle += struct.pack(">HH", len(name), len(key)) + name + key

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "wb").write(bundle)
print(f"{len(entries)} CAs de {os.path.basename(pem_path)} -> {os.path.normpath(OUT)} ({len(bundle)} bytes, formato v1)")
