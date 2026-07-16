LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin

# Use Homebrew LLVM clang only if it exists; otherwise fall back to clang on PATH
ifeq ($(wildcard $(LLVM_PREFIX)/clang),)
  CLANG ?= clang
else
  CLANG ?= $(LLVM_PREFIX)/clang
endif
QEMU ?= qemu-system-riscv64
BUILD := build
ELF := $(BUILD)/c26.elf
DISK := $(BUILD)/c26.img

CFLAGS := --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 \
	-mcmodel=medany -ffreestanding -fno-builtin -fno-stack-protector \
	-O2 -g -Wall -Wextra -Iinclude
LDFLAGS := -fuse-ld=lld -nostdlib -nostartfiles -Wl,-T,src/linker.ld \
	-Wl,--gc-sections -Wl,--no-relax

SRCS := src/boot.S src/trap.S src/uart.c src/interrupts.c src/runtime.c src/virtio.c src/block.c src/fs.c src/input.c src/devices.c src/graphics.c \
	src/audio.c src/basic.c src/desktop.c src/framebuffer.c src/robot.c src/kernel.c
OBJS := $(patsubst src/%,$(BUILD)/%.o,$(SRCS))

QEMU_MACHINE := -M virt -global virtio-mmio.force-legacy=false -cpu rv64 -m 256M
QEMU_BOOT := -bios none -no-reboot -kernel $(ELF)
QEMU_DEVICES := -device virtio-gpu-device -device virtio-keyboard-device \
	-device virtio-mouse-device -device virtio-sound-device \
	-drive if=none,format=raw,file=$(DISK),id=c26disk \
	-device virtio-blk-device,drive=c26disk

.PHONY: all build disk run run-headless smoke clean

all: build

build: $(ELF)

disk: $(DISK)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.c.o: src/%.c
	mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD)/%.S.o: src/%.S
	mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(CLANG) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(DISK): scripts/mkdisk.py | $(BUILD)
	python3 scripts/mkdisk.py $@

run: $(ELF) $(DISK)
	$(QEMU) $(QEMU_MACHINE) -display default -serial stdio -monitor none \
		$(QEMU_BOOT) $(QEMU_DEVICES)

run-headless: $(ELF) $(DISK)
	$(QEMU) $(QEMU_MACHINE) -display none -serial stdio -monitor none \
		$(QEMU_BOOT) -device virtio-gpu-device -device virtio-keyboard-device \
		-device virtio-mouse-device -audiodev driver=none,id=audio0 \
		-device virtio-sound-device,audiodev=audio0 \
		-drive if=none,format=raw,file=$(DISK),id=c26disk \
		-device virtio-blk-device,drive=c26disk

smoke:
	python3 scripts/smoke.py

clean:
	rm -rf $(BUILD)
