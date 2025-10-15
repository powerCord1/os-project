#!/bin/bash
set -e

./build.sh
qemu-system-i386 -kernel os.bin -display sdl