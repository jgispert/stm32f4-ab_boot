# Cambios

## v0.1.0 — sin publicar

Primera versión, extraída de VENDO SLAVE (Fase 8.5, decisión D29).

- Cargador A/B de ≈ 2 KB en el sector 0, sin framework: elige la copia, cuenta
  los arranques a prueba (AB_MAX_TRIES) y vuelve a la anterior; watchdog para la
  copia a prueba; DFU de fábrica si ninguna copia es válida.
- Estado en dos sectores alternos con registros de 64 bytes y CRC32
  (resistente a cortes de luz).
- Protocolo de texto `#FW` (INFO, BEGIN, DATA, END, APPLY, CONFIRM, ABORT) con
  comprobación de CRC32, producto y dirección de enlace (cabecera AB_FW_HEADER).
- Flash del STM32F4 a nivel de registro (sector 0 protegido).
- Herramientas: `fw_package.py` (.fwu) y `fw_update.py` (actualizar por un puerto serie).
- Pruebas en PC con una flash en RAM; CI con el cargador y un ejemplo Arduino.
