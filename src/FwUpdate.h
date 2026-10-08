/**
 * @file    FwUpdate.h
 * @brief   Actualización del firmware por un enlace de texto (USB CDC o serie):
 *          comandos #FW. El firmware en marcha recibe la imagen en la copia
 *          libre; el cargador (AbBootloader) la arranca a prueba y vuelve a la
 *          anterior si no se confirma.
 *
 *   #FW                               → #FW INFO SLOT=A STATE=OK TRIES=0 VER=… FREE=B MAX=131072 PROD=…
 *   #FW BEGIN <tamaño> <crc32> <A|B>  → #FW READY             (borra la copia libre: 1–2 s)
 *   #FW DATA <posición> <hex>         → #FW OK <siguiente>    (hasta 128 bytes; posiciones en hex, 8 cifras)
 *   #FW END                           → #FW VERIFIED VER=<versión> (CRC32, producto y dirección de la copia)
 *   #FW APPLY                         → #FW REBOOT            (arrancará la copia nueva a prueba)
 *   #FW CONFIRM                       → #FW CONFIRMED         (la copia en marcha queda fija)
 *   #FW ABORT                         → #FW ABORTED
 * Errores: #FW ERR NOBOOT|SIZE|SLOT|SEQ <pos>|WRITE|CRC|PRODUCT|STATE|ARG.
 *   STATE en BEGIN: la copia en marcha está a prueba (confirmar antes).
 *   (El proyecto puede responder #FW ERR BUSY por su cuenta, p. ej. con el bus ocupado.)
 * Una sesión sin comandos durante AB_FW_IDLE_MS se cancela sola (el siguiente DATA → ERR STATE).
 *
 * Uso en un proyecto:
 *   1. Definir la cabecera de la imagen, una vez:  AB_FW_HEADER("MI-PRODUCTO", FW_VERSION);
 *   2. Compilar para la copia A y para la B (-DAB_RUN_SLOT=0 / 1 y la dirección
 *      de enlace; ver README).
 *   3. Al arrancar: FwUpdate_begin(ab_platformF4(...))  (AbPlatformF4.h).
 *   4. Cada línea recibida que cumpla FwUpdate_isFw() → FwUpdate_line(); y
 *      FwUpdate_poll() en loop(). Mientras FwUpdate_busy(), no usar lo que
 *      no deba coincidir con una actualización.
 */

#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stdint.h>
#include "AbConfig.h"
#include "BootState.h"

#define AB_HDR_MAGIC     "AB-BOOT-FW-HDR-1"   // 16 caracteres, sin '\0'
#define AB_FW_CHUNK_MAX  128                  // Bytes por #FW DATA (línea de ≈ 280 caracteres)
#ifndef AB_FW_IDLE_MS
#define AB_FW_IDLE_MS    30000                // Sesión sin comandos → se cancela
#endif

/// Cabecera dentro de cada imagen (la busca #FW END por su marca): 160 bytes.
struct FwHeader {
    char     magic[16];        ///< AB_HDR_MAGIC
    char     product[24];      ///< Producto, rellenado con '\0' (una imagen de otro producto se rechaza)
    uint32_t linkAddr;         ///< Dirección para la que se compiló (copia A o B)
    uint32_t hdrVersion;       ///< 1
    char     version[48];      ///< Versión (p. ej. git describe)
    uint8_t  signature[64];    ///< Reservado para una firma digital (más adelante)
};
static_assert(sizeof(FwHeader) == 160, "FwHeader debe medir 160 bytes");

/// La cabecera de ESTA imagen: la define el proyecto con AB_FW_HEADER().
extern "C" const FwHeader ab_fwHeader;

#define AB_FW_HEADER(product, version)                                                    \
    extern "C" __attribute__((used)) const FwHeader ab_fwHeader = {                       \
        {'A','B','-','B','O','O','T','-','F','W','-','H','D','R','-','1'},                \
        product, AB_LINK_ADDR, 1, version, {0}}

struct FwPlatform {
    const uint8_t* slot[2];    ///< Dónde se lee cada copia
    uint32_t       slotLink[2];///< Dirección de cada copia en el micro (para la cabecera)
    uint32_t       slotSize;
    BsFlash        state;      ///< Sectores de estado
    int            running;    ///< Copia en marcha (0/1), -1 = sin cargador
    bool (*eraseSlot)(int slot);
    bool (*program)(const uint8_t* addr, const void* data, uint32_t len);
    void (*watchdog)(uint32_t ms);   ///< Cambiar el tiempo del watchdog (borrar para la CPU 1–2 s)
    void (*reboot)();
    uint32_t (*nowMs)();
    void (*out)(const char* line);   ///< Escribe una línea de respuesta (sin '\n')
    void (*flush)();                 ///< Espera a que salga lo escrito (antes de reiniciar)
};

/// Al arrancar: si hay cargador y no hay estado, crea el inicial (copia en marcha, confirmada).
void FwUpdate_begin(const FwPlatform* p);

/// Una línea "#FW…" completa (sin '\n'): escribe exactamente una respuesta "#FW …".
void FwUpdate_line(const char* line);

/// ¿La línea es un comando #FW? (#FW, #fw, seguido de espacio o fin)
bool FwUpdate_isFw(const char* line);

/// Sesión abierta (BEGIN…APPLY).
bool FwUpdate_busy();

/// Cancela la sesión si lleva AB_FW_IDLE_MS sin comandos. Llamar en loop().
void FwUpdate_poll();

#endif
