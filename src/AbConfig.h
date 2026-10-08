/**
 * @file    AbConfig.h
 * @brief   Mapa de memoria del arranque A/B. Valores por defecto para el
 *          STM32F411CE (512 KB); un proyecto puede cambiarlos definiendo las
 *          macros antes (build_flags) o en un "AbBootConfig.h" propio.
 *
 *   Sector 0 (16 KB)   0x0800 0000  cargador (nunca se reescribe)
 *   Sector 1 (16 KB)   0x0800 4000  estado (alterno)
 *   Sector 2 (16 KB)   0x0800 8000  estado (alterno)
 *   Sectores 3–4                     libres
 *   Sector 5 (128 KB)  0x0802 0000  copia A
 *   Sector 6 (128 KB)  0x0804 0000  copia B
 *   Sector 7 (128 KB)                libre
 */

#ifndef AB_CONFIG_H
#define AB_CONFIG_H

#if defined(__has_include)
#if __has_include("AbBootConfig.h")
#include "AbBootConfig.h"
#endif
#endif

#ifndef AB_BL_ADDR
#define AB_BL_ADDR          0x08000000u
#endif
#ifndef AB_STATE0_ADDR
#define AB_STATE0_ADDR      0x08004000u
#endif
#ifndef AB_STATE1_ADDR
#define AB_STATE1_ADDR      0x08008000u
#endif
#ifndef AB_STATE0_SECTOR
#define AB_STATE0_SECTOR    1
#endif
#ifndef AB_STATE1_SECTOR
#define AB_STATE1_SECTOR    2
#endif
#ifndef AB_STATE_SIZE
#define AB_STATE_SIZE       0x4000u
#endif
#ifndef AB_SLOT_A_ADDR
#define AB_SLOT_A_ADDR      0x08020000u
#endif
#ifndef AB_SLOT_B_ADDR
#define AB_SLOT_B_ADDR      0x08040000u
#endif
#ifndef AB_SLOT_SIZE
#define AB_SLOT_SIZE        0x20000u
#endif
#ifndef AB_RAM_START
#define AB_RAM_START        0x20000000u
#endif
#ifndef AB_RAM_END
#define AB_RAM_END          0x20020000u
#endif
#ifndef AB_SYSMEM_ADDR
#define AB_SYSMEM_ADDR      0x1FFF0000u   // Cargador de fábrica (DFU por USB)
#endif
#ifndef AB_MAX_TRIES
#define AB_MAX_TRIES        3u            // Arranques a prueba sin confirmar antes de volver
#endif
#ifndef AB_TRIAL_WDG_MS
#define AB_TRIAL_WDG_MS     8000u         // Watchdog que pone el cargador a una copia a prueba
#endif

/// Copia para la que se compila esta imagen: build_flags -DAB_RUN_SLOT=0 (A) o 1 (B).
/// Sin definir: imagen sin cargador (en 0x0800 0000, para depurar).
#ifndef AB_RUN_SLOT
#define AB_RUN_SLOT         -1
#endif
#if AB_RUN_SLOT == 0
#define AB_LINK_ADDR        AB_SLOT_A_ADDR
#elif AB_RUN_SLOT == 1
#define AB_LINK_ADDR        AB_SLOT_B_ADDR
#else
#define AB_LINK_ADDR        AB_BL_ADDR
#endif

#endif
