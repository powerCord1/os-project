# Allow explicit compiler specification via CROSS_PREFIX
# e.g., make CROSS_PREFIX=x86_64-elf-
# Set a default if not provided
CC = $(CROSS_PREFIX)gcc
AS = $(CROSS_PREFIX)as
LD = $(CROSS_PREFIX)gcc

ARCH ?= x86_64

SRCDIR = .
BUILDDIR = build
INCLUDEDIR = include

BUILD_VERSION := $(shell ./buildscripts/version.sh)
BUILD_TIME := $(shell date -u +'%Y-%m-%d %H:%M:%S')
COMMIT := $(shell git rev-parse --short HEAD)

# Auto-detect Limine installation directory
# Check common locations, allow override via LIMINEDIR variable
ifndef LIMINEDIR
  ifneq ($(shell test -d /usr/local/share/limine && echo "found"),)
    LIMINEDIR = /usr/local/share/limine
  else
    ifneq ($(shell test -d /usr/share/limine && echo "found"),)
      LIMINEDIR = /usr/share/limine
    else
      # Default fallback
      LIMINEDIR = /usr/share/limine
      $(warning Limine not found in common locations. Using default: $(LIMINEDIR))
      $(warning Set LIMINEDIR explicitly if Limine is installed elsewhere.)
      $(warning Example: make LIMINEDIR=/path/to/limine cdrom)
    endif
  endif
endif

# Verify Limine installation
LIMINE_CHECK := $(shell test -f $(LIMINEDIR)/limine-bios.sys && echo "found")
ifeq ($(LIMINE_CHECK),)
  $(warning Limine files not found in $(LIMINEDIR))
  $(warning Make sure Limine is installed or set LIMINEDIR to the correct path.)
endif

# Base CFLAGS that work on all architectures
CFLAGS = -std=gnu99 \
		 -ffreestanding \
		 -O0 \
		 -Wall \
		 -Wextra \
		 -DBUILD_VERSION="\"$(BUILD_VERSION)\"" \
		 -DBUILD_TIME="\"$(BUILD_TIME)\"" \
		 -DCOMMIT="\"$(COMMIT)\"" \
		 -I$(INCLUDEDIR) \
		 -Iwasm3/source \
		 -Dd_m3LogTimestamps=0 \
		 -Dd_m3MaxFunctionStackHeight=500 \
		 -Dd_m3MaxLinearMemoryPages=1024 \
		 -fno-stack-protector \
		 -fno-stack-check \
		 -fno-PIC \
		 -fno-lto \
		 -fdata-sections \
		 -ffunction-sections \
		 -no-pie \
		 -g3

# Architecture-specific flags (only for x86_64)
# When targeting x86_64, we MUST use a cross-compiler because:
# 1. The code contains x86-64 specific inline assembly
# 2. The assembly files are x86-64 specific
# 3. Native ARM gcc cannot compile x86-64 code
ifeq ($(ARCH),x86_64)
  CFLAGS += -mno-red-zone \
			-march=x86-64 \
			-mcmodel=kernel
else
  ifneq ($(CROSS_PREFIX),)
    # Using cross-compiler, safe to add x86-specific flags
    CFLAGS += -mno-red-zone \
			-march=x86-64 \
			-mcmodel=kernel
#   else
    # No cross-compiler detected - this will fail because:
    # - C code has x86-64 inline assembly that ARM assembler can't handle
    # - Assembly files are x86-64 specific
#     $(error Cannot compile x86_64 code without a cross-compiler. \
#             The code contains x86-64 specific inline assembly and assembly files. \
#             Please install a cross-compiler or set CROSS_PREFIX. \
#             \
#             To install on Ubuntu/Debian: \
#               sudo apt-get install gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
#             Then use: make CROSS_PREFIX=x86_64-linux-gnu- \
#             \
#             Or for a bare-metal toolchain, install x86_64-elf-gcc and use: \
#               make CROSS_PREFIX=x86_64-elf-)
  endif
endif

ASFLAGS =
LDFLAGS = -T linker.ld -ffreestanding -O2 -nostdlib -lgcc -no-pie

TARGET = $(BUILDDIR)/os.bin
ISO_TARGET = $(BUILDDIR)/os.iso
DISK_IMG = disk.img
DISK_SIZE = 32

