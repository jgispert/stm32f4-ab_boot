// Ejemplo mínimo: atiende #FW por el USB CDC. Cualquier otra línea → "? <línea>".
#include <Arduino.h>
#include <IWatchdog.h>
#include "FwUpdate.h"
#include "AbPlatformF4.h"

AB_FW_HEADER("EJEMPLO-AB", EXAMPLE_VERSION);

static const uint32_t WDG_MS = 1000;

static void wdg(uint32_t ms) { IWatchdog.set((ms ? ms : WDG_MS) * 1000); IWatchdog.reload(); }
static void reboot() { NVIC_SystemReset(); }
static uint32_t now() { return millis(); }
static void out(const char* l) { Serial.println(l); }
static void flush() { Serial.flush(); }

static char line[320];
static size_t len = 0;

void setup() {
    IWatchdog.begin(WDG_MS * 1000);
    Serial.begin(115200);
    FwUpdate_begin(ab_platformF4({wdg, reboot, now, out, flush}));
}

void loop() {
    IWatchdog.reload();
    FwUpdate_poll();
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') { if (len < sizeof line - 1) line[len++] = c; continue; }
        line[len] = '\0';
        len = 0;
        if (FwUpdate_isFw(line)) FwUpdate_line(line);
        else if (line[0]) { Serial.print("? "); Serial.println(line); }
    }
}
