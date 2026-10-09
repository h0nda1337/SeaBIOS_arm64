# SPDX-License-Identifier: LGPL-3.0-or-later
# Independent AArch64 build: leaves the SeaBIOS x86 Makefile untouched.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ARM := $(ROOT)/arm64
OUT := $(ARM)/out
CLANG ?= clang
LLD ?= ld.lld
OBJCOPY ?= llvm-objcopy
QEMU ?= qemu-system-aarch64
CFLAGS := --target=aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 \
          -Wall -Wextra -Werror -Wno-unused-parameter -I$(ARM)
# Building original SeaBIOS src/e820map.c and using its src/list.h is an
# incremental step toward shared subsystems; this is not the full POST port.
CFLAGS += -DCONFIG_AARCH64=1 -I$(ROOT)/arm64/include
OBJS := $(OUT)/start.o $(OUT)/boot.o $(OUT)/uart.o $(OUT)/fdt.o \
        $(OUT)/post.o $(OUT)/bootorder.o $(OUT)/string.o \
        $(OUT)/compat.o $(OUT)/e820map.o $(OUT)/romfile.o \
        $(OUT)/heap.o $(OUT)/fwcfg.o $(OUT)/exception.o $(OUT)/virtio.o

.PHONY: all clean run test qemu-test
all: $(OUT)/seabios-arm64.bin $(OUT)/diagnostic-payload.bin

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: $(ARM)/%.c $(ARM)/include/arm64.h | $(OUT)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/e820map.o: $(ROOT)/src/e820map.c $(ROOT)/src/e820map.h \
                    $(ROOT)/src/types.h $(ARM)/include/seabios_compat.h | $(OUT)
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

run: all
	$(QEMU) -machine virt -cpu cortex-a57 -m 256M -smp 1 -nographic \
	    -bios $(OUT)/seabios-arm64.bin \
	    -device loader,file=$(OUT)/diagnostic-payload.bin,addr=0x48000000

test:
	python3 $(ARM)/tests/test_fdt.py
	python3 $(ARM)/tests/test_payload.py

qemu-test: all
	QEMU=$(QEMU) python3 $(ARM)/tests/test_qemu.py

clean:
	rm -rf $(OUT)
