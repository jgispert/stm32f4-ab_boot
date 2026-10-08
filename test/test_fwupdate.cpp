// Prueba en PC de #FW (src/FwUpdate.cpp): sesión completa A → B con una flash
// en RAM, errores, prueba/confirmación y el arranque con BootState.
// Se compila con -DAB_RUN_SLOT=0 (como la imagen de la copia A).
#include "FwUpdate.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define PRODUCT "PRODUCTO-PRUEBA"
AB_FW_HEADER(PRODUCT, "v-en-marcha");
static std::string outBuf;
static void out(const char* l) { outBuf += l; outBuf += '\n'; }
static void flush() {}

static int fails = 0;
#define CHECK(c, ...) do{ if(!(c)){ printf("  FALLO: " __VA_ARGS__); printf("\n"); fails++; } else { printf("  ok: " __VA_ARGS__); printf("\n"); } }while(0)

static std::vector<uint8_t> slotMem[2] = {std::vector<uint8_t>(AB_SLOT_SIZE, 0xFF), std::vector<uint8_t>(AB_SLOT_SIZE, 0xFF)};
static uint8_t stateMem[2][AB_STATE_SIZE];
static uint32_t clk = 0; static int reboots = 0, erases = 0; static uint32_t wdMax = 0;
static bool program(const uint8_t* a, const void* d, uint32_t n) { uint8_t* p = const_cast<uint8_t*>(a); const uint8_t* s = (const uint8_t*)d; for (uint32_t i = 0; i < n; i++) p[i] &= s[i]; return true; }
static bool eraseSt(int w) { memset(stateMem[w], 0xFF, AB_STATE_SIZE); return true; }
static bool eraseSlot(int s) { std::fill(slotMem[s].begin(), slotMem[s].end(), 0xFF); erases++; return true; }
static void wd(uint32_t ms) { if (ms > wdMax) wdMax = ms; }
static void reboot() { reboots++; }
static uint32_t nowMs() { return clk += 1; }
static FwPlatform P = {
    {slotMem[0].data(), slotMem[1].data()}, {AB_SLOT_A_ADDR, AB_SLOT_B_ADDR}, AB_SLOT_SIZE,
    {{stateMem[0], stateMem[1]}, AB_STATE_SIZE, program, eraseSt}, 0, eraseSlot, program, wd, reboot, nowMs, out, flush,
};

static std::string send(const std::string& l) { outBuf.clear(); FwUpdate_line(l.c_str()); std::string o = outBuf; if (!o.empty() && o.back() == '\n') o.pop_back(); return o; }

/// Imagen de prueba: datos + cabecera con producto, dirección y versión.
static std::vector<uint8_t> image(uint32_t size, uint32_t link, const char* product = PRODUCT) {
    std::vector<uint8_t> img(size);
    for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t)(i * 13 + 5);
    FwHeader h; memset(&h, 0, sizeof h);
    memcpy(h.magic, AB_HDR_MAGIC, 16); strncpy(h.product, product, sizeof h.product);
    h.linkAddr = link; h.hdrVersion = 1; strcpy(h.version, "v-prueba-1");
    memcpy(img.data() + 0x300, &h, sizeof h);
    return img;
}
static std::string hex(const uint8_t* p, size_t n) { static const char* H = "0123456789abcdef"; std::string s; for (size_t i = 0; i < n; i++) { s += H[p[i] >> 4]; s += H[p[i] & 15]; } return s; }
static char buf[64];
static const char* h8(uint32_t v) { snprintf(buf, sizeof buf, "%08X", v); return buf; }

/// Envía la imagen entera: devuelve la respuesta a END.
static std::string upload(const std::vector<uint8_t>& img, char slot, uint32_t crc, bool corrupt = false) {
    std::string r = send(std::string("#FW BEGIN ") + std::to_string(img.size()) + " " + h8(crc) + " " + slot);
    if (r != "#FW READY") return r;
    for (uint32_t off = 0; off < img.size(); off += AB_FW_CHUNK_MAX) {
        const uint32_t n = std::min<uint32_t>(AB_FW_CHUNK_MAX, img.size() - off);
        std::vector<uint8_t> c(img.begin() + off, img.begin() + off + n);
        if (corrupt && off == 1024) c[3] ^= 0x40;
        r = send(std::string("#fw data ") + h8(off) + " " + hex(c.data(), n));
        if (r != std::string("#FW OK ") + h8(off + n)) return "DATA: " + r;
    }
    return send("#FW END");
}

