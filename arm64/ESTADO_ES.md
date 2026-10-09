# SeaBIOS-ARM64 — estado del primer prototipo

## Entregado en esta revisión

Se agregó `arm64/` al árbol original de SeaBIOS sin cambiar ni un archivo del
firmware x86. El prototipo se compila con Clang/LLD para AArch64 y produce un
firmware binario pensado para la máquina virtual `virt` de QEMU.

- Entrada ARM64 con pila, copia de datos inicializados y limpieza de BSS.
- Consola serial PL011 con detección de la dirección mediante Device Tree.
- Lector de Device Tree con límites y detección de memoria/PL011.
- Transferencia de control a una pequeña rutina ARM64 de diagnóstico cargada
  de forma externa por QEMU. **No arranca Windows ni Linux**.
- 12 pruebas automatizadas en el equipo anfitrión, aprobadas.

## Limitaciones importantes

**La prueba de ejecución real con QEMU está pendiente**. La máquina de desarrollo
no tenía `qemu-system-aarch64`; la compilación y los tests del lector FDT no
sustituyen la ejecución del firmware emulado.

No implementa las llamadas de BIOS x86 (`INT 13h`, etc.). No contiene Boot
Services de UEFI, ACPI, cargador PE/COFF, controladores de disco, MMU,
PSCI ni un protocolo de arranque de SO.

Para Windows ARM64 será necesario implementar UEFI (la alternativa más práctica
es integrar/reutilizar EDK2) y completar las interfaces que necesita Windows.

## Probar en Debian/WSL

```bash
sudo apt-get update
sudo apt-get install clang lld llvm qemu-system-arm python3 make
cd seabios
make -f arm64/Makefile
make -f arm64/Makefile test
make -f arm64/Makefile qemu-test
```

Resultados esperados de `qemu-test`: dos líneas `PASS`, una sin payload y otra
con el payload de diagnóstico. Si falla, compartir el resultado completo y la
versión de QEMU para corregir el siguiente paso.

Para revisar el resultado manualmente:

```bash
make -f arm64/Makefile run
```

La ventana de QEMU se cierra pulsando `Ctrl+A`, y después `X`.

## Próximo paso

Verificar la salida serial en QEMU y luego implementar temporizador/excepciones,
PSCI/EL, detección de bloques VirtIO y un protocolo real de arranque de ARM64.
La rama Windows ARM64 requerirá UEFI y los servicios de plataforma completos.
