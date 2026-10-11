# SeaBIOS-ARM64 — fase experimental de arranque legacy MBR (v0.6)

**Estado:** ejecuta segundas etapas AArch64 sintéticas desde un disco MBR/FAT32 en QEMU. **NO inicia Windows ARM64, no ejecuta VBR x86, no implementa UEFI.**

## Qué se ha programado

1. **MBR real, sin código x86:** lee el sector 0 mediante VirtIO-MMIO, comprueba `55 AA` y selecciona una partición primaria **activa** FAT32 (tipos `0x0b` y `0x0c`). La firma MBR no significa que se ejecute el código de arranque x86 contenido en los primeros 446 bytes: ese código no puede correr directamente sobre ARM64.
2. **Sistema de archivos FAT32 en modo lectura:** valida BPB, bytes por sector, geometría de clusters, número y tamaño de FAT, y busca el directorio `BOOT` más un archivo 8.3 llamado `ARM64.BIN` o `ARM64.EXE`. Sigue cadenas de clusters no contiguos con comprobación de límites. No modifica el disco.
3. **Dos formatos experimentales de segunda etapa AArch64:**
   - `SBARMLE1`: cabecera de 64 bytes con longitud de código y FNV-1a (control de integridad accidental, **no firma de autenticidad**). Carga la etapa a `0x49000000` y transfiere el control al código cargado con un DTB en `x0`.
   - **PE32+ ARM64 nativo:** interpreta cabeceras DOS/PE, valida la arquitectura `0xaa64`, el subsistema `NATIVE (1)`, las secciones, las longitudes y un punto de entrada ejecutable. Carga al ImageBase **fijo** `0x4a000000`. Por diseño rechaza imports, relocations, TLS, DLL, CLR, EFI-subsystem y direcciones distintas. Ese ejecutable sigue siendo una segunda etapa propia, no Windows.
4. **Conserva el resto del port:** rutas anteriores de disco GPT/MBR para el kernel sintético Linux, lector VirtIO v1/v2 y código original de SeaBIOS en `src/`.

## Qué demuestra, y qué NO

El firmware puede elegir una partición MBR activa, leer un FAT32 real, cargar una imagen AArch64 de prueba y ejecutar esa segunda etapa. La ruta PE demuestra que puede interpretar ejecutables AArch64 de formato PE32+, **no que ejecute el administrador de arranque de Windows**.

Microsoft especifica UEFI para el arranque de Windows en plataformas ARM/SoC; la ruta BIOS/MBR heredada de Windows x86 no es una implementación portable a ARM64. Véase: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/minimum-uefi-requirements-for-windows-on-soc-platforms .

**La decisión de este proyecto es no incorporar UEFI.** Por ello, arrancar Windows ARM64 sin cambios sobre este firmware **no es una característica implementada ni una ruta compatible documentada por Microsoft**. Investigarlo requeriría diseñar un cargador Windows independiente, gestionar sus datos internos y reconstruir el entorno de arranque del kernel, HAL, memoria, CPU, ACPI, controladores y seguridad; una cadena de disco FAT32/PE por sí sola no resuelve esos contratos. No se debe comunicar como “Windows ARM arrancó”.

## Cómo reproducir

```sh
# Desde el directorio raíz del port
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
make ARCH=arm64 qemu-legacy-test

# El target legacy crea la imagen MBR/FAT32 y un ejecutable PE32+ sintético
# mediante Clang AArch64, lld-link y QEMU qemu-system-aarch64.
```

La prueba de la ruta legacy usa discos virtuales SPARSE de ~40 MiB, no instaladores Windows. El único uso de EFI en los tests es la cadena de texto de rechazo de imágenes que declararían tal subsistema, **no servicios firmware**. Se requieren Clang con targets `aarch64-none-elf` y `aarch64-pc-windows-msvc`, LLD, lld-link, llvm-objcopy, Python 3 y QEMU ARM64 con ROMs.

## Limitaciones de seguridad y alcance

- Sin verificación criptográfica de etapas, sin Secure Boot, sin autenticación de ejecutables.
- FAT32 sólo en lectura y nombres cortos 8.3; no LFN, FAT16, FAT12, NTFS, exFAT ni particiones extendidas.
- Solo particiones FAT32 activas, no arranque MBR de código máquina x86. No emula INT 13h ni real mode.
- La ruta PE nativa usa direcciones fijas en la RAM de QEMU `virt`, no soporta relocations, imports, ASLR ni todos los tipos PE.
- Ninguna etapa prueba compatibilidad con `bootmgfw.efi`, `winload.efi`, BCD ni `ntoskrnl.exe`.
- No flashear el firmware a hardware físico ni a Moto G20. Los datos de ejemplo no son imágenes de instalación reales.

## Próxima meta compatible con "legacy puro"

Definir un ABI de arranque AArch64 estable para una segunda etapa propia y ampliar el cargador PE nativo de forma aislada, validando cada requisito. Para Windows habría que estudiar los contratos no públicos de su cargador y núcleo en una investigación independiente; **no se asumirá que NT admite el ABI creado aquí**.
