CC = gcc
AS = x86_64-elf-as
LD = gcc

SRCDIR = .
BUILDDIR = build
INCLUDEDIR = include
LIMINEDIR = /usr/share/limine

CFLAGS = -std=gnu99 \
		 -ffreestanding \
		 -O2 \
		 -Wall \
		 -Wextra \
		 -I$(INCLUDEDIR) \
		 -mno-red-zone \
		 -fno-stack-protector \
		 -fno-stack-check \
		 -fno-PIC \
		 -fno-lto \
		 -fdata-sections \
		 -ffunction-sections \
		 -march=x86-64 \
		 -mcmodel=kernel \
		 -no-pie
ASFLAGS =
LDFLAGS = -T linker.ld -ffreestanding -O2 -nostdlib -lgcc -no-pie

TARGET = $(BUILDDIR)/os.bin
ISO_TARGET = $(BUILDDIR)/os.iso

C_SOURCES = $(shell find $(SRCDIR) -name '*.c' -not -path './$(BUILDDIR)/*')
ASM_SOURCES = $(shell find $(SRCDIR) -name '*.s' -not -path './$(BUILDDIR)/*')

C_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.o,$(ASM_SOURCES))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

all: $(TARGET)

run: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -display sdl -serial stdio -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run_debug: $(TARGET)
	qemu-system-x86_64 -cdrom $(ISO_TARGET) -display sdl -serial stdio -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 -s -S

run_cdrom: $(ISO_TARGET)
	qemu-system-x86_64 -cdrom $(ISO_TARGET) -enable-kvm -display sdl -serial stdio -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	@echo "Linking $(TARGET)..."
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.s | $(BUILDDIR)
	@echo "Assembling $<..."
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

cdrom: $(ISO_TARGET)

$(ISO_TARGET): $(TARGET)
	@echo "Creating ISO image..."
	@mkdir -p $(BUILDDIR)/iso/boot/limine

	cp $(TARGET) $(BUILDDIR)/iso/boot/os.bin
	cp limine.conf $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(BUILDDIR)/iso/boot/limine/

	mkdir -p $(BUILDDIR)/iso/EFI/BOOT
	cp $(LIMINEDIR)/BOOTX64.EFI $(LIMINEDIR)/BOOTIA32.EFI $(BUILDDIR)/iso/EFI/BOOT
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label $(BUILDDIR)/iso -o $(BUILDDIR)/os.iso

	@echo "ISO image created at $(ISO_TARGET)"

clean:
	rm -rf $(BUILDDIR)

compile_commands:
	compiledb -o $(BUILDDIR)/compile_commands.json make

format:
	clang-format -i $(shell find $(SRCDIR) \( -name '*.c' -o -name '*.h' \) -not -path './$(BUILDDIR)/*')

rebuild: clean all

.PHONY: all clean rebuild run run_debug run_cdrom cdrom