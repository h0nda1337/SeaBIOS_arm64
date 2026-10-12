# SeaBIOS-ARM64 — estado experimental de v1.5

La v1.5 ejecuta `src/block.c` original en AArch64 y comparte el registro IPL
extraído de `src/boot.c`. Arranca Debian ARM64 con systemd PID 1 y raíz ext4
en escritura, con VirtIO v1/v2, dos posiciones de disco y 128 MiB de RAM.
La compilación x86 se conserva. Estimación de ingeniería: alrededor de 44%,
sin equivalencia con cobertura medida o un port completo.

Estado y pruebas: [PHASE13_CORE_V15.md](PHASE13_CORE_V15.md).
Reproducción Linux: [DEBIAN_EXT4_ROOT.md](DEBIAN_EXT4_ROOT.md).
Límites de Windows: [WINDOWS_ARM64_CONSTRAINTS.md](WINDOWS_ARM64_CONSTRAINTS.md).

Los apartados siguientes describen el estado histórico de v0.8.

**Ruta MBR FAT32 AArch64 funcional en QEMU, sin UEFI. Windows ARM64 no arranca.**

El firmware encuentra una partición MBR FAT32 activa en un disco VirtIO,
localiza `BOOT/ARM64.BIN` o `BOOT/ARM64.EXE`, valida y carga una segunda
etapa AArch64 y transfiere el control. También carga ejecutables PE32+ ARM64
nativos **sin imports y con reubicaciones DIR64 limitadas**, creados para esta prueba; esto no
equivale al administrador de arranque de Microsoft.

Se mantienen los subsistemas originales de SeaBIOS reutilizados desde
v0.3-v0.5 y se comprueba que la compilación x86 original sigue funcionando.
Los tests están en `arm64/tests/test_qemu_legacy.py` y la documentación
completa en [PHASE4_LEGACY_MBR.md](PHASE4_LEGACY_MBR.md).

**No se debe flashear a un dispositivo físico.**

## Lo nuevo en v0.7

- Coincidencia de prioridades `bootorder` compartida entre x86 y ARM64
  a partir del codigo original de `src/boot.c`
- Lectura de prioridad VirtIO-MMIO desde `fw_cfg` verificada en QEMU
- `src/sha256.c` original compilado en ambas arquitecturas
- Contenedor legacy opcional `SBARMLE2` con hash SHA-256 (integridad,
  no autenticacion), manteniendo `SBARMLE1`

**Windows ARM64 sigue sin arrancar. No existen servicios UEFI.**

## Lo nuevo en v0.8

- El criterio original de orden de arranque de SeaBIOS ahora se comparte entre
  x86 y AArch64 (`src/bootprio.c`, llamado por `src/boot.c`)
- Carga PE32+ ARM64 sin importaciones, admitiendo reubicaciones DIR64
- Pruebas reales en QEMU con PE enlazado a una direccion distinta de carga
- Rechazo probado de reubicaciones manipuladas en VirtIO v1 y v2

**Esta fase NO arranca Windows ARM64. No implementa UEFI.**
