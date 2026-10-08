/**
 * @file    AbBootloader.cpp
 * @brief   Ver AbBootloader.h. Incluye el arranque mínimo (tabla de vectores,
 *          .data/.bss) para un proyecto sin framework. Sólo con -DAB_BOOTLOADER.
 */

#if defined(AB_BOOTLOADER) && defined(__arm__)

#include "AbBootloader.h"
#include <stdint.h>
#include <string.h>
#include "AbConfig.h"
#include "BootState.h"
#include "FlashF4.h"

// ── Arranque mínimo (sin CMSIS ni HAL) ───────────────────────────────────────
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern "C" int main();

extern "C" [[noreturn]] void Reset_Handler() {
    memcpy(&_sdata, &_sidata, reinterpret_cast<uintptr_t>(&_edata) - reinterpret_cast<uintptr_t>(&_sdata));
    memset(&_sbss, 0, reinterpret_cast<uintptr_t>(&_ebss) - reinterpret_cast<uintptr_t>(&_sbss));
    main();
    for (;;) { }
}

extern "C" [[noreturn]] void Default_Handler() {
    for (;;) { }
}

// Sólo las 16 excepciones del núcleo: el cargador no usa interrupciones.
extern "C" __attribute__((section(".isr_vector"), used))
const uintptr_t g_vectors[16] = {
    reinterpret_cast<uintptr_t>(&_estack),
    reinterpret_cast<uintptr_t>(&Reset_Handler),
    reinterpret_cast<uintptr_t>(&Default_Handler),   // NMI
    reinterpret_cast<uintptr_t>(&Default_Handler),   // HardFault
    reinterpret_cast<uintptr_t>(&Default_Handler),   // MemManage
    reinterpret_cast<uintptr_t>(&Default_Handler),   // BusFault
    reinterpret_cast<uintptr_t>(&Default_Handler),   // UsageFault
    0, 0, 0, 0,
    reinterpret_cast<uintptr_t>(&Default_Handler),   // SVC
    reinterpret_cast<uintptr_t>(&Default_Handler),   // DebugMon
    0,
    reinterpret_cast<uintptr_t>(&Default_Handler),   // PendSV
    reinterpret_cast<uintptr_t>(&Default_Handler),   // SysTick
};

// ── Cargador ─────────────────────────────────────────────────────────────────
namespace {

volatile uint32_t& reg(uint32_t addr) { return *reinterpret_cast<volatile uint32_t*>(addr); }
const uint32_t SCB_VTOR = 0xE000ED08u, SYST_CSR = 0xE000E010u;
const uint32_t RCC_APB2ENR = 0x40023844u, SYSCFG_MEMRMP = 0x40013800u;
const uint32_t IWDG_KR = 0x40003000u, IWDG_PR = 0x40003004u, IWDG_RLR = 0x40003008u, IWDG_SR = 0x4000300Cu;

uint32_t slotAddr(int slot) { return slot ? AB_SLOT_B_ADDR : AB_SLOT_A_ADDR; }

bool vectorsOk(uint32_t base, uint32_t limit) {
    const uint32_t sp = reg(base), pc = reg(base + 4);
    return sp > AB_RAM_START && sp <= AB_RAM_END && (sp & 3u) == 0
        && (pc & 1u) && (pc & ~1u) >= base && (pc & ~1u) < base + limit;
}

bool checkSlot(int slot, uint32_t size, uint32_t crc) {
    const uint32_t base = slotAddr(slot);
    if (!vectorsOk(base, AB_SLOT_SIZE)) return false;
    if (size == 0) return true;                       // Grabada con el ST-Link: sin CRC conocido
    if (size > AB_SLOT_SIZE) return false;            // (0xFFFFFFFF: se estaba escribiendo)
    return bs_crc32(0, reinterpret_cast<const void*>(base), size) == crc;
}

bool program(const uint8_t* addr, const void* data, uint32_t len) {
    return flash_program(reinterpret_cast<uint32_t>(addr), data, len);
}
bool erase(int which) { return flash_eraseSector(which ? AB_STATE1_SECTOR : AB_STATE0_SECTOR); }

void trialWatchdog() {
    reg(IWDG_KR) = 0xCCCC;                            // Arranca (enciende el LSI)
    reg(IWDG_KR) = 0x5555;
    reg(IWDG_PR) = 4;                                 // LSI / 64 ≈ 2 ms por cuenta
    uint32_t rl = AB_TRIAL_WDG_MS / 2;
    reg(IWDG_RLR) = rl > 4095 ? 4095 : rl;
    uint32_t guard = 1000000;
    while (reg(IWDG_SR) && --guard) { }
    reg(IWDG_KR) = 0xAAAA;
}

[[noreturn]] void jump(uint32_t vtor, uint32_t base) {
    reg(SYST_CSR) = 0;
    reg(SCB_VTOR) = vtor;
    const uint32_t sp = reg(base), pc = reg(base + 4);
    __asm volatile("dsb\n isb\n msr msp, %0\n bx %1" : : "r"(sp), "r"(pc) : "memory");
    __builtin_unreachable();
}

}  // namespace

extern "C" __attribute__((weak)) void ab_bootloader_board_init(void) { }

extern "C" void ab_bootloader_run(void) {
    ab_bootloader_board_init();
    static const BsFlash F = {
        {reinterpret_cast<const uint8_t*>(AB_STATE0_ADDR), reinterpret_cast<const uint8_t*>(AB_STATE1_ADDR)},
        AB_STATE_SIZE, program, erase,
    };
    BsRecord latest;
    const bool have = bs_latest(&F, &latest);
    const BsDecision d = bs_decide(have ? &latest : nullptr, checkSlot);
    if (d.write) bs_append(&F, d.record);             // Si falla, se arranca igual
    if (d.slot >= 0) {
        if (d.record.state == BS_STATE_TRIAL && d.record.active == d.slot) trialWatchdog();
        jump(slotAddr(d.slot), slotAddr(d.slot));
    }

    // Ninguna copia válida: cargador de fábrica (DFU) con la memoria de sistema en 0
    reg(RCC_APB2ENR) |= 1u << 14;                     // SYSCFGEN
    { const uint32_t t = reg(RCC_APB2ENR); (void)t; } // Lectura: espera al reloj (errata)
    __asm volatile("dsb");
    reg(SYSCFG_MEMRMP) = 1u;
    jump(AB_SYSMEM_ADDR, AB_SYSMEM_ADDR);
}

#endif  // AB_BOOTLOADER && __arm__
