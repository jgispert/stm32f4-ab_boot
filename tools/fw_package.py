#!/usr/bin/env python3
"""
fw_package.py — Empaqueta una versión para la actualización por #FW (stm32f4-ab_boot):
un fichero <nombre>-<versión>.fwu con las dos copias del mismo firmware.

    python3 fw_package.py --slot-a .pio/build/slot_a/firmware.bin \\
                          --slot-b .pio/build/slot_b/firmware.bin  [--product MI-PRODUCTO] [--out dist]

El .fwu es un ZIP con:
    slot_a.bin, slot_b.bin   el firmware compilado para la copia A y para la B
    manifest.json            producto, versión, y tamaño + CRC32 + dirección de cada copia

Comprueba con la cabecera de cada imagen (AB_FW_HEADER, FwUpdate.h) que las dos
son del mismo producto y versión, y cada una compilada para su dirección.
"""
import argparse
import json
import os
import struct
import sys
import time
import zipfile
import zlib

MAGIC = b"AB-BOOT-FW-HDR-1"
SLOTS = {"A": 0x08020000, "B": 0x08040000}       # AB_SLOT_A_ADDR / AB_SLOT_B_ADDR por defecto
SLOT_SIZE = 0x20000


def header(img):
    """FwHeader: magic[16] product[24] linkAddr u32 hdrVersion u32 version[48] signature[64]."""
    for off in range(0, len(img) - 159, 4):
        if img[off:off + 16] == MAGIC:
            product, link, hver, version = struct.unpack_from("<24sII48s", img, off + 16)
            return {"offset": off, "product": product.split(b"\0")[0].decode(),
                    "link": link, "hdr_version": hver, "version": version.split(b"\0")[0].decode()}
    return None


def build(paths, product=None, slots=SLOTS, slot_size=SLOT_SIZE):
    """Comprueba las dos imágenes; devuelve (manifest, {fichero: datos})."""
    found, version = {}, None
    for name, addr in slots.items():
        path = paths[name]
        if not os.path.exists(path):
            raise SystemExit(f"Falta {path}")
        img = open(path, "rb").read()
        h = header(img)
        if not h:
            raise SystemExit(f"{path}: sin cabecera AB_FW_HEADER")
        if product is None:
            product = h["product"]
        if h["product"] != product:
            raise SystemExit(f"{path}: producto {h['product']!r}, se esperaba {product!r}")
        if h["link"] != addr:
            raise SystemExit(f"{path}: compilado para 0x{h['link']:08X}, la copia {name} es 0x{addr:08X}")
        if len(img) > slot_size:
            raise SystemExit(f"{path}: {len(img)} bytes, no cabe en una copia de {slot_size}")
        if version is not None and h["version"] != version:
            raise SystemExit(f"Versiones distintas: {version} y {h['version']} (compila las dos a la vez)")
        version = h["version"]
        found[name] = {"file": f"slot_{name.lower()}.bin", "size": len(img),
                       "crc32": f"{zlib.crc32(img) & 0xFFFFFFFF:08X}", "link": f"0x{addr:08X}", "data": img}
        print(f"Copia {name}: {len(img)} bytes, CRC32 {found[name]['crc32']}, "
              f"{100 * len(img) / slot_size:.0f} % de la copia")
    manifest = {"product": product, "version": version, "format": 1,
                "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
                "slots": {k: {kk: vv for kk, vv in v.items() if kk != "data"} for k, v in found.items()}}
    return manifest, {v["file"]: v["data"] for v in found.values()}


def write(manifest, files, out_dir, name=None):
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"{name or manifest['product']}-{manifest['version']}.fwu")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        for f, data in files.items():
            z.writestr(f, data)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--slot-a", required=True, help="firmware.bin compilado para la copia A")
    ap.add_argument("--slot-b", required=True, help="firmware.bin compilado para la copia B")
    ap.add_argument("--product", help="producto esperado (por defecto, el de la cabecera)")
    ap.add_argument("--name", help="prefijo del fichero (por defecto, el producto)")
    ap.add_argument("--out", default="dist", help="carpeta de salida")
    a = ap.parse_args()
    manifest, files = build({"A": a.slot_a, "B": a.slot_b}, a.product)
    if manifest["version"].endswith("-dirty"):
        print("AVISO: la versión lleva '-dirty' (cambios sin commit)")
    print(f"\n{write(manifest, files, a.out, a.name)}")


if __name__ == "__main__":
    main()
