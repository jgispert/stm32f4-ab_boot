/**
 * @file    BootState.cpp
 * @brief   Ver BootState.h
 */

#include "BootState.h"
#include <string.h>

uint32_t bs_crc32(uint32_t crc, const void* data, size_t len) {
    // CRC-32 de zlib (polinomio reflejado 0xEDB88320), tabla de 16 entradas:
    // pequeño para el cargador y lo bastante rápido (128 KB ≈ 100 ms a 16 MHz).
    static const uint32_t T[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ T[crc & 0x0F];
        crc = (crc >> 4) ^ T[crc & 0x0F];
    }
    return ~crc;
}

bool bs_valid(const BsRecord* r) {
    return r->magic == BS_MAGIC
        && r->crc32 == bs_crc32(0, r, offsetof(BsRecord, crc32))
        && r->active <= 1;
}

static bool blank(const uint8_t* p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xFF) return false;
    return true;
}

/// Último registro correcto de un sector (y su posición), y primer hueco libre.
static bool scan(const BsFlash* f, int s, BsRecord* best, uint32_t* freeOff) {
    bool found = false;
    *freeOff = f->sectorSize;                 // = lleno
    for (uint32_t off = 0; off + BS_RECORD_SIZE <= f->sectorSize; off += BS_RECORD_SIZE) {
        const uint8_t* p = f->sector[s] + off;
        if (blank(p, BS_RECORD_SIZE)) { *freeOff = off; break; }   // se escribe en orden
        BsRecord r;
        memcpy(&r, p, sizeof r);
        if (bs_valid(&r) && (!found || r.seq > best->seq)) { *best = r; found = true; }
    }
    return found;
}

bool bs_latest(const BsFlash* f, BsRecord* out) {
    BsRecord r[2];
    uint32_t fo;
    const bool ok0 = scan(f, 0, &r[0], &fo);
    const bool ok1 = scan(f, 1, &r[1], &fo);
    if (ok0 && (!ok1 || r[0].seq >= r[1].seq)) { *out = r[0]; return true; }
    if (ok1) { *out = r[1]; return true; }
    return false;
}

bool bs_append(const BsFlash* f, BsRecord r) {
    // Sector del último registro (o el 0 si no hay ninguno)
    BsRecord last[2];
    uint32_t freeOff[2];
    const bool ok0 = scan(f, 0, &last[0], &freeOff[0]);
    const bool ok1 = scan(f, 1, &last[1], &freeOff[1]);
    int s = 0;
    uint32_t seq = 1;
    if (ok0 || ok1) {
        s = (ok0 && (!ok1 || last[0].seq >= last[1].seq)) ? 0 : 1;
        seq = last[s].seq + 1;
    }
    if (freeOff[s] >= f->sectorSize) {        // Lleno: al otro sector, borrado
        s ^= 1;
        if (!f->erase(s)) return false;
        freeOff[s] = 0;
    }
    r.magic = BS_MAGIC;
    r.seq = seq;
    r.crc32 = bs_crc32(0, &r, offsetof(BsRecord, crc32));
    const uint8_t* dst = f->sector[s] + freeOff[s];
    if (!f->program(dst, &r, sizeof r)) return false;
    return memcmp(dst, &r, sizeof r) == 0;
}

BsDecision bs_decide(const BsRecord* latest, BsSlotCheck check) {
    BsDecision d;
    memset(&d, 0, sizeof d);
    d.slot = -1;

    if (!latest) {                            // Recién grabada con el ST-Link: A, si no B
        if (check(0, 0, 0)) d.slot = 0;
        else if (check(1, 0, 0)) d.slot = 1;
        return d;
    }

    BsRecord r = *latest;
    if (r.state == BS_STATE_TRIAL) {          // Un arranque más a prueba
        r.tries++;
        d.write = true;
        if (r.tries > BS_MAX_TRIES) {         // No se confirmó: volver
            if (r.prev <= 1) r.active = r.prev;   // (sin copia anterior: se queda, confirmada)
            r.prev = BS_SLOT_NONE;
            r.state = BS_STATE_OK;
            r.tries = 0;
        }
    }

    const int a = r.active, b = a ^ 1;
    if (check(a, r.size[a], r.crc[a])) {
        d.slot = a;
    } else if (check(b, r.size[b], r.crc[b])) {   // La elegida está mal: la otra
        r.active = static_cast<uint8_t>(b);
        r.prev = BS_SLOT_NONE;
        r.state = BS_STATE_OK;
        r.tries = 0;
        d.slot = b;
        d.write = true;
    }
    d.record = r;                              // Sin copia válida: DFU (no se escribe nada útil)
    if (d.slot < 0) d.write = false;
    return d;
}

BsRecord bs_initial(int running) {
    BsRecord r;
    memset(&r, 0, sizeof r);
    r.active = static_cast<uint8_t>(running);
    r.state = BS_STATE_OK;
    r.prev = BS_SLOT_NONE;
    return r;
}

BsRecord bs_trial(const BsRecord& cur, int running, int slot, uint32_t size, uint32_t crc) {
    BsRecord r = cur;
    r.prev = static_cast<uint8_t>(running);
    r.active = static_cast<uint8_t>(slot);
    r.state = BS_STATE_TRIAL;
    r.tries = 0;
    r.size[slot] = size;
    r.crc[slot] = crc;
    return r;
}

BsRecord bs_confirm(const BsRecord& cur) {
    BsRecord r = cur;
    r.state = BS_STATE_OK;
    r.tries = 0;
    r.prev = BS_SLOT_NONE;
    return r;
}
