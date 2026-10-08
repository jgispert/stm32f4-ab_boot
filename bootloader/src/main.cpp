// Cargador A/B de ejemplo: todo está en la librería (AbBootloader.cpp).
// Un proyecto puede redefinir ab_bootloader_board_init() para dejar sus pines
// en un estado seguro antes de que arranque la copia (o el DFU de fábrica).
#include "AbBootloader.h"

int main() {
    ab_bootloader_run();
}
