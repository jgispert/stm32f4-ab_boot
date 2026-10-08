/**
 * @file    FwUpdate.cpp
 * @brief   Ver FwUpdate.h
 */

#include "FwUpdate.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum class St : uint8_t { Idle, Recv, Verified };

const FwPlatform* P = nullptr;
St       s_st = St::Idle;
int      s_slot = -1;
uint32_t s_size = 0, s_crc = 0, s_next = 0, s_lastMs = 0;
char     s_ver[sizeof(FwHeader::version) + 1];

char slotName(int s) { return s == 0 ? 'A' : s == 1 ? 'B' : '-'; }

void reply(const char* text) {
    char b[176];
    snprintf(b, sizeof b, "#FW %s", text);
    if (P && P->out) P->out(b);
}

void replyf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void replyf(const char* fmt, ...) {
    char b[168];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    reply(b);
}

bool latest(BsRecord* r) {
    if (bs_latest(&P->state, r)) return true;
    *r = bs_initial(P->running);
    return false;
}

void info() {
    char ver[sizeof ab_fwHeader.version + 1], prod[sizeof ab_fwHeader.product + 1];
    memcpy(ver, ab_fwHeader.version, sizeof ab_fwHeader.version);   ver[sizeof ab_fwHeader.version] = '\0';
    memcpy(prod, ab_fwHeader.product, sizeof ab_fwHeader.product);  prod[sizeof ab_fwHeader.product] = '\0';
    if (P->running < 0) {
        replyf("INFO SLOT=NONE VER=%s PROD=%s", ver, prod);
        return;
    }
    BsRecord r;
    latest(&r);
    const bool trial = r.state == BS_STATE_TRIAL && r.active == P->running;
    replyf("INFO SLOT=%c STATE=%s TRIES=%u VER=%s FREE=%c MAX=%lu PROD=%s",
           slotName(P->running), trial ? "TRIAL" : "OK", trial ? r.tries : 0u, ver,
           slotName(P->running ^ 1), static_cast<unsigned long>(P->slotSize), prod);
}

