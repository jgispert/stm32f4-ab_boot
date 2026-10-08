# stm32f4-ab_boot

Arranque **A/B** y **actualización del firmware por un enlace de texto** (USB CDC o
puerto serie) para el STM32F4. Pensado para equipos instalados lejos: la versión
nueva se graba en la copia libre mientras la actual sigue funcionando, arranca
**a prueba** y, si nadie la confirma, el cargador **vuelve sola a la anterior**.

- Cargador de ≈ 2 KB en el sector 0, sin framework ni HAL. Nunca se reescribe.
- Estado en flash con CRC32: un corte de luz en cualquier momento deja el equipo arrancable.
- Protocolo `#FW` de líneas de texto: se integra en cualquier intérprete de comandos.
- Flash a nivel de registro (sin HAL): vale con Arduino (STM32duino), STM32Cube o sin framework.
- Herramientas en Python para empaquetar (`.fwu`) y enviar.

Probada en la Black Pill STM32F411CE (512 KB). Otro F4: cambiar el mapa de memoria (abajo).

## Mapa de memoria (por defecto, STM32F411CE)

| Sector | Dirección    | Tamaño | Uso |
|--------|--------------|--------|-----|
| 0      | 0x0800 0000  | 16 KB  | Cargador |
| 1      | 0x0800 4000  | 16 KB  | Estado (alterno) |
| 2      | 0x0800 8000  | 16 KB  | Estado (alterno) |
| 3–4    | 0x0800 C000  | 80 KB  | Libres (datos del proyecto) |
| 5      | 0x0802 0000  | 128 KB | Copia A |
| 6      | 0x0804 0000  | 128 KB | Copia B |
| 7      | 0x0806 0000  | 128 KB | Libre |

Todo está en `src/AbConfig.h` como macros `AB_*` con `#ifndef`: se cambian con
`build_flags` o en un `AbBootConfig.h` propio (se incluye solo si existe). El
cargador y el firmware deben usar **los mismos** valores.

## Cómo funciona

```
#FW BEGIN → borra la copia libre      #FW APPLY → estado TRIAL, reinicio
#FW DATA × n → la graba               cargador → arranca la nueva (intento 1 de 3)
#FW END → CRC32, producto, dirección  app → #FW → STATE=TRIAL → #FW CONFIRM → OK
```

Sin `#FW CONFIRM`, cada reinicio gasta un intento; a los `AB_MAX_TRIES` (3) el
cargador vuelve a la copia anterior. Mientras está a prueba, el cargador deja el
watchdog activado (`AB_TRIAL_WDG_MS`, 8 s): una versión que se cuelgue antes de
poner el suyo también reinicia. Si ninguna copia es válida, salta al cargador de
fábrica del STM32 (DFU por USB).

### Protocolo

| Envío | Respuesta |
|-------|-----------|
| `#FW` | `#FW INFO SLOT=A STATE=OK TRIES=0 VER=… FREE=B MAX=131072 PROD=…` |
| `#FW BEGIN <tamaño> <crc32 hex> <A\|B>` | `#FW READY` (tras borrar, 1–2 s) |
| `#FW DATA <posición hex 8> <hex, ≤ 128 bytes>` | `#FW OK <siguiente posición>` |
| `#FW END` | `#FW VERIFIED VER=<versión>` |
| `#FW APPLY` | `#FW REBOOT` y reinicio |
| `#FW CONFIRM` | `#FW CONFIRMED` |
| `#FW ABORT` | `#FW ABORTED` |

Errores: `#FW ERR NOBOOT|SIZE|SLOT|SEQ <pos>|WRITE|CRC|PRODUCT|STATE|ARG`.
`STATE` en BEGIN = la versión en marcha está a prueba (confirmar antes). Una
sesión sin comandos durante 30 s (`AB_FW_IDLE_MS`) se cancela.
Sin el cargador (imagen en 0x0800 0000), `#FW` responde `SLOT=NONE`.

## Uso en un proyecto PlatformIO (Arduino)

Ver `examples/serial` completo. Lo esencial:

```ini
lib_deps = https://github.com/jgispert/stm32f4-ab_boot.git#v0.1.0

[env:slot_a]
build_flags = ${env.build_flags} -DAB_RUN_SLOT=0
board_build.flash_offset  = 0x20000
board_upload.maximum_size = 262144        ; offset + 128 KB (ldscript de STM32duino)

[env:slot_b]
build_flags = ${env.build_flags} -DAB_RUN_SLOT=1
board_build.flash_offset  = 0x40000
board_upload.maximum_size = 393216
```

```cpp
#include "FwUpdate.h"
#include "AbPlatformF4.h"

AB_FW_HEADER("MI-PRODUCTO", "v1.0.0");    // Una vez: producto (≤ 23) y versión (≤ 47)

void setup() {
    FwUpdate_begin(ab_platformF4({wdg, reboot, millis_, out, flush}));
}
// Cada línea recibida:  if (FwUpdate_isFw(l)) FwUpdate_line(l);
// En loop():            FwUpdate_poll();
// Mientras FwUpdate_busy(): no hacer lo que no deba coincidir con una actualización.
```

Ganchos (`AbHooks`): `watchdog(ms)` cambia el tiempo del watchdog (0 = el normal;
borrar una copia para la CPU 1–2 s), `reboot()`, `nowMs()`, `out(línea)` y `flush()`.

**Cargador:** copiar `bootloader/` al proyecto y poner en `lib_deps` la URL de
git. Para dejar pines en un estado seguro antes de saltar (p. ej. el DE de un
RS-485), redefinir `extern "C" void ab_bootloader_board_init()`.

### Primera instalación (ST-Link, una vez por placa)

```
pio run -d bootloader -t upload
pio run -e slot_a -t upload
```

Los `upload_command` de los ejemplos graban antes `tools/state_blank.bin` en el
sector de estado: un estado viejo de otra prueba no debe decidir el arranque.

### Versiones nuevas

```
pio run -e slot_a -e slot_b
python3 tools/fw_package.py --slot-a .pio/build/slot_a/firmware.bin --slot-b .pio/build/slot_b/firmware.bin
python3 tools/fw_update.py dist/MI-PRODUCTO-v1.0.1.fwu --vid-pid 0483:5740
```

El `.fwu` es un ZIP con `slot_a.bin`, `slot_b.bin` y `manifest.json` (producto,
versión, tamaño, CRC32 y dirección de cada copia). Quien actualiza pregunta con
`#FW` qué copia está libre y envía ésa.

## Pruebas

```
./test/run.sh        # estado A/B, #FW completo con una flash en RAM, herramientas
```

CI (GitHub Actions): pruebas en PC, cargador (< 16 KB) y el ejemplo para las dos copias + `.fwu`.

## Licencia

Joan Gispert — TPC NetGrup.
