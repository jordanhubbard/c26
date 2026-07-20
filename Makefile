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

SRCS := src/boot.S src/trap.S src/user_stubs.S src/setjmp.S src/uart.c src/console.c src/interrupts.c src/runtime.c src/virtio.c src/block.c src/fs.c src/net.c src/input.c src/devices.c src/graphics.c \
	src/audio.c src/basic.c src/cart.c src/vm.c src/scheme.c src/scheme_glue.c src/desktop.c src/framebuffer.c src/robot.c src/kernel.c
OBJS := $(patsubst src/%,$(BUILD)/%.o,$(SRCS))

CART_LDFLAGS := -fuse-ld=lld -nostdlib -nostartfiles -Wl,-T,apps/cart.ld \
	-Wl,--no-relax
CART_NAMES := paint crash spin ticker ping pong files edit tracker breakout net asm \
              calc clock hexedit sheet robot snake monitor tinyc fetch
CART_LIB := $(wildcard apps/lib/*.c)
CARTS := $(CART_NAMES:%=$(BUILD)/%.cart)

QEMU_MACHINE := -M virt -global virtio-mmio.force-legacy=false -cpu rv64 -m 256M
QEMU_BOOT := -bios none -no-reboot -kernel $(ELF)
QEMU_DEVICES := -device virtio-gpu-device -device virtio-keyboard-device \
	-device virtio-mouse-device -device virtio-sound-device \
	-drive if=none,format=raw,file=$(DISK),id=c26disk \
	-device virtio-blk-device,drive=c26disk \
	-netdev user,id=net0,hostfwd=udp:127.0.0.1:12600-:2600,hostfwd=udp:127.0.0.1:12601-:2601 \
	-device virtio-net-device,netdev=net0

.PHONY: all build carts disk run run-headless run-vnc smoke test check compdb scheme-repl clean

all: build carts compdb

build: $(ELF)

# Host-side unit tests: the BASIC interpreter and C26FS compile unmodified
# on the host against shims, so logic bugs surface without a QEMU boot.
TEST_BINS := $(BUILD)/host/test_basic $(BUILD)/host/test_fs $(BUILD)/host/test_scheme $(BUILD)/host/test_dns

$(BUILD)/host/test_%: tests/test_%.c tests/host_shim.c tests/host_shim.h $(wildcard src/*.c include/*.h)
	mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOSTCFLAGS) $< tests/host_shim.c -o $@

# c26 Scheme is self-contained (no shim); explicit rules override the
# pattern above so it links only scheme.c.
$(BUILD)/host/test_scheme: tests/test_scheme.c src/scheme.c include/c26_scheme.h
	mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOSTCFLAGS) tests/test_scheme.c src/scheme.c -o $@

# The DNS codec is a header-only pure module; the test needs no shim or .c.
$(BUILD)/host/test_dns: tests/test_dns.c include/c26_dns.h
	mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOSTCFLAGS) tests/test_dns.c -o $@

$(BUILD)/host/scheme: tools/scheme_repl.c src/scheme.c include/c26_scheme.h
	mkdir -p $(BUILD)/host
	$(HOSTCC) $(HOSTCFLAGS) tools/scheme_repl.c src/scheme.c -o $@

# Build and launch the host Scheme REPL prototype.
scheme-repl: $(BUILD)/host/scheme
	@$(BUILD)/host/scheme

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

$(BUILD)/%.elf: apps/%/main.c apps/crt0.S apps/cart.ld include/c26_api.h $(CART_LIB) $(wildcard apps/lib/*.h) | $(BUILD)
	$(CLANG) $(CFLAGS) -Iapps/lib $(CART_LDFLAGS) apps/crt0.S $< $(CART_LIB) -o $@

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

# The graphical desktop served over VNC (127.0.0.1:5901) with a fully-emulated
# null audio backend — no host display or audio needed, so it works headless
# or over a remote shell. Connect with any VNC viewer (macOS: open vnc://...).
run-vnc: $(ELF) $(DISK)
	@echo "c26 desktop on VNC 127.0.0.1:5901 (open vnc://127.0.0.1:5901)"
	$(QEMU) $(QEMU_MACHINE) -display vnc=127.0.0.1:1 -serial stdio -monitor none \
		$(QEMU_BOOT) -device virtio-gpu-device -device virtio-keyboard-device \
		-device virtio-mouse-device -audiodev driver=none,id=audio0 \
		-device virtio-sound-device,audiodev=audio0 \
		-drive if=none,format=raw,file=$(DISK),id=c26disk \
		-device virtio-blk-device,drive=c26disk \
		-netdev user,id=net0 -device virtio-net-device,netdev=net0

smoke:
	python3 scripts/smoke.py

clean:
	rm -rf $(BUILD)
