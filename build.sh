#!/bin/bash
set -e

i686-elf-as boot.s -o boot.o

i686-elf-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c drivers/tty.c -o drivers/tty.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c drivers/graphics.c -o drivers/graphics.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c libc/string.c -o libc/string.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c libc/math.c -o libc/math.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c libc/printf.c -o libc/printf.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c libc/putchar.c -o libc/putchar.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c panic.c -o panic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c debug.c -o debug.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c x86/pic.c -o x86/pic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c sys/power.c -o sys/power.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include
i686-elf-gcc -c x86/halt.c -o x86/halt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -I./include

i686-elf-gcc -T linker.ld -o os.bin -ffreestanding -O2 -nostdlib boot.o kernel.o drivers/tty.o libc/string.o drivers/graphics.o libc/math.o libc/printf.o libc/putchar.o panic.o debug.o x86/pic.c sys/power.o x86/halt.o -lgcc
