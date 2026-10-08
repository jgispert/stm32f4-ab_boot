/**
 * @file    FlashF4.cpp
 * @brief   Ver FlashF4.h. Registros: RM0383 (STM32F411), capítulo 3.
 */

#include "FlashF4.h"
#if defined(__arm__)                       // Sólo en el micro (las pruebas de PC no lo compilan)
#include <string.h>

namespace {
struct FlashRegs {
    volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, OPTCR;
};
FlashRegs* const FL = reinterpret_cast<FlashRegs*>(0x40023C00u);

const uint32_t KEY1 = 0x45670123u, KEY2 = 0xCDEF89ABu;
const uint32_t SR_EOP = 1u << 0, SR_ERRS = 0xF2u;      // OPERR, WRPERR, PGAERR, PGPERR, PGSERR
const uint32_t SR_BSY = 1u << 16;
const uint32_t CR_PG = 1u << 0, CR_SER = 1u << 1, CR_STRT = 1u << 16, CR_LOCK = 1u << 31;
const uint32_t CR_PSIZE_X32 = 2u << 8;
const uint32_t ACR_ICEN = 1u << 9, ACR_DCEN = 1u << 10, ACR_ICRST = 1u << 11, ACR_DCRST = 1u << 12;

void unlock() {
    if (FL->CR & CR_LOCK) { FL->KEYR = KEY1; FL->KEYR = KEY2; }
}
void lock() { FL->CR |= CR_LOCK; }
bool waitIdle() {
    while (FL->SR & SR_BSY) { }
    const bool ok = (FL->SR & SR_ERRS) == 0;
    FL->SR = SR_EOP | SR_ERRS;                         // Se borran escribiendo 1
    return ok;
}
/// Antes de empezar: espera y borra errores antiguos (no son de esta operación).
void prepare() {
    while (FL->SR & SR_BSY) { }
    FL->SR = SR_EOP | SR_ERRS;
}
}  // namespace

extern "C" int flash_sectorOf(uint32_t addr) {
    if (addr < 0x08000000u || addr >= 0x08080000u) return -1;
    const uint32_t off = addr - 0x08000000u;
    if (off < 0x10000u) return static_cast<int>(off / 0x4000u);   // 0–3: 16 KB
    if (off < 0x20000u) return 4;                                 // 4: 64 KB
    return static_cast<int>(5 + (off - 0x20000u) / 0x20000u);    // 5–7: 128 KB
}

extern "C" bool flash_eraseSector(int sector) {
    if (sector < 1 || sector > 7) return false;      // El sector 0 (cargador) nunca se borra
    unlock();
    prepare();
    FL->CR = CR_PSIZE_X32 | CR_SER | (static_cast<uint32_t>(sector) << 3);
    FL->CR |= CR_STRT;
    const bool ok = waitIdle();
    FL->CR = 0;
    lock();
    flash_cacheFlush();
    return ok;
}

extern "C" bool flash_program(uint32_t addr, const void* data, uint32_t len) {
    if ((addr & 3u) || (len & 3u)) return false;
    if (flash_sectorOf(addr) < 1 || flash_sectorOf(addr + len - 1) < 1) return false;   // Ni el cargador ni fuera
    const uint8_t* src = static_cast<const uint8_t*>(data);
    unlock();
    prepare();
    bool ok = true;
    FL->CR = CR_PSIZE_X32 | CR_PG;
    for (uint32_t i = 0; ok && i < len; i += 4) {
        uint32_t w;
        memcpy(&w, src + i, 4);
        *reinterpret_cast<volatile uint32_t*>(addr + i) = w;
        ok = waitIdle();
    }
    FL->CR = 0;
    lock();
    flash_cacheFlush();
    return ok && memcmp(reinterpret_cast<const void*>(addr), data, len) == 0;
}

extern "C" void flash_cacheFlush(void) {
    const uint32_t acr = FL->ACR;
    FL->ACR = acr & ~(ACR_ICEN | ACR_DCEN);
    FL->ACR = (acr & ~(ACR_ICEN | ACR_DCEN)) | ACR_ICRST | ACR_DCRST;
    FL->ACR = acr & ~(ACR_ICRST | ACR_DCRST);
}

#endif  // __arm__
