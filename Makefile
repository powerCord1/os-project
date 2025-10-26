CC = i686-elf-gcc
AS = i686-elf-as
LD = i686-elf-gcc

SRCDIR = .
BUILDDIR = build
INCLUDEDIR = include

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I$(INCLUDEDIR)
ASFLAGS = 
LDFLAGS = -T linker.ld -ffreestanding -O2 -nostdlib -lgcc

TARGET = $(BUILDDIR)/os.bin

C_SOURCES = $(shell find $(SRCDIR) -name '*.c' -not -path './$(BUILDDIR)/*')
ASM_SOURCES = $(shell find $(SRCDIR) -name '*.s' -not -path './$(BUILDDIR)/*')

C_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.o,$(ASM_SOURCES))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

all: $(TARGET)

run: $(TARGET)
	qemu-system-i386 -kernel $(TARGET) -display sdl

run_debug: $(TARGET)
	qemu-system-i386 -kernel $(TARGET) -display sdl -s -S

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

clean:
	rm -rf $(BUILDDIR)

compile_commands:
	compiledb -o $(BUILDDIR)/compile_commands.json make

format:
	clang-format -i $(shell find $(SRCDIR) \( -name '*.c' -o -name '*.h' \) -not -path './$(BUILDDIR)/*')

rebuild: clean all

.PHONY: all clean rebuild run