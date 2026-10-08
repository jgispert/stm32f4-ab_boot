/**
 * @file    FlashF4.h
 * @brief   Borrado y grabación de la flash interna del STM32F4 a nivel de
 *          registro (sin HAL), para el cargador y para #FW.
 *
 * La CPU se detiene mientras la flash está ocupada (todo el programa se
 * ejecuta desde ella): un borrado de 128 KB la para 1–2 s. Quien borre una
 * copia debe alargar antes el watchdog (FwPlatform::watchdog).
 * Tras grabar, flash_cacheFlush() para que las lecturas vean lo nuevo.
 */

#ifndef FLASH_F4_H
#define FLASH_F4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Sector (0–7) que contiene `addr`, o -1. Mapa del STM32F411CE (512 KB).
int  flash_sectorOf(uint32_t addr);

/// Borra un sector entero. true = sin errores.
bool flash_eraseSector(int sector);

/// Graba `len` bytes (múltiplo de 4, `addr` alineada a 4) ya borrados.
/// true = sin errores y comprobado.
bool flash_program(uint32_t addr, const void* data, uint32_t len);

/// Vacía las cachés de instrucciones y datos (tras borrar o grabar).
void flash_cacheFlush(void);

#ifdef __cplusplus
}
#endif

#endif
