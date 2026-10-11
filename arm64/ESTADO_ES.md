# SeaBIOS-ARM64 — estado experimental de v0.6

**Ruta MBR FAT32 AArch64 funcional en QEMU, sin UEFI. Windows ARM64 no arranca.**

El firmware encuentra una partición MBR FAT32 activa en un disco VirtIO,
localiza `BOOT/ARM64.BIN` o `BOOT/ARM64.EXE`, valida y carga una segunda
etapa AArch64 y transfiere el control. También carga ejecutables PE32+ ARM64
nativos **sin imports ni relocations**, creados para esta prueba; esto no
equivale al administrador de arranque de Microsoft.

Se mantienen los subsistemas originales de SeaBIOS reutilizados desde
v0.3-v0.5 y se comprueba que la compilación x86 original sigue funcionando.
Los tests están en `arm64/tests/test_qemu_legacy.py` y la documentación
completa en [PHASE4_LEGACY_MBR.md](PHASE4_LEGACY_MBR.md).

**No se debe flashear a un dispositivo físico.**
