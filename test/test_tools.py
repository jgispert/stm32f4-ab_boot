#!/usr/bin/env python3
"""Pruebas en PC de tools/fw_package.py con imágenes sintéticas."""
import json
import os
import struct
import sys
import tempfile
import zipfile
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import fw_package as fp  # noqa: E402

fails = 0


def check(cond, msg):
    global fails
    print(f"  {'ok' if cond else 'FALLO'}: {msg}")
    fails += 0 if cond else 1


def image(link, product="PRUEBA", version="v1.2.3", size=4000):
    img = bytearray((i * 7 + 3) & 0xFF for i in range(size))
    h = fp.MAGIC + struct.pack("<24sII48s64s", product.encode(), link, 1, version.encode(), b"")
    img[0x200:0x200 + len(h)] = h
    return bytes(img)


def expect_exit(fn, text, msg):
    try:
        fn()
        check(False, msg + " (no falló)")
    except SystemExit as e:
        check(text in str(e), f"{msg}: {e}")


with tempfile.TemporaryDirectory() as d:
    def put(name, data):
        p = os.path.join(d, name)
        open(p, "wb").write(data)
        return p

    a = put("a.bin", image(0x08020000))
    b = put("b.bin", image(0x08040000))
    print("1) Paquete correcto")
    check(fp.header(open(a, "rb").read())["offset"] == 0x200, "cabecera encontrada")
    man, files = fp.build({"A": a, "B": b})
    out = fp.write(man, files, os.path.join(d, "dist"))
    check(os.path.basename(out) == "PRUEBA-v1.2.3.fwu", os.path.basename(out))
    z = zipfile.ZipFile(out)
    m = json.loads(z.read("manifest.json"))
    check(m["product"] == "PRUEBA" and m["version"] == "v1.2.3", "manifest: producto y versión")
    check(m["slots"]["B"]["crc32"] == f"{zlib.crc32(z.read('slot_b.bin')) & 0xFFFFFFFF:08X}", "CRC32 de B")
    check(m["slots"]["A"]["link"] == "0x08020000", "dirección de A")

    print("2) Errores")
    expect_exit(lambda: fp.build({"A": b, "B": a}), "compilado para", "copias cambiadas")
    c = put("c.bin", image(0x08040000, version="v9"))
    expect_exit(lambda: fp.build({"A": a, "B": c}), "Versiones distintas", "versiones distintas")
    e = put("e.bin", image(0x08040000, product="OTRO"))
    expect_exit(lambda: fp.build({"A": a, "B": e}), "producto", "producto distinto")
    expect_exit(lambda: fp.build({"A": a, "B": b}, product="X"), "producto", "--product distinto")
    n = put("n.bin", bytes(3000))
    expect_exit(lambda: fp.build({"A": n, "B": b}), "sin cabecera", "sin cabecera")
    big = put("big.bin", image(0x08040000, size=0x20004))
    expect_exit(lambda: fp.build({"A": a, "B": big}), "no cabe", "demasiado grande")

print("\nTODO OK (0 fallos)" if not fails else f"\nHAY FALLOS ({fails})")
sys.exit(1 if fails else 0)
