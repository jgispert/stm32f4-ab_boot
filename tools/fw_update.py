#!/usr/bin/env python3
"""
fw_update.py — Actualiza por #FW (stm32f4-ab_boot) un equipo conectado por un
puerto serie / USB CDC. Es también la referencia para otras implementaciones
(app, monitor serie): mismos pasos y mismas comprobaciones.

    python3 fw_update.py fichero.fwu --port /dev/ttyACM0
    python3 fw_update.py fichero.fwu --vid-pid 0483:5740 --pre "#MODE BRIDGE"
    python3 fw_update.py fichero.fwu --no-confirm      # dejar la versión nueva a prueba
    python3 fw_update.py fichero.fwu --confirm-only    # confirmar la versión a prueba
    python3 fw_update.py fichero.fwu --corrupt         # un byte cambiado → ERR CRC
    python3 fw_update.py fichero.fwu --swap            # la copia equivocada → ERR SLOT
    python3 fw_update.py fichero.fwu --stop-at 50      # cortar a mitad (50 %)

Pasos: #FW (qué copia está libre) → BEGIN → DATA × n → END → APPLY → el equipo
se reinicia → reabrir el puerto → #FW (debe decir la copia nueva, STATE=TRIAL y
la versión nueva) → #FW CONFIRM.

Las líneas que no empiezan por "#FW" (avisos del propio equipo) se ignoran.
"""
import argparse
import json
import os
import sys
import time
import zipfile

import serial                                   # pip install pyserial
from serial.tools import list_ports

CHUNK = 128


class Link:
    def __init__(self, port, baud=115200):
        self.s = serial.Serial(port, baud, timeout=0.05)
        self.buf = b""

    def drain(self, t=0.2):
        end = time.monotonic() + t
        while time.monotonic() < end:
            self.s.read(4096)
        self.buf = b""

    def cmd(self, line, timeout=2.0):
        """Envía una línea y devuelve la primera respuesta '#FW…' (o None)."""
        self.s.write((line + "\n").encode())
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                ln, self.buf = self.buf.split(b"\n", 1)
                ln = ln.decode(errors="replace").strip()
                if ln.upper().startswith("#FW"):
                    return ln
        return None

    def close(self):
        self.s.close()


def find_port(vid_pid):
    if not vid_pid:
        return None
    vid, pid = (int(x, 16) for x in vid_pid.split(":"))
    for p in list_ports.comports():
        if p.vid == vid and p.pid == pid:
            return p.device
    return None


def info(link):
    ln = link.cmd("#FW", 1.0)
    if not ln or not ln.startswith("#FW INFO"):
        sys.exit(f"#FW → {ln!r}: ¿el firmware no lleva stm32f4-ab_boot?")
    return dict(t.split("=", 1) for t in ln.split()[2:] if "=" in t), ln


