# Windows ARM64 and the no-UEFI constraint

No Windows ARM64 binary was booted or tested in v1.5. Native PE32+ diagnostics
prove only mapping, entry and limited DIR64 relocation of import-free test
programs. They do not implement Windows Boot Manager, winload, NT loader
parameters, HAL initialization, registry/BCD processing or Windows drivers.

Microsoft's [Windows SoC firmware requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms)
describe a supported boot path using UEFI, including an EFI system table,
memory/image/protocol boot services, ExitBootServices and runtime services.
That table also exposes ACPI RSDP and SMBIOS through EFI configuration entries.
Its platform setup section requires initialized boot devices, timers and
interrupt controllers, and describes PSCI or the Windows Arm CPU-start
protocol. The article describes Windows 10-era SoC platforms; it is evidence
of the documented interface dependencies, not a binary audit of every current
Windows ARM64 build.

The project explicitly excludes implementing those UEFI services. Therefore
following Microsoft's documented firmware chain is outside its scope. A
separate native legacy NT chain would have to demonstrate a usable kernel/HAL
entry contract, memory and hardware descriptions, CPU/interrupt startup and
boot-storage drivers, without assuming that loading PE sections suffices.
The existence of such a working chain has not been established here.

This is an engineering inference from the documented requirements and the
implemented firmware, not a proof that all conceivable non-UEFI NT loading
is impossible. Future research should identify exact dependencies in a
specific legally obtained Windows ARM64 release, document every missing ABI,
and keep Linux and synthetic-PE success separate from Windows evidence.
No compatibility percentage or Windows support claim is justified yet.