# Userspace (WASM) build settings
WASM_CC = clang
WASM_CFLAGS = --target=wasm32 -nostdlib -O2
WASM_LDFLAGS = -Wl,--no-entry -Wl,--allow-undefined
WASM_SRCDIR = userspace
WASM_SOURCES = $(wildcard $(WASM_SRCDIR)/*.c)
WASM_BINARIES = $(patsubst $(WASM_SRCDIR)/%.c,$(BUILDDIR)/wasm/%.wm,$(WASM_SOURCES))

# Find source files
WASM3_C_SOURCES = ./wasm3/source/m3_bind.c ./wasm3/source/m3_code.c ./wasm3/source/m3_compile.c \
                  ./wasm3/source/m3_core.c ./wasm3/source/m3_env.c ./wasm3/source/m3_exec.c \
                  ./wasm3/source/m3_function.c ./wasm3/source/m3_info.c ./wasm3/source/m3_module.c \
                  ./wasm3/source/m3_parse.c
C_SOURCES_REGULAR = $(shell find $(SRCDIR) -name '*.c' -not -path './$(BUILDDIR)/*' -not -path './data/*' -not -path './userspace/*' -not -path './wasm3/*') $(WASM3_C_SOURCES)
ASM_SOURCES = $(shell find $(SRCDIR) -name '*.s' -not -path './$(BUILDDIR)/*')
DATA_SOURCES = $(wildcard data/*)
RESOURCE_C_SOURCES = $(patsubst data/%,data/%.c,$(filter-out %.c,$(DATA_SOURCES)))
C_SOURCES = $(C_SOURCES_REGULAR) $(RESOURCE_C_SOURCES)

C_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.o,$(ASM_SOURCES))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

QEMU_PREFIX = -enable-kvm -machine q35,acpi=on -cpu host -display sdl -serial stdio -drive format=raw,file=disk.img,if=ide -boot d

all: $(TARGET)

wasm: $(WASM_BINARIES)

$(BUILDDIR)/wasm/%.wm: $(WASM_SRCDIR)/%.c $(WASM_SRCDIR)/api.h
	@echo "Compiling WASM $<..."
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) $(WASM_LDFLAGS) -Wl,--export=_start -o $@ $<

disk: wasm
	@echo "Creating disk image..."
	@./buildscripts/mkdisk.sh $(DISK_IMG) $(DISK_SIZE) $(BUILDDIR)/wasm
	@echo "Disk image created: $(DISK_IMG)"

run: $(ISO_TARGET) wasm disk
	qemu-system-x86_64 -cdrom $(ISO_TARGET) $(QEMU_PREFIX) -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run_noaudio: $(ISO_TARGET) wasm disk
	qemu-system-x86_64 -cdrom $(ISO_TARGET) $(QEMU_PREFIX)

run_debug: $(ISO_TARGET) wasm disk
	qemu-system-x86_64 -cdrom $(ISO_TARGET) $(QEMU_PREFIX) -s -S

data/%.c: data/%
	@echo "Generating C source for $<..."
	@./buildscripts/resources.sh

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	@echo "Linking $(TARGET)..."
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILDDIR)/%.o: $(SRCDIR)/%.s
	@echo "Assembling $<..."
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

cdrom: $(ISO_TARGET)

$(ISO_TARGET): $(TARGET)
	@echo "Creating ISO image..."
	@mkdir -p $(BUILDDIR)/iso/boot/limine

	cp $(TARGET) $(BUILDDIR)/iso/boot/os.bin
	cp limine.conf $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(BUILDDIR)/iso/boot/limine/

	# UEFI boot files (optional - newer Limine versions may not need these)
	mkdir -p $(BUILDDIR)/iso/EFI/BOOT
	@if [ -f $(LIMINEDIR)/BOOTX64.EFI ]; then \
		cp $(LIMINEDIR)/BOOTX64.EFI $(BUILDDIR)/iso/EFI/BOOT/; \
	else \
		echo "Note: BOOTX64.EFI not found, UEFI boot will use limine-uefi-cd.bin"; \
	fi
	@if [ -f $(LIMINEDIR)/BOOTIA32.EFI ]; then \
		cp $(LIMINEDIR)/BOOTIA32.EFI $(BUILDDIR)/iso/EFI/BOOT/; \
	fi
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label $(BUILDDIR)/iso -o $(BUILDDIR)/os.iso

	@echo "ISO image created at $(ISO_TARGET)"

clean:
	rm -rf $(BUILDDIR)

clean_disk:
	rm -f $(DISK_IMG)

compile_commands:
	compiledb -o $(BUILDDIR)/compile_commands.json make

format:
	clang-format -i $(shell find $(SRCDIR) \( -name '*.c' -o -name '*.h' \) -not -path './$(BUILDDIR)/*')

rebuild: clean all

.PHONY: all _build clean clean_disk rebuild run run_vm run_debug run_cdrom run_cdrom_vm cdrom compile_commands wasm disk