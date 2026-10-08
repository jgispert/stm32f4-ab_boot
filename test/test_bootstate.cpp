// Prueba en PC de lib/BootState (D29): registros con cortes de luz, sectores
// alternos y la decisión del cargador, con una flash simulada en RAM.
#include "BootState.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <zlib.h>

static int fails = 0;
#define CHECK(c, ...) do{ if(!(c)){ printf("  FALLO: " __VA_ARGS__); printf("\n"); fails++; } else { printf("  ok: " __VA_ARGS__); printf("\n"); } }while(0)

static const uint32_t SECT = 1024;            // sectores pequeños: se llenan antes (16 registros)
static uint8_t flash[2][SECT];
static int erases = 0;
static int cutAfterBytes = -1;                // corte de luz simulado: graba sólo N bytes
static bool prog(const uint8_t* addr, const void* data, uint32_t len) {
    uint8_t* d = const_cast<uint8_t*>(addr);
    const uint8_t* s = static_cast<const uint8_t*>(data);
    for (uint32_t i = 0; i < len; i++) {
        if (cutAfterBytes >= 0 && (int)i >= cutAfterBytes) return false;
        d[i] &= s[i];                         // la flash sólo pasa bits de 1 a 0
    }
    return true;
}
static bool erase(int w) { memset(flash[w], 0xFF, SECT); erases++; return true; }
static BsFlash F = {{flash[0], flash[1]}, SECT, prog, erase};

// Copias simuladas: válidas o no
static bool okA = true, okB = true;
static uint32_t wantCrcB = 0;
static bool check(int slot, uint32_t size, uint32_t crc) {
    if (slot == 0) return okA;
    if (size && crc != wantCrcB) return false;
    return okB;
}

int main() {
    puts("1) CRC32 = el de zlib (Python, empaquetado)");
    const char* s = "VENDO SLAVE";
    CHECK(bs_crc32(0, s, strlen(s)) == crc32(0, (const Bytef*)s, strlen(s)), "'%s' → %08X", s, bs_crc32(0, s, strlen(s)));
    std::vector<uint8_t> big(100000); for (size_t i = 0; i < big.size(); i++) big[i] = (uint8_t)(i * 7 + 3);
    CHECK(bs_crc32(0, big.data(), big.size()) == crc32(0, big.data(), big.size()), "100 KB iguales");
    CHECK(bs_crc32(bs_crc32(0, big.data(), 500), big.data() + 500, big.size() - 500) == crc32(0, big.data(), big.size()), "por partes = de una vez");

    puts("2) Flash vacía → sin estado; el cargador arranca A (sólo vectores)");
    erase(0); erase(1);
    BsRecord r;
    CHECK(!bs_latest(&F, &r), "sin registros");
    BsDecision d = bs_decide(nullptr, check);
    CHECK(d.slot == 0 && !d.write, "copia A, sin escribir");
    okA = false; d = bs_decide(nullptr, check); CHECK(d.slot == 1, "A mal → B"); okA = true;

    puts("3) Registros: el último manda; llenar un sector pasa al otro");
    CHECK(bs_append(&F, bs_initial(0)), "estado inicial grabado");
    CHECK(bs_latest(&F, &r) && r.seq == 1 && r.active == 0 && r.state == BS_STATE_OK, "seq 1, A confirmada");
    for (int i = 0; i < 20; i++) { BsRecord x = r; x.tries = (uint8_t)i; bs_append(&F, x); }
    CHECK(bs_latest(&F, &r) && r.seq == 21 && r.tries == 19, "21 registros: último seq 21 (sector 0 lleno con 16)");
    CHECK(erases == 3, "un borrado al cambiar de sector (%d)", erases - 2);

    puts("4) Corte de luz a mitad de un registro → vale el anterior");
    const uint32_t seqBefore = r.seq;
    cutAfterBytes = 30; BsRecord x = r; x.active = 1; bool w = bs_append(&F, x); cutAfterBytes = -1;
    CHECK(!w && bs_latest(&F, &r) && r.seq == seqBefore && r.active == 0, "registro cortado ignorado (sigue A, seq %u)", r.seq);
    CHECK(bs_append(&F, bs_initial(0)) && bs_latest(&F, &r) && r.seq == seqBefore + 1, "el siguiente se graba detrás del cortado");

    puts("5) Actualización: a prueba, 3 intentos y vuelta atrás");
    wantCrcB = 0x1234ABCD;
    BsRecord t = bs_trial(r, 0, 1, 40000, 0x1234ABCD);
    CHECK(bs_append(&F, t) && bs_latest(&F, &r) && r.active == 1 && r.state == BS_STATE_TRIAL && r.prev == 0, "APPLY: B a prueba, vuelta a A");
    for (int boot = 1; boot <= 3; boot++) {
        d = bs_decide(&r, check);
        CHECK(d.slot == 1 && d.write && d.record.tries == boot, "arranque %d a prueba → B (intentos %d)", boot, d.record.tries);
        bs_append(&F, d.record); bs_latest(&F, &r);
    }
    d = bs_decide(&r, check);
    CHECK(d.slot == 0 && d.write && d.record.state == BS_STATE_OK && d.record.active == 0, "4.º arranque sin confirmar → vuelve a A");
    bs_append(&F, d.record); bs_latest(&F, &r);
    d = bs_decide(&r, check);
    CHECK(d.slot == 0 && !d.write, "y se queda en A sin escribir más");

    puts("6) Confirmación");
    t = bs_trial(r, 0, 1, 40000, 0x1234ABCD); bs_append(&F, t); bs_latest(&F, &r);
    d = bs_decide(&r, check); bs_append(&F, d.record); bs_latest(&F, &r);
    bs_append(&F, bs_confirm(r)); bs_latest(&F, &r);
    CHECK(r.state == BS_STATE_OK && r.active == 1 && r.prev == BS_SLOT_NONE, "CONFIRM: B confirmada");
    for (int i = 0; i < 5; i++) { d = bs_decide(&r, check); CHECK(d.slot == 1 && !d.write, "arranque %d → B, sin escribir", i + 1); }

    puts("7) Copias dañadas");
    wantCrcB = 0; d = bs_decide(&r, check);
    CHECK(d.slot == 0 && d.write && d.record.active == 0, "CRC de B mal → arranca A y lo guarda");
    okA = false; d = bs_decide(&r, check);
    CHECK(d.slot == -1 && !d.write, "las dos mal → -1 (DFU de fábrica)");
    okA = true; wantCrcB = 0x1234ABCD;
    t = bs_trial(r, 1, 0, 0, 0); t.prev = BS_SLOT_NONE;
    for (int i = 0; i < 4; i++) { d = bs_decide(&t, check); t = d.record; }
    CHECK(t.state == BS_STATE_OK && t.tries == 0, "a prueba sin copia anterior: tras 3 intentos queda confirmada (no se desborda)");

    puts("8) Registro con CRC o marca mal → no vale");
    BsRecord bad = bs_initial(1); bad.magic = BS_MAGIC; bad.seq = 999; bad.crc32 = 0;
    CHECK(!bs_valid(&bad), "CRC mal"); bad.crc32 = bs_crc32(0, &bad, offsetof(BsRecord, crc32)); CHECK(bs_valid(&bad), "CRC bien");
    bad.active = 7; bad.crc32 = bs_crc32(0, &bad, offsetof(BsRecord, crc32)); CHECK(!bs_valid(&bad), "copia 7 no existe");

    printf("\n%s\n", fails ? "HAY FALLOS" : "TODO OK (0 fallos)");
    return fails ? 1 : 0;
}