def reopen(a, port, t_max=20):
    """Espera a que el puerto desaparezca (reinicio) y vuelva."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < 5 and os.path.exists(port):
        time.sleep(0.05)
    print(f"  puerto desaparecido a los {time.monotonic() - t0:.1f} s")
    while time.monotonic() - t0 < t_max:
        time.sleep(0.5)
        p = find_port(a.vid_pid) or port
        try:
            link = Link(p, a.baud)
            link.drain()
            if link.cmd("#FW", 0.5):
                print(f"  de vuelta a los {time.monotonic() - t0:.1f} s en {p}")
                return link
            link.close()
        except (serial.SerialException, OSError):
            pass
    sys.exit(f"El equipo no ha vuelto en {t_max} s: si la versión nueva no arranca, el cargador "
             "vuelve a la anterior tras los reinicios a prueba; compruébalo con #FW")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fwu", help="fichero .fwu (fw_package.py)")
    ap.add_argument("--port", help="puerto serie")
    ap.add_argument("--vid-pid", help="buscar el puerto por VID:PID en hex (p. ej. 0483:5740)")
    ap.add_argument("--baud", type=int, default=115200, help="velocidad (USB CDC: no importa)")
    ap.add_argument("--pre", action="append", default=[], help="línea a enviar antes (repetible)")
    ap.add_argument("--no-confirm", action="store_true", help="no enviar #FW CONFIRM")
    ap.add_argument("--confirm-only", action="store_true", help="sólo confirmar la versión a prueba")
    ap.add_argument("--corrupt", action="store_true", help="cambiar un byte (prueba de ERR CRC)")
    ap.add_argument("--swap", action="store_true", help="enviar la imagen de la otra copia (ERR SLOT)")
    ap.add_argument("--stop-at", type=int, default=None, help="dejar de enviar al N %% (corte)")
    a = ap.parse_args()

    z = zipfile.ZipFile(a.fwu)
    man = json.loads(z.read("manifest.json"))
    print(f"Fichero: {man['product']} {man['version']}")

    port = a.port or find_port(a.vid_pid)
    if not port:
        sys.exit("Indica --port o --vid-pid")
    link = Link(port, a.baud)
    link.drain()
    for pre in a.pre:
        link.s.write((pre + "\n").encode())
    link.drain()
    inf, line = info(link)
    print(f"Equipo:  {line}")
    if a.confirm_only:
        ln = link.cmd("#FW CONFIRM")
        print(f"> #FW CONFIRM  < {ln}")
        return 0 if ln == "#FW CONFIRMED" else 1
    if inf.get("SLOT") in (None, "NONE"):
        sys.exit("El equipo no tiene cargador (se grabó sin él): primera instalación con el programador")
    if inf.get("PROD") != man["product"]:
        sys.exit(f"Producto del equipo {inf.get('PROD')!r} y del fichero {man['product']!r} distintos")
    free = inf["FREE"]
    send_slot = ("A" if free == "B" else "B") if a.swap else free
    s = man["slots"][send_slot]
    img = bytearray(z.read(s["file"]))
    if a.corrupt:
        img[len(img) // 2] ^= 0x40
    print(f"Envío:   copia {send_slot}.bin a la copia libre {free} ({len(img)} bytes, CRC32 {s['crc32']})")

    t0 = time.monotonic()
    ln = link.cmd(f"#FW BEGIN {len(img)} {s['crc32']} {free}", 10.0)
    print(f"> #FW BEGIN …  < {ln}   ({time.monotonic() - t0:.1f} s, borrado incluido)")
    if ln != "#FW READY":
        if ln == "#FW ERR STATE" and inf.get("STATE") == "TRIAL":
            print("La versión en marcha está A PRUEBA: confírmala antes (--confirm-only) "
                  "o reinicia el equipo para que vuelva a la anterior")
        sys.exit(1)
    stop = len(img) * a.stop_at // 100 if a.stop_at is not None else None
    off, lines = 0, 0
    t1 = time.monotonic()
    while off < len(img):
        if stop is not None and off >= stop:
            print(f"Corte simulado en {off} de {len(img)} bytes: sin END ni APPLY; "
                  "el equipo sigue con la versión anterior")
            return 0
        chunk = bytes(img[off:off + CHUNK])
        ln = link.cmd(f"#FW DATA {off:08X} {chunk.hex()}")
        if ln == f"#FW OK {off + len(chunk):08X}":
            off += len(chunk)
            lines += 1
            if lines % 50 == 0:
                print(f"  {100 * off // len(img):3d} %  {off} bytes", end="\r")
            continue
        if ln and ln.startswith("#FW ERR SEQ "):           # Seguir desde donde dice el equipo
            off = int(ln.split()[3], 16)
            continue
        sys.exit(f"\n#FW DATA {off:08X} → {ln!r}")
    dt = time.monotonic() - t1
    print(f"  100 %  {off} bytes en {lines} líneas, {dt:.1f} s ({off / dt / 1024:.1f} KB/s)")

    ln = link.cmd("#FW END", 5.0)
    print(f"> #FW END  < {ln}")
    if not ln or not ln.startswith("#FW VERIFIED"):
        print("No se aplica: el equipo sigue con su versión")
        link.cmd("#FW ABORT", 1.0)
        return 1
    ln = link.cmd("#FW APPLY")
    print(f"> #FW APPLY  < {ln}")
    if ln != "#FW REBOOT":
        return 1
    link.close()
    link = reopen(a, port)
    for pre in a.pre:
        link.s.write((pre + "\n").encode())
    link.drain()
    inf, line = info(link)
    print(f"< {line}")
    ok = inf.get("SLOT") == free and inf.get("STATE") == "TRIAL" and inf.get("VER") == man["version"]
    if not ok:
        print("RESULTADO: el equipo NO arrancó la versión nueva (el cargador volvió a la anterior)")
        return 1
    if a.no_confirm:
        print("RESULTADO: versión nueva A PRUEBA, sin confirmar: tras los reinicios a prueba volverá a la anterior")
        return 0
    ln = link.cmd("#FW CONFIRM")
    print(f"> #FW CONFIRM  < {ln}")
    print(f"RESULTADO: {'ACTUALIZADO' if ln == '#FW CONFIRMED' else 'SIN CONFIRMAR'} a {man['version']} "
          f"en la copia {free}, {time.monotonic() - t0:.0f} s en total")
    return 0 if ln == "#FW CONFIRMED" else 1


if __name__ == "__main__":
    sys.exit(main())
