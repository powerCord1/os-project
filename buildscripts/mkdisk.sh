#!/bin/sh
set -e

DISK_IMG="$1"
DISK_SIZE_MB="$2"
WASM_DIR="$3"

PART_OFFSET=2048
SECTOR_SIZE=512
TOTAL_SECTORS=$((DISK_SIZE_MB * 1024 * 1024 / SECTOR_SIZE))
PART_SECTORS=$((TOTAL_SECTORS - PART_OFFSET))

dd if=/dev/zero of="$DISK_IMG" bs=1M count="$DISK_SIZE_MB" 2>/dev/null

# MBR partition table: one FAT32 partition starting at sector 2048
printf '\x80'                          > /tmp/mbr_entry.bin
printf '\x00\x00\x00'                 >> /tmp/mbr_entry.bin
printf '\x0c'                         >> /tmp/mbr_entry.bin
printf '\x00\x00\x00'                 >> /tmp/mbr_entry.bin
python3 -c "
import struct, sys
sys.stdout.buffer.write(struct.pack('<I', $PART_OFFSET))
sys.stdout.buffer.write(struct.pack('<I', $PART_SECTORS))
" >> /tmp/mbr_entry.bin

dd if=/tmp/mbr_entry.bin of="$DISK_IMG" bs=1 seek=446 conv=notrunc 2>/dev/null
printf '\x55\xaa' | dd of="$DISK_IMG" bs=1 seek=510 conv=notrunc 2>/dev/null
rm -f /tmp/mbr_entry.bin

# Format the partition as FAT32 using mformat
MTOOLSRC=$(mktemp)
echo "drive z: file=\"$DISK_IMG\" partition=1" > "$MTOOLSRC"
export MTOOLSRC

mformat -F z: 2>/dev/null

# Copy WASM files
for f in "$WASM_DIR"/*.wm; do
    [ -f "$f" ] && mcopy -o "$f" z:/
done

rm -f "$MTOOLSRC"
