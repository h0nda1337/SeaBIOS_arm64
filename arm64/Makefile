# SPDX-License-Identifier: LGPL-3.0-or-later
# Independent AArch64 build: leaves the SeaBIOS x86 Makefile untouched.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ARM := $(ROOT)/arm64
OUT := $(ARM)/out
CLANG ?= clang
LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
QEMU ?= qemu-system-aarch64
CFLAGS := --target=aarch64-none-elf -march=armv8-a -ffreestanding -mgeneral-regs-only -mstrict-align -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 \
          -Wall -Wextra -Werror -Wno-unused-parameter -I$(ARM)
# Building original SeaBIOS src/e820map.c and using its src/list.h is an
# incremental step toward shared subsystems; this is not the full POST port.
CFLAGS += -DCONFIG_AARCH64=1 -I$(ROOT)/arm64/include -I$(ROOT)/src
OBJS := $(OUT)/start.o $(OUT)/boot.o $(OUT)/uart.o $(OUT)/fdt.o \
        $(OUT)/post.o $(OUT)/postflow.o $(OUT)/bootorder.o $(OUT)/string.o \
        $(OUT)/compat.o $(OUT)/e820map.o $(OUT)/romfile.o \
        $(OUT)/heap.o $(OUT)/fwcfg.o $(OUT)/bootprio.o $(OUT)/sha256.o $(OUT)/exception.o $(OUT)/virtio-mmio.o \
        $(OUT)/block.o $(OUT)/blockpolicy.o $(OUT)/blockgeom.o $(OUT)/diskprobe.o $(OUT)/linuxboot.o $(OUT)/linuximage.o \
        $(OUT)/virtio-ring.o $(OUT)/gpt.o $(OUT)/gptcrc.o $(OUT)/timer.o \
        $(OUT)/fdt_boot.o $(OUT)/fat32.o $(OUT)/legacyboot.o $(OUT)/pe_legacy.o

.PHONY: all clean run test qemu-test qemu-multidrive-test qemu-multiboot-test qemu-disk-test qemu-legacy-test qemu-bootorder-test qemu-reloc-test qemu-real-linux-test qemu-fat-linux-test
all: $(OUT)/seabios-arm64.bin $(OUT)/diagnostic-payload.bin

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: $(ARM)/%.c $(ARM)/include/arm64.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/virtio-mmio.o: $(ROOT)/src/hw/virtio-mmio.c \
                         $(ARM)/include/arm64.h $(ARM)/include/virtio_adapter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/virtio-ring.o: $(ROOT)/src/hw/virtio-ring.c \
                         $(ROOT)/src/hw/virtio-ring.h \
                         $(ARM)/include/virtio_adapter.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/e820map.o: $(ROOT)/src/e820map.c $(ROOT)/src/e820map.h \
                    $(ROOT)/src/types.h $(ARM)/include/seabios_compat.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/postflow.o: $(ROOT)/src/postflow.c $(ROOT)/src/postflow.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/blockgeom.o: $(ROOT)/src/blockgeom.c $(ROOT)/src/blockgeom.h $(ROOT)/src/block.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/blockpolicy.o: $(ROOT)/src/blockpolicy.c $(ROOT)/src/blockpolicy.h $(ROOT)/src/block.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/sha256.o: $(ROOT)/src/sha256.c $(ROOT)/src/sha.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/bootprio.o: $(ROOT)/src/bootprio.c $(ROOT)/src/bootprio.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/romfile.o: $(ROOT)/src/romfile.c $(ROOT)/src/romfile.h \
                   $(ARM)/include/seabios_compat.h | $(OUT)
	$(CLANG) $(CFLAGS) -Wno-sign-compare -c $< -o $@

$(OUT)/start.o: $(ARM)/start.S | $(OUT)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@

$(OUT)/seabios-arm64.elf: $(OBJS) $(ARM)/linker.ld
	$(LLD) --no-undefined -T $(ARM)/linker.ld -Map=$(OUT)/firmware.map -o $@ $(OBJS)

$(OUT)/seabios-arm64.bin: $(OUT)/seabios-arm64.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/diagnostic-code.o: $(ARM)/tests/diagnostic.S | $(OUT)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@

$(OUT)/diagnostic-code.elf: $(OUT)/diagnostic-code.o
	$(LLD) -Ttext=0x48000040 --entry=diagnostic_start -o $@ $<

$(OUT)/diagnostic-code.bin: $(OUT)/diagnostic-code.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/diagnostic-payload.bin: $(OUT)/diagnostic-code.bin $(ARM)/tools/pack_payload.py
	python3 $(ARM)/tools/pack_payload.py $< $@