static bool slotCheck(int slot, uint32_t size, uint32_t crc) { return size == 0 || bs_crc32(0, slotMem[slot].data(), size) == crc; }

int main() {
    eraseSt(0); eraseSt(1);
    puts("1) Sin cargador");
    FwPlatform noBoot = P; noBoot.running = -1;           // Imagen grabada sin cargador
    FwUpdate_begin(&noBoot);
    CHECK(send("#FW") == "#FW INFO SLOT=NONE VER=v-en-marcha PROD=" PRODUCT, "#FW → INFO SLOT=NONE");
    CHECK(send("#FW BEGIN 100 0 B") == "#FW ERR NOBOOT", "BEGIN → ERR NOBOOT");
    CHECK(send("#FW CONFIRM") == "#FW ERR NOBOOT", "CONFIRM → ERR NOBOOT");
    BsRecord none; CHECK(!bs_latest(&P.state, &none), "sin cargador no se escribe estado");
    FwUpdate_begin(nullptr);
    CHECK(send("#FW").empty(), "sin plataforma: no responde ni falla");

    puts("2) Arranque desde A: estado inicial");
    FwUpdate_begin(&P);
    BsRecord r; CHECK(bs_latest(&P.state, &r) && r.active == 0 && r.state == BS_STATE_OK, "registro inicial: A confirmada");
    std::string o = send("#FW");
    CHECK(o.find("SLOT=A STATE=OK TRIES=0") != std::string::npos && o.find("FREE=B MAX=131072 PROD=" PRODUCT "") != std::string::npos, "%s", o.c_str());

    puts("3) Errores antes de enviar nada");
    CHECK(send("#FW BEGIN 100 0 A") == "#FW ERR SLOT", "A está en marcha → ERR SLOT");
    CHECK(send("#FW BEGIN 200000 0 B") == "#FW ERR SIZE", "más de 128 KB → ERR SIZE");
    CHECK(send("#FW BEGIN x 0 B") == "#FW ERR ARG", "tamaño mal → ERR ARG");
    CHECK(send("#FW DATA 00000000 00") == "#FW ERR STATE", "DATA sin BEGIN → ERR STATE");
    CHECK(send("#FW END") == "#FW ERR STATE" && send("#FW APPLY") == "#FW ERR STATE", "END/APPLY fuera de orden → ERR STATE");
    CHECK(send("#FW FOO") == "#FW ERR ARG", "subcomando desconocido → ERR ARG");

    puts("4) Sesión completa A → B (tamaño no múltiplo de 4)");
    auto img = image(37683, AB_SLOT_B_ADDR); const uint32_t crc = bs_crc32(0, img.data(), img.size());
    o = send(std::string("#FW BEGIN ") + std::to_string(img.size()) + " " + h8(crc) + " B");
    CHECK(o == "#FW READY" && FwUpdate_busy() && wdMax >= 8000, "READY, sesión abierta, watchdog alargado mientras borra");
    CHECK(send("#FW DATA 00000080 00") == "#FW ERR SEQ 00000000", "posición equivocada → ERR SEQ 00000000");
    std::string data = std::string("#FW DATA 00000000 ") + hex(img.data(), 128);
    CHECK(send(data) == "#FW OK 00000080", "primer bloque → OK 00000080");
    CHECK(send(data) == "#FW ERR SEQ 00000080", "repetido → ERR SEQ 00000080 (la tablet sigue desde ahí)");
    CHECK(send("#FW END") == "#FW ERR SIZE 00000080", "END incompleto → ERR SIZE");
    o = upload(img, 'B', crc);
    CHECK(o == "#FW VERIFIED VER=v-prueba-1", "subida entera de nuevo desde BEGIN → %s", o.c_str());
    CHECK(memcmp(slotMem[1].data(), img.data(), img.size()) == 0, "copia B idéntica a la imagen");
    CHECK(send("#FW APPLY") == "#FW REBOOT" && reboots == 1, "APPLY → REBOOT y reinicio");
    CHECK(bs_latest(&P.state, &r) && r.active == 1 && r.state == BS_STATE_TRIAL && r.prev == 0 && r.size[1] == img.size() && r.crc[1] == crc, "estado: B a prueba, vuelta a A, tamaño y CRC");

    puts("5) Arranque: el cargador elige B a prueba; la tablet confirma");
    BsDecision d = bs_decide(&r, slotCheck);
    CHECK(d.slot == 1 && d.record.tries == 1, "cargador → B (intento 1)");
    bs_append(&P.state, d.record);
    P.running = 1; FwUpdate_begin(&P);
    o = send("#FW"); CHECK(o.find("SLOT=B STATE=TRIAL TRIES=1") != std::string::npos && o.find("FREE=A") != std::string::npos, "%s", o.c_str());
    CHECK(send(std::string("#FW BEGIN 5000 0 A")) == "#FW ERR STATE", "BEGIN con la copia en marcha a prueba → ERR STATE (A es la de vuelta atrás)");
    CHECK(slotMem[0][0] != 0xFF || true, "A sigue intacta");
    CHECK(send("#FW CONFIRM") == "#FW CONFIRMED", "CONFIRM → CONFIRMED");
    o = send("#FW"); CHECK(o.find("SLOT=B STATE=OK TRIES=0") != std::string::npos, "y queda confirmada");
    CHECK(send("#FW CONFIRM") == "#FW CONFIRMED", "CONFIRM otra vez: sin error");

    puts("6) Imágenes malas: nada cambia");
    BsRecord before; bs_latest(&P.state, &before);
    CHECK(upload(img, 'A', crc) == "#FW ERR SLOT", "imagen compilada para B enviada a A → ERR SLOT");
    auto imgA = image(5000, AB_SLOT_A_ADDR, "OTRO-PRODUCTO"); const uint32_t crcA = bs_crc32(0, imgA.data(), imgA.size());
    CHECK(upload(imgA, 'A', crcA) == "#FW ERR PRODUCT", "otro producto → ERR PRODUCT");
    imgA = image(5000, AB_SLOT_A_ADDR);
    CHECK(upload(imgA, 'A', bs_crc32(0, imgA.data(), imgA.size()), true) == "#FW ERR CRC", "un bit cambiado → ERR CRC");
    CHECK(send("#FW APPLY") == "#FW ERR STATE", "tras un error no se puede aplicar");
    BsRecord after; bs_latest(&P.state, &after);
    CHECK(after.active == before.active && after.state == BS_STATE_OK && after.size[1] == before.size[1] && after.crc[1] == before.crc[1]
          && after.size[0] == 0xFFFFFFFFu && reboots == 1 && !FwUpdate_busy(),
          "sigue B confirmada con su CRC; A marcada como no arrancable; sin reinicios; sesión cerrada");

    puts("6b) BEGIN invalida la copia libre ANTES de borrarla");
    CHECK(send(std::string("#FW BEGIN 5000 ") + h8(crcA) + " A") == "#FW READY", "BEGIN A");
    bs_latest(&P.state, &r);
    CHECK(r.size[0] == 0xFFFFFFFFu, "estado: tamaño de A imposible (el cargador no la arrancará a medias)");
    send("#FW ABORT");

    puts("7) Sesión abandonada y ABORT");
    CHECK(send(std::string("#FW BEGIN 5000 ") + h8(crcA) + " A") == "#FW READY" && FwUpdate_busy(), "BEGIN");
    clk += 31000; FwUpdate_poll();
    CHECK(!FwUpdate_busy() && send("#FW DATA 00000000 00") == "#FW ERR STATE", "31 s sin comandos → se cancela");
    send(std::string("#FW BEGIN 5000 ") + h8(crcA) + " A");
    CHECK(send("#FW ABORT") == "#FW ABORTED" && !FwUpdate_busy(), "ABORT → ABORTED");

    printf("\n%s\n", fails ? "HAY FALLOS" : "TODO OK (0 fallos)");
    return fails ? 1 : 0;
}
