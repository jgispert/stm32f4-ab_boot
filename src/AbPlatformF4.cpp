/**
 * @file    AbPlatformF4.cpp
 * @brief   Ver AbPlatformF4.h. Sólo en el micro.
 */

#if defined(__arm__) && !defined(AB_BOOTLOADER)

#include "AbPlatformF4.h"
#include "FlashF4.h"

namespace {

bool program(const uint8_t* addr, const void* data, uint32_t len) {
    return flash_program(reinterpret_cast<uint32_t>(addr), data, len);
}
bool eraseState(int which) { return flash_eraseSector(which ? AB_STATE1_SECTOR : AB_STATE0_SECTOR); }
bool eraseSlot(int slot) { return flash_eraseSector(flash_sectorOf(slot ? AB_SLOT_B_ADDR : AB_SLOT_A_ADDR)); }

int runningSlot() {
    const uint32_t vtor = *reinterpret_cast<volatile uint32_t*>(0xE000ED08u);   // SCB->VTOR
    if (vtor == AB_SLOT_A_ADDR) return 0;
    if (vtor == AB_SLOT_B_ADDR) return 1;
    return -1;                                    // Sin cargador (imagen en 0x0800 0000)
}

}  // namespace

const FwPlatform* ab_platformF4(const AbHooks& h) {
    static FwPlatform p;
    p.slot[0] = reinterpret_cast<const uint8_t*>(AB_SLOT_A_ADDR);
    p.slot[1] = reinterpret_cast<const uint8_t*>(AB_SLOT_B_ADDR);
    p.slotLink[0] = AB_SLOT_A_ADDR;
    p.slotLink[1] = AB_SLOT_B_ADDR;
    p.slotSize = AB_SLOT_SIZE;
    p.state.sector[0] = reinterpret_cast<const uint8_t*>(AB_STATE0_ADDR);
    p.state.sector[1] = reinterpret_cast<const uint8_t*>(AB_STATE1_ADDR);
    p.state.sectorSize = AB_STATE_SIZE;
    p.state.program = program;
    p.state.erase = eraseState;
    p.running = runningSlot();
    p.eraseSlot = eraseSlot;
    p.program = program;
    p.watchdog = h.watchdog;
    p.reboot = h.reboot;
    p.nowMs = h.nowMs;
    p.out = h.out;
    p.flush = h.flush;
    return &p;
}

#endif