$(OUT)/linux-image-diagnostic.o: $(ARM)/tests/linux_image_diagnostic.S | $(OUT)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@

$(OUT)/linux-image-diagnostic.elf: $(OUT)/linux-image-diagnostic.o
	$(LLD) -Ttext=0x48080000 --entry=synthetic_image_start -o $@ $<

$(OUT)/linux-image-diagnostic.bin: $(OUT)/linux-image-diagnostic.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/linux-test-disk.img: $(OUT)/linux-image-diagnostic.bin $(ARM)/tools/create_linux_image_test_disk.py
	python3 $(ARM)/tools/create_linux_image_test_disk.py $< $@

$(OUT)/legacy-stage.o: $(ARM)/tests/legacy_stage.S | $(OUT)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@

$(OUT)/legacy-stage.elf: $(OUT)/legacy-stage.o
	$(LLD) -Ttext=0x49000040 --entry=legacy_stage_start -o $@ $<

$(OUT)/legacy-stage.bin: $(OUT)/legacy-stage.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/legacy-pe-stage.obj: $(ARM)/tests/legacy_pe_stage.S | $(OUT)
	$(CLANG) --target=aarch64-pc-windows-msvc -c $< -o $@

$(OUT)/legacy-pe-stage.exe: $(OUT)/legacy-pe-stage.obj
	lld-link /machine:arm64 /subsystem:native /entry:pe_native_start \
	    /base:0x4a000000 /nodefaultlib /opt:ref /out:$@ $<

$(OUT)/legacy-pe-reloc-stage.obj: $(ARM)/tests/legacy_pe_reloc_stage.S | $(OUT)
	$(CLANG) --target=aarch64-pc-windows-msvc -c $< -o $@

$(OUT)/legacy-pe-reloc-stage.exe: $(OUT)/legacy-pe-reloc-stage.obj
	lld-link /machine:arm64 /subsystem:native /entry:pe_native_reloc_start \
	    /base:0x4b000000 /dynamicbase /fixed:no /nodefaultlib /opt:ref /out:$@ $<

$(OUT)/legacy-mbr-test-disk.img: $(OUT)/legacy-stage.bin $(ARM)/tools/create_legacy_mbr_disk.py
	python3 $(ARM)/tools/create_legacy_mbr_disk.py $< $@

qemu-bootorder-test: all $(OUT)/legacy-mbr-test-disk.img
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_bootorder.py

qemu-legacy-test: all $(OUT)/legacy-mbr-test-disk.img $(OUT)/legacy-pe-stage.exe
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_legacy.py

qemu-reloc-test: all $(OUT)/legacy-pe-reloc-stage.exe
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_reloc.py

run: all
	$(QEMU) -machine virt -cpu cortex-a57 -m 256M -smp 1 -nographic \
	    -bios $(OUT)/seabios-arm64.bin \
	    -device loader,file=$(OUT)/diagnostic-payload.bin,addr=0x48000000

test:
	python3 $(ARM)/tests/test_blockgeom.py
	python3 $(ARM)/tests/test_postflow.py
	python3 $(ARM)/tests/test_blockpolicy.py
	cd $(ROOT) && python3 -m unittest arm64.tests.test_real_linux_image -v
	python3 $(ARM)/tests/test_shared_core.py
	python3 $(ARM)/tests/test_fdt_boot.py
	python3 $(ARM)/tests/test_fdt.py
	python3 $(ARM)/tests/test_payload.py
	python3 $(ARM)/tests/test_linuximage.py
	python3 $(ARM)/tests/test_gptcrc.py

qemu-test: all
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu.py

qemu-multidrive-test: all
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_multidrive.py

qemu-disk-test: all $(OUT)/linux-test-disk.img
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_disk.py

clean:
	rm -rf $(OUT)

# Requires an independently supplied *genuine* Linux ARM64 Image. Unlike the
# old synthetic handoff tests, success requires a Linux userspace /init marker.
qemu-real-linux-test: all
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_real_linux.py \
	    --image "$(ARM64_REAL_LINUX_IMAGE)"

# Phase 10: independent disk selection, native MBR/FAT32 stage2 and fw_cfg bootindex.
qemu-multiboot-test: all $(OUT)/legacy-stage.bin $(OUT)/linux-image-diagnostic.bin
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_multiboot.py

# v1.4: FAT32 file-based Linux Image on active MBR (no UEFI).
qemu-fat-linux-test: all $(OUT)/linux-image-diagnostic.bin
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu_fat_linux.py
