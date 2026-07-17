LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin

# Use Homebrew LLVM clang only if it exists; otherwise fall back to clang on PATH
ifeq ($(wildcard $(LLVM_PREFIX)/clang),)
  CLANG ?= clang
  OBJCOPY ?= llvm-objcopy
else
  CLANG ?= $(LLVM_PREFIX)/clang
  OBJCOPY ?= $(LLVM_PREFIX)/llvm-objcopy
endif
QEMU ?= qemu-system-riscv64
BUILD := build
ELF := $(BUILD)/c26.elf
DISK := $(BUILD)/c26.img

CFLAGS := --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 \
	-mcmodel=medany -ffreestanding -fno-builtin -fno-stack-protector \
	-O2 -g -Wall -Wextra -Iinclude -MMD -MP
LDFLAGS := -fuse-ld=lld -nostdlib -nostartfiles -Wl,-T,src/linker.ld \
	-Wl,--gc-sections -Wl,--no-relax
HOSTCC ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Iinclude -Itests

SRCS := src/boot.S src/trap.S src/user_stubs.S src/uart.c src/console.c src/interrupts.c src/runtime.c src/virtio.c src/block.c src/fs.c src/input.c src/devices.c src/graphics.c \
	src/audio.c src/basic.c src/cart.c src/vm.c src/desktop.c src/framebuffer.c src/robot.c src/kernel.c
OBJS := $(patsubst src/%,$(BUILD)/%.o,$(SRCS))

CART_LDFLAGS := -fuse-ld=lld -nostdlib -nostartfiles -Wl,-T,apps/cart.ld \
	-Wl,--no-relax
CART_NAMES := paint crash spin ticker ping pong
CARTS := $(CART_NAMES:%=$(BUILD)/%.cart)

QEMU_MACHINE := -M virt -global virtio-mmio.force-legacy=false -cpu rv64 -m 256M
QEMU_BOOT := -bios none -no-reboot -kernel $(ELF)
QEMU_DEVICES := -device virtio-gpu-device -device virtio-keyboard-device \
	-device virtio-mouse-device -device virtio-sound-device \
	-drive if=none,format=raw,file=$(DISK),id=c26disk \
	-device virtio-blk-device,drive=c26disk

.PHONY: all build carts disk run run-headless smoke test check compdb clean

all: build carts compdb

build: $(ELF)

# Host-side unit tests: the BASIC interpreter and C26FS compile unmodified
# on the host against shims, so logic bugs surface without a QEMU boot.
TEST_BINS := $(BUILD)/host/test_basic $(BUILD)/host/test_fs

$(BUILD)/host/test_%: tests/test_%.c tests/host_shim.c tests/host_shim.h $(wildcard src/*.c include/*.h)
	mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOSTCFLAGS) $< tests/host_shim.c -o $@

test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; $$t; done

# The one gate: everything the machine claims, verified.
check: build carts test smoke

# compile_commands.json for clangd/IDE tooling.
compdb: $(ELF)
	@echo '[' > compile_commands.json
	@cat $(BUILD)/*.o.json >> compile_commands.json 2>/dev/null || true
	@echo '{}]' >> compile_commands.json

carts: $(CARTS)

disk: $(DISK)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.c.o: src/%.c
	mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -MJ $@.json -c $< -o $@

$(BUILD)/%.S.o: src/%.S
	mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

$(ELF): $(OBJS)
	$(CLANG) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(BUILD)/%.elf: apps/%/main.c apps/crt0.S apps/cart.ld include/c26_api.h | $(BUILD)
	$(CLANG) $(CFLAGS) $(CART_LDFLAGS) apps/crt0.S $< -o $@

$(BUILD)/%.cart: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(DISK): scripts/mkdisk.py scripts/fsinstall.py $(CARTS) | $(BUILD)
	python3 scripts/mkdisk.py $@
	python3 scripts/fsinstall.py $@ $(foreach c,$(CART_NAMES),$(shell echo $(c) | tr a-z A-Z)=$(BUILD)/$(c).cart)

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
