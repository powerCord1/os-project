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

# MBR partition table: one Linux partition (type 0x83) starting at sector 2048
printf '\x80'                          > /tmp/mbr_entry.bin
printf '\x00\x00\x00'                 >> /tmp/mbr_entry.bin
printf '\x83'                         >> /tmp/mbr_entry.bin
printf '\x00\x00\x00'                 >> /tmp/mbr_entry.bin
python3 -c "
import struct, sys
sys.stdout.buffer.write(struct.pack('<I', $PART_OFFSET))
sys.stdout.buffer.write(struct.pack('<I', $PART_SECTORS))
" >> /tmp/mbr_entry.bin

dd if=/tmp/mbr_entry.bin of="$DISK_IMG" bs=1 seek=446 conv=notrunc 2>/dev/null
printf '\x55\xaa' | dd of="$DISK_IMG" bs=1 seek=510 conv=notrunc 2>/dev/null
rm -f /tmp/mbr_entry.bin

# Extract partition into a separate file, format as ext2, copy files with debugfs
PART_IMG=$(mktemp)
PART_OFFSET_BYTES=$((PART_OFFSET * SECTOR_SIZE))
PART_SIZE_BYTES=$((PART_SECTORS * SECTOR_SIZE))

dd if=/dev/zero of="$PART_IMG" bs=512 count="$PART_SECTORS" 2>/dev/null
mkfs.ext2 -q "$PART_IMG"

# Build debugfs command script to copy WASM files
DEBUGFS_CMDS=$(mktemp)
for f in "$WASM_DIR"/*.wm; do
    [ -f "$f" ] && echo "write $f $(basename "$f")" >> "$DEBUGFS_CMDS"
done

debugfs -w -f "$DEBUGFS_CMDS" "$PART_IMG" 2>/dev/null

# Write partition back into disk image
dd if="$PART_IMG" of="$DISK_IMG" bs=512 seek="$PART_OFFSET" conv=notrunc 2>/dev/null

rm -f "$PART_IMG" "$DEBUGFS_CMDS"
