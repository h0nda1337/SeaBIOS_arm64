# SeaBIOS-ARM64 — fase 1, revisión v0.3-dev

**Compila para AArch64. No se han ejecutado pruebas de arranque en QEMU.**

La revisión integra código auténtico de SeaBIOS: `src/e820map.c`,
`src/romfile.c` y `src/list.h`. Se añadió un backend QEMU fw_cfg-MMIO,
lectura de reservas del Device Tree, enumeración inicial de transportes
VirtIO-MMIO, heap temporal de arranque, salida serie
para mensajes del núcleo original y vectores de excepción EL1/EL2.

El programa diagnóstico comprueba las regiones disponibles del mapa de
memoria antes de transferir el control. La versión x86 original continúa
disponible en el mismo árbol y no se ha sustituido por una implementación
nueva.

**Esto todavía no es un port completo del POST ni del boot manager.** No
arranca Linux, Windows ARM64 ni programas x86, y carece de UEFI, controladores
VirtIO de disco y un protocolo de arranque de SO.

Consulta [PHASE1.md](PHASE1.md) para detalles técnicos, compilación,
riesgos conocidos y siguientes etapas.
