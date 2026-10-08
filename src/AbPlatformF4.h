/**
 * @file    AbPlatformF4.h
 * @brief   FwPlatform para el STM32F4 real: copias y estado en la flash interna
 *          (AbConfig.h), copia en marcha según la tabla de vectores (VTOR) que
 *          puso el cargador. El proyecto aporta watchdog, reinicio, reloj y salida.
 */

#ifndef AB_PLATFORM_F4_H
#define AB_PLATFORM_F4_H

#include <stdint.h>
#include "FwUpdate.h"

struct AbHooks {
    void (*watchdog)(uint32_t ms);   ///< ms = tiempo nuevo; 0 = volver al normal
    void (*reboot)();
    uint32_t (*nowMs)();
    void (*out)(const char* line);   ///< Una línea de respuesta (sin '\n')
    void (*flush)();
};

/// Plataforma lista para FwUpdate_begin(). running = -1 si no hay cargador.
const FwPlatform* ab_platformF4(const AbHooks& hooks);

#endif
