/**
 * @file    AbBootloader.h
 * @brief   Cargador A/B (sector 0). Se compila sólo con -DAB_BOOTLOADER, en un
 *          proyecto PlatformIO aparte sin framework (ver bootloader/ y README).
 *
 * En cada arranque (milisegundos, sin USB ni relojes nuevos):
 *   1. ab_bootloader_board_init() (débil: el proyecto la redefine para dejar
 *      sus pines en un estado seguro, p. ej. el DE de un RS-485 a 0).
 *   2. Lee el último registro de estado (sectores AB_STATE0/1).
 *   3. Copia a prueba: cuenta el intento; a los AB_MAX_TRIES sin confirmar
 *      vuelve a la anterior (bs_decide).
 *   4. Comprueba la copia: vectores razonables y, si se conoce, tamaño + CRC32;
 *      si está mal, prueba la otra.
 *   5. Guarda el registro si ha cambiado algo y salta. Si la copia está a prueba,
 *      antes activa el watchdog (≈ AB_TRIAL_WDG_MS): una versión que se cuelgue
 *      antes de activar el suyo también reinicia y gasta sus intentos.
 *   6. Si ninguna vale: salta al cargador de fábrica del STM32 (DFU por USB).
 * No toca RCC_CSR: el firmware sigue viendo el motivo del reinicio.
 */

#ifndef AB_BOOTLOADER_H
#define AB_BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/// Decide y salta. No vuelve.
[[noreturn]] void ab_bootloader_run(void);

/// Opcional (débil): pines del proyecto a un estado seguro antes de decidir.
void ab_bootloader_board_init(void);

#ifdef __cplusplus
}
#endif

#endif
