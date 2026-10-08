/**
 * @file    BootState.h
 * @brief   Estado del arranque A/B: registros, decisión del cargador y CRC32. Lógica pura: sin Arduino ni registros del micro;
 *          la flash se maneja con funciones que pone quien la usa (el cargador,
 *          el firmware o una flash simulada en las pruebas de PC).
 *
 * Estado = registros de 64 bytes AÑADIDOS uno tras otro en dos sectores que se
 * alternan (1 y 2). Vale el registro correcto (marca + CRC) con el número de
 * secuencia más alto. Un corte de luz al escribir deja un registro incompleto
 * que no pasa el CRC y se ignora: sigue valiendo el anterior.
 *
 * Copias: 0 = A, 1 = B. Mapa de memoria en AbConfig.h.
 */

#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "AbConfig.h"

#define BS_MAGIC        0x53425356u   // "VSBS" en memoria
#define BS_RECORD_SIZE  64u
#define BS_MAX_TRIES    AB_MAX_TRIES  // Arranques a prueba sin confirmar antes de volver
#define BS_SLOT_NONE    0xFFu

enum BsState : uint8_t {
    BS_STATE_OK    = 0,               ///< Copia activa confirmada
    BS_STATE_TRIAL = 1,               ///< Copia activa a prueba (falta #FW CONFIRM)
};

/// Registro de estado: 64 bytes, el CRC32 cubre los 60 anteriores.
struct BsRecord {
    uint32_t magic;
    uint32_t seq;          ///< Crece en cada registro
    uint8_t  active;       ///< Copia a arrancar (0 = A, 1 = B)
    uint8_t  state;        ///< BsState
    uint8_t  tries;        ///< Arranques hechos a prueba
    uint8_t  prev;         ///< Copia a la que volver si la prueba falla (BS_SLOT_NONE: ninguna)
    uint32_t size[2];      ///< Tamaño de cada copia (0 = desconocido: sólo se miran los vectores)
    uint32_t crc[2];       ///< CRC32 de cada copia
    uint8_t  reserved[32];
    uint32_t crc32;        ///< CRC32 de los 60 bytes anteriores
};
static_assert(sizeof(BsRecord) == BS_RECORD_SIZE, "BsRecord debe medir 64 bytes");

/// Flash del estado: dos sectores alternos, leídos directamente en memoria.
struct BsFlash {
    const uint8_t* sector[2];          ///< Direcciones de los dos sectores
    uint32_t       sectorSize;
    /// Graba `len` bytes (múltiplo de 4) en `addr`, que estaba borrada. true = ok.
    bool (*program)(const uint8_t* addr, const void* data, uint32_t len);
    /// Borra el sector `which` (0 o 1). true = ok.
    bool (*erase)(int which);
};

/// CRC32 (el de zlib / Python `zlib.crc32`). `crc` = valor anterior (0 al empezar).
uint32_t bs_crc32(uint32_t crc, const void* data, size_t len);

/// ¿Registro correcto (marca y CRC)?
bool bs_valid(const BsRecord* r);

/// Último registro correcto. false si no hay ninguno (placa recién grabada).
bool bs_latest(const BsFlash* f, BsRecord* out);

/// Añade `r` (le pone marca, secuencia y CRC). Si el sector se llena, pasa al
/// otro (borrándolo antes). true = grabado y comprobado.
bool bs_append(const BsFlash* f, BsRecord r);

// ── Decisión del cargador ────────────────────────────────────────────────────

/// Comprobación de una copia: tamaño + CRC32 (size 0: sólo que los vectores
/// sean razonables). La pone quien llama (lee la flash de la copia).
typedef bool (*BsSlotCheck)(int slot, uint32_t size, uint32_t crc);

struct BsDecision {
    int      slot;         ///< Copia a arrancar, o -1 = ninguna válida (DFU de fábrica)
    bool     write;        ///< Hay que guardar `record` antes de saltar
    BsRecord record;
};

/// Lo que hace el cargador en cada arranque (ver el diagrama del diseño).
BsDecision bs_decide(const BsRecord* latest /* nullptr si no hay */, BsSlotCheck check);

// ── Cambios que pide el firmware ─────────────────────────────────────────────

/// Estado inicial si no hay ninguno: la copia en marcha, confirmada.
BsRecord bs_initial(int running);

/// #FW APPLY: arrancar `slot` a prueba (la actual pasa a ser la de vuelta atrás).
BsRecord bs_trial(const BsRecord& cur, int running, int slot, uint32_t size, uint32_t crc);

/// #FW CONFIRM: la copia activa queda confirmada.
BsRecord bs_confirm(const BsRecord& cur);

#endif