void endSession() {
    s_st = St::Idle;
    s_slot = -1;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// "1A2B" → valor; false si no es hexadecimal o tiene más de 8 cifras.
bool parseHex32(const char* s, size_t n, uint32_t* out) {
    if (n == 0 || n > 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        const int h = hexval(s[i]);
        if (h < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(h);
    }
    *out = v;
    return true;
}

/// Siguiente palabra de `*p` (separada por espacios). Devuelve su longitud.
size_t word(const char** p, const char** start) {
    while (**p == ' ' || **p == '\t') (*p)++;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    return static_cast<size_t>(*p - *start);
}

bool is(const char* w, size_t n, const char* kw) {
    if (strlen(kw) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (toupper(static_cast<unsigned char>(w[i])) != kw[i]) return false;
    return true;
}

void cmdBegin(const char* p) {
    if (P->running < 0) return reply("ERR NOBOOT");
    const char *w1, *w2, *w3;
    const size_t n1 = word(&p, &w1), n2 = word(&p, &w2), n3 = word(&p, &w3);
    uint32_t size, crc;
    const char* rest;
    if (!n1 || !n2 || n3 != 1 || word(&p, &rest) || !parseHex32(w2, n2, &crc)) return reply("ERR ARG");
    char* end;
    const unsigned long sz = strtoul(w1, &end, 10);
    if (end != w1 + n1) return reply("ERR ARG");
    size = static_cast<uint32_t>(sz);
    const int slot = toupper(static_cast<unsigned char>(*w3)) == 'A' ? 0
                   : toupper(static_cast<unsigned char>(*w3)) == 'B' ? 1 : -1;
    if (slot != (P->running ^ 1)) return reply("ERR SLOT");
    if (size == 0 || size > P->slotSize) return reply("ERR SIZE");
    BsRecord cur;
    latest(&cur);
    // La copia en marcha está a prueba: la libre es la de vuelta atrás. Hay que
    // confirmar (o volver) antes de borrarla.
    if (cur.state == BS_STATE_TRIAL) return reply("ERR STATE");
    endSession();
    // Antes de borrar: la copia libre deja de ser válida para el cargador
    // (tamaño imposible). Una copia a medias nunca se arrancará por sus vectores.
    cur.size[slot] = 0xFFFFFFFFu;
    cur.crc[slot] = 0;
    if (!bs_append(&P->state, cur)) return reply("ERR WRITE");
    P->watchdog(8000);                       // Borrar 128 KB para la CPU 1–2 s
    const bool ok = P->eraseSlot(slot);
    P->watchdog(0);                          // 0 = volver al tiempo normal
    if (!ok) return reply("ERR WRITE");
    s_st = St::Recv;
    s_slot = slot;
    s_size = size;
    s_crc = crc;
    s_next = 0;
    reply("READY");
}

void cmdData(const char* p) {
    if (s_st != St::Recv) return reply("ERR STATE");
    const char *w1, *w2, *rest;
    const size_t n1 = word(&p, &w1), n2 = word(&p, &w2);
    uint32_t off;
    if (!n1 || !n2 || word(&p, &rest) || !parseHex32(w1, n1, &off) || (n2 & 1) || n2 / 2 > AB_FW_CHUNK_MAX)
        return reply("ERR ARG");
    if (off != s_next) return replyf("ERR SEQ %08lX", static_cast<unsigned long>(s_next));
    const uint32_t len = static_cast<uint32_t>(n2 / 2);
    if (off + len > s_size) return reply("ERR SIZE");
    uint8_t buf[AB_FW_CHUNK_MAX + 4];
    for (uint32_t i = 0; i < len; i++) {
        const int hi = hexval(w2[2 * i]), lo = hexval(w2[2 * i + 1]);
        if (hi < 0 || lo < 0) return reply("ERR ARG");
        buf[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    uint32_t plen = len;
    while (plen & 3u) buf[plen++] = 0xFF;    // La flash se graba por palabras de 4 bytes
    if (!P->program(P->slot[s_slot] + off, buf, plen)) { endSession(); return reply("ERR WRITE"); }
    s_next += len;
    replyf("OK %08lX", static_cast<unsigned long>(s_next));
}

const FwHeader* findHeader(const uint8_t* img, uint32_t size) {
    for (uint32_t off = 0; off + sizeof(FwHeader) <= size; off += 4)
        if (memcmp(img + off, ab_fwHeader.magic, sizeof ab_fwHeader.magic) == 0)
            return reinterpret_cast<const FwHeader*>(img + off);
    return nullptr;
}

void cmdEnd() {
    if (s_st != St::Recv) return reply("ERR STATE");
    if (s_next != s_size) return replyf("ERR SIZE %08lX", static_cast<unsigned long>(s_next));
    const uint8_t* img = P->slot[s_slot];
    if (bs_crc32(0, img, s_size) != s_crc) { endSession(); return reply("ERR CRC"); }
    const FwHeader* h = findHeader(img, s_size);
    if (!h || strncmp(h->product, ab_fwHeader.product, sizeof h->product) != 0) { endSession(); return reply("ERR PRODUCT"); }
    if (h->linkAddr != P->slotLink[s_slot]) { endSession(); return reply("ERR SLOT"); }
    memcpy(s_ver, h->version, sizeof h->version);
    s_ver[sizeof h->version] = '\0';
    s_st = St::Verified;
    replyf("VERIFIED VER=%s", s_ver);
}

void cmdApply() {
    if (s_st != St::Verified) return reply("ERR STATE");
    BsRecord cur;
    latest(&cur);
    if (!bs_append(&P->state, bs_trial(cur, P->running, s_slot, s_size, s_crc))) {
        endSession();
        return reply("ERR WRITE");
    }
    reply("REBOOT");
    if (P->flush) P->flush();
    const uint32_t t0 = P->nowMs();
    while (P->nowMs() - t0 < 200) { }        // Que la respuesta salga por USB
    P->reboot();
}

void cmdConfirm() {
    if (P->running < 0) return reply("ERR NOBOOT");
    BsRecord cur;
    const bool have = latest(&cur);
    if (cur.active != P->running) return reply("ERR STATE");
    if (have && cur.state == BS_STATE_OK) return reply("CONFIRMED");     // Ya lo estaba
    if (!bs_append(&P->state, bs_confirm(cur))) return reply("ERR WRITE");
    reply("CONFIRMED");
}

}  // namespace

void FwUpdate_begin(const FwPlatform* p) {
    P = p;
    endSession();
    if (!P || P->running < 0) return;
    BsRecord r;
    if (!bs_latest(&P->state, &r)) bs_append(&P->state, bs_initial(P->running));
}

bool FwUpdate_isFw(const char* line) {
    while (*line == ' ' || *line == '\t') line++;
    return line[0] == '#' && toupper(static_cast<unsigned char>(line[1])) == 'F'
        && toupper(static_cast<unsigned char>(line[2])) == 'W'
        && (line[3] == '\0' || line[3] == ' ' || line[3] == '\t');
}

void FwUpdate_line(const char* line) {
    while (*line == ' ' || *line == '\t') line++;
    const char* p = line + 3;                // Tras "#FW"
    const char* w;
    const size_t n = word(&p, &w);
    if (!P) return;                          // Sin FwUpdate_begin(): no hay a quién responder
    s_lastMs = P->nowMs();
    if (n == 0)                  return info();
    if (P->running < 0 && (is(w, n, "CONFIRM") || is(w, n, "BEGIN"))) return reply("ERR NOBOOT");
    if (is(w, n, "BEGIN"))       return cmdBegin(p);
    if (is(w, n, "DATA"))        return cmdData(p);
    const char* rest;
    if (word(&p, &rest))         return reply("ERR ARG");
    if (is(w, n, "END"))         return cmdEnd();
    if (is(w, n, "APPLY"))       return cmdApply();
    if (is(w, n, "CONFIRM"))     return cmdConfirm();
    if (is(w, n, "ABORT"))       { endSession(); return reply("ABORTED"); }
    reply("ERR ARG");
}

bool FwUpdate_busy() {
    return s_st != St::Idle;
}

void FwUpdate_poll() {
    if (s_st != St::Idle && P && P->nowMs() - s_lastMs > AB_FW_IDLE_MS) endSession();
}
