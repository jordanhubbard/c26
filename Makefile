LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin
CLANG ?= $(LLVM_PREFIX)/clang
QEMU ?= qemu-system-riscv64
BUILD := build
ELF := $(BUILD)/c26.elf

CFLAGS := --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 \
	-mcmodel=medany -ffreestanding -fno-builtin -fno-stack-protector \
	-nostdlib -nostartfiles -O2 -g -Wall -Wextra -Iinclude
LDFLAGS := -fuse-ld=lld -nostdlib -Wl,-T,src/linker.ld \
	-Wl,--gc-sections -Wl,--no-relax

SRCS := src/boot.S src/uart.c src/runtime.c src/devices.c src/graphics.c \
	src/audio.c src/basic.c src/desktop.c src/robot.c src/kernel.c
OBJS := $(patsubst src/%,$(BUILD)/%.o,$(SRCS))

.PHONY: all build run smoke clean

all: build

build: $(ELF)

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

run: $(ELF)
	$(QEMU) -M virt -cpu rv64 -m 256M -nographic -bios none -no-reboot -kernel $(ELF)

smoke:
	python3 scripts/smoke.py

clean:
	rm -rf $(BUILD)
