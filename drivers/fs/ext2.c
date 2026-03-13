#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <disk.h>
#include <ext2.h>
#include <fs.h>
#include <heap.h>
#include <string.h>

static bool ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buf)
{
    uint64_t lba = fs->lba_start + (uint64_t)block_num * fs->sectors_per_block;
    return disk_read(fs->disk_id, lba, fs->sectors_per_block, buf);
}

static bool ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buf)
{
    uint64_t lba = fs->lba_start + (uint64_t)block_num * fs->sectors_per_block;
    return disk_write(fs->disk_id, lba, fs->sectors_per_block, buf);
}

static bool ext2_read_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *out)
{
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block = fs->group_descs[group].bg_inode_table + index / inodes_per_block;
    uint32_t offset = (index % inodes_per_block) * fs->inode_size;

    if (!ext2_read_block(fs, block, fs->block_buf))
        return false;
    memcpy(out, fs->block_buf + offset, sizeof(ext2_inode_t));
    return true;
}

static bool ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const ext2_inode_t *inode)
{
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block = fs->group_descs[group].bg_inode_table + index / inodes_per_block;
    uint32_t offset = (index % inodes_per_block) * fs->inode_size;

    if (!ext2_read_block(fs, block, fs->block_buf))
        return false;
    memcpy(fs->block_buf + offset, inode, sizeof(ext2_inode_t));
    return ext2_write_block(fs, block, fs->block_buf);
}

static uint32_t ext2_bmap(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical)
{
    uint32_t n = fs->addrs_per_block;

    if (logical < EXT2_DIRECT_BLOCKS)
        return inode->i_block[logical];

    logical -= EXT2_DIRECT_BLOCKS;

    if (logical < n) {
        uint32_t ind_block = inode->i_block[EXT2_IND_BLOCK];
        if (!ind_block)
            return 0;
        uint8_t *buf = (uint8_t *)malloc(fs->block_size);
        if (!buf)
            return 0;
        if (!ext2_read_block(fs, ind_block, buf)) {
            free(buf);
            return 0;
        }
        uint32_t result = ((uint32_t *)buf)[logical];
        free(buf);
        return result;
    }
    logical -= n;

    if (logical < n * n) {
        uint32_t dind_block = inode->i_block[EXT2_DIND_BLOCK];
        if (!dind_block)
            return 0;
        uint8_t *buf = (uint8_t *)malloc(fs->block_size);
        if (!buf)
            return 0;
        if (!ext2_read_block(fs, dind_block, buf)) {
            free(buf);
            return 0;
        }
        uint32_t ind = ((uint32_t *)buf)[logical / n];
        if (!ind) {
            free(buf);
            return 0;
        }
        if (!ext2_read_block(fs, ind, buf)) {
            free(buf);
            return 0;
        }
        uint32_t result = ((uint32_t *)buf)[logical % n];
        free(buf);
        return result;
    }
    logical -= n * n;

    if (logical < n * n * n) {
        uint32_t tind_block = inode->i_block[EXT2_TIND_BLOCK];
        if (!tind_block)
            return 0;
        uint8_t *buf = (uint8_t *)malloc(fs->block_size);
        if (!buf)
            return 0;
        if (!ext2_read_block(fs, tind_block, buf)) {
            free(buf);
            return 0;
        }
        uint32_t dind = ((uint32_t *)buf)[logical / (n * n)];
        if (!dind) {
            free(buf);
            return 0;
        }
        if (!ext2_read_block(fs, dind, buf)) {
            free(buf);
            return 0;
        }
        uint32_t ind = ((uint32_t *)buf)[(logical / n) % n];
        if (!ind) {
            free(buf);
            return 0;
        }
        if (!ext2_read_block(fs, ind, buf)) {
            free(buf);
            return 0;
        }
        uint32_t result = ((uint32_t *)buf)[logical % n];
        free(buf);
        return result;
    }

    return 0;
}

static bool ext2_lookup(ext2_fs_t *fs, uint32_t dir_ino, const char *name,
                         uint32_t *found_ino)
{
    ext2_inode_t dir;
    if (!ext2_read_inode(fs, dir_ino, &dir))
        return false;

    uint32_t num_blocks = (dir.i_size + fs->block_size - 1) / fs->block_size;
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf)
        return false;

    size_t name_len = strlen(name);
    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_bmap(fs, &dir, b);
        if (!phys || !ext2_read_block(fs, phys, buf)) {
            free(buf);
            return false;
        }

        uint32_t offset = 0;
        while (offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + offset);
            if (de->rec_len == 0)
                break;
            if (de->inode != 0 && de->name_len == name_len &&
                memcmp(de->name, name, name_len) == 0) {
                *found_ino = de->inode;
                free(buf);
                return true;
            }
            offset += de->rec_len;
        }
    }

    free(buf);
    return false;
}

static uint32_t ext2_alloc_block(ext2_fs_t *fs, uint32_t preferred_group)
{
    uint8_t *bitmap = (uint8_t *)malloc(fs->block_size);
    if (!bitmap)
        return 0;

    for (uint32_t i = 0; i < fs->num_groups; i++) {
        uint32_t g = (preferred_group + i) % fs->num_groups;
        if (fs->group_descs[g].bg_free_blocks_count == 0)
            continue;

        if (!ext2_read_block(fs, fs->group_descs[g].bg_block_bitmap, bitmap)) {
            free(bitmap);
            return 0;
        }

        for (uint32_t j = 0; j < fs->blocks_per_group; j++) {
            if (!(bitmap[j / 8] & (1 << (j % 8)))) {
                bitmap[j / 8] |= (1 << (j % 8));
                ext2_write_block(fs, fs->group_descs[g].bg_block_bitmap, bitmap);
                fs->group_descs[g].bg_free_blocks_count--;
                fs->sb.s_free_blocks_count--;
                free(bitmap);
                return g * fs->blocks_per_group + j + fs->first_data_block;
            }
        }
    }

    free(bitmap);
    return 0;
}

static void ext2_free_block(ext2_fs_t *fs, uint32_t block_num)
{
    uint32_t adjusted = block_num - fs->first_data_block;
    uint32_t g = adjusted / fs->blocks_per_group;
    uint32_t idx = adjusted % fs->blocks_per_group;

    uint8_t *bitmap = (uint8_t *)malloc(fs->block_size);
    if (!bitmap)
        return;
    if (!ext2_read_block(fs, fs->group_descs[g].bg_block_bitmap, bitmap)) {
        free(bitmap);
        return;
    }

    bitmap[idx / 8] &= ~(1 << (idx % 8));
    ext2_write_block(fs, fs->group_descs[g].bg_block_bitmap, bitmap);
    fs->group_descs[g].bg_free_blocks_count++;
    fs->sb.s_free_blocks_count++;
    free(bitmap);
}

static uint32_t ext2_alloc_inode(ext2_fs_t *fs, uint32_t preferred_group)
{
    uint8_t *bitmap = (uint8_t *)malloc(fs->block_size);
    if (!bitmap)
        return 0;

    for (uint32_t i = 0; i < fs->num_groups; i++) {
        uint32_t g = (preferred_group + i) % fs->num_groups;
        if (fs->group_descs[g].bg_free_inodes_count == 0)
            continue;

        if (!ext2_read_block(fs, fs->group_descs[g].bg_inode_bitmap, bitmap)) {
            free(bitmap);
            return 0;
        }

        for (uint32_t j = 0; j < fs->inodes_per_group; j++) {
            if (!(bitmap[j / 8] & (1 << (j % 8)))) {
                bitmap[j / 8] |= (1 << (j % 8));
                ext2_write_block(fs, fs->group_descs[g].bg_inode_bitmap, bitmap);
                fs->group_descs[g].bg_free_inodes_count--;
                fs->sb.s_free_inodes_count--;
                free(bitmap);
                return g * fs->inodes_per_group + j + 1;
            }
        }
    }

    free(bitmap);
    return 0;
}

static void ext2_free_inode(ext2_fs_t *fs, uint32_t ino)
{
    uint32_t g = (ino - 1) / fs->inodes_per_group;
    uint32_t idx = (ino - 1) % fs->inodes_per_group;

    uint8_t *bitmap = (uint8_t *)malloc(fs->block_size);
    if (!bitmap)
        return;
    if (!ext2_read_block(fs, fs->group_descs[g].bg_inode_bitmap, bitmap)) {
        free(bitmap);
        return;
    }

    bitmap[idx / 8] &= ~(1 << (idx % 8));
    ext2_write_block(fs, fs->group_descs[g].bg_inode_bitmap, bitmap);
    fs->group_descs[g].bg_free_inodes_count++;
    fs->sb.s_free_inodes_count++;
    free(bitmap);
}

static bool ext2_set_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical,
                            uint32_t phys, uint32_t inode_group)
{
    uint32_t n = fs->addrs_per_block;

    if (logical < EXT2_DIRECT_BLOCKS) {
        inode->i_block[logical] = phys;
        return true;
    }
    logical -= EXT2_DIRECT_BLOCKS;

    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf)
        return false;

    if (logical < n) {
        if (!inode->i_block[EXT2_IND_BLOCK]) {
            inode->i_block[EXT2_IND_BLOCK] = ext2_alloc_block(fs, inode_group);
            if (!inode->i_block[EXT2_IND_BLOCK]) {
                free(buf);
                return false;
            }
            memset(buf, 0, fs->block_size);
        } else {
            if (!ext2_read_block(fs, inode->i_block[EXT2_IND_BLOCK], buf)) {
                free(buf);
                return false;
            }
        }
        ((uint32_t *)buf)[logical] = phys;
        bool ok = ext2_write_block(fs, inode->i_block[EXT2_IND_BLOCK], buf);
        free(buf);
        return ok;
    }
    logical -= n;

    if (logical < n * n) {
        if (!inode->i_block[EXT2_DIND_BLOCK]) {
            inode->i_block[EXT2_DIND_BLOCK] = ext2_alloc_block(fs, inode_group);
            if (!inode->i_block[EXT2_DIND_BLOCK]) {
                free(buf);
                return false;
            }
            memset(buf, 0, fs->block_size);
            ext2_write_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
        }
        if (!ext2_read_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf)) {
            free(buf);
            return false;
        }
        uint32_t ind_idx = logical / n;
        uint32_t ind_block = ((uint32_t *)buf)[ind_idx];
        if (!ind_block) {
            ind_block = ext2_alloc_block(fs, inode_group);
            if (!ind_block) {
                free(buf);
                return false;
            }
            ((uint32_t *)buf)[ind_idx] = ind_block;
            ext2_write_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf);
            memset(buf, 0, fs->block_size);
        } else {
            if (!ext2_read_block(fs, ind_block, buf)) {
                free(buf);
                return false;
            }
        }
        ((uint32_t *)buf)[logical % n] = phys;
        bool ok = ext2_write_block(fs, ind_block, buf);
        free(buf);
        return ok;
    }

    free(buf);
    return false;
}

static void ext2_free_file_blocks(ext2_fs_t *fs, ext2_inode_t *inode)
{
    uint32_t n = fs->addrs_per_block;
    uint32_t num_blocks = (inode->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t i = 0; i < num_blocks && i < EXT2_DIRECT_BLOCKS; i++) {
        if (inode->i_block[i]) {
            ext2_free_block(fs, inode->i_block[i]);
            inode->i_block[i] = 0;
        }
    }

    if (inode->i_block[EXT2_IND_BLOCK]) {
        uint8_t *buf = (uint8_t *)malloc(fs->block_size);
        if (buf && ext2_read_block(fs, inode->i_block[EXT2_IND_BLOCK], buf)) {
            uint32_t *ptrs = (uint32_t *)buf;
            for (uint32_t i = 0; i < n; i++) {
                if (ptrs[i])
                    ext2_free_block(fs, ptrs[i]);
            }
        }
        ext2_free_block(fs, inode->i_block[EXT2_IND_BLOCK]);
        inode->i_block[EXT2_IND_BLOCK] = 0;
        if (buf) free(buf);
    }

    if (inode->i_block[EXT2_DIND_BLOCK]) {
        uint8_t *buf = (uint8_t *)malloc(fs->block_size);
        uint8_t *buf2 = (uint8_t *)malloc(fs->block_size);
        if (buf && buf2 && ext2_read_block(fs, inode->i_block[EXT2_DIND_BLOCK], buf)) {
            uint32_t *dptrs = (uint32_t *)buf;
            for (uint32_t i = 0; i < n; i++) {
                if (!dptrs[i])
                    continue;
                if (ext2_read_block(fs, dptrs[i], buf2)) {
                    uint32_t *ptrs = (uint32_t *)buf2;
                    for (uint32_t j = 0; j < n; j++) {
                        if (ptrs[j])
                            ext2_free_block(fs, ptrs[j]);
                    }
                }
                ext2_free_block(fs, dptrs[i]);
            }
        }
        ext2_free_block(fs, inode->i_block[EXT2_DIND_BLOCK]);
        inode->i_block[EXT2_DIND_BLOCK] = 0;
        if (buf) free(buf);
        if (buf2) free(buf2);
    }

    if (inode->i_block[EXT2_TIND_BLOCK]) {
        ext2_free_block(fs, inode->i_block[EXT2_TIND_BLOCK]);
        inode->i_block[EXT2_TIND_BLOCK] = 0;
    }
}

static bool ext2_add_dir_entry(ext2_fs_t *fs, uint32_t dir_ino, uint32_t child_ino,
                                const char *name, uint8_t file_type)
{
    ext2_inode_t dir;
    if (!ext2_read_inode(fs, dir_ino, &dir))
        return false;

    uint8_t name_len = (uint8_t)strlen(name);
    uint16_t needed = ((8 + name_len + 3) / 4) * 4;

    uint32_t num_blocks = (dir.i_size + fs->block_size - 1) / fs->block_size;
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf)
        return false;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_bmap(fs, &dir, b);
        if (!phys || !ext2_read_block(fs, phys, buf))
            continue;

        uint32_t offset = 0;
        while (offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + offset);
            if (de->rec_len == 0)
                break;

            uint16_t actual = ((8 + de->name_len + 3) / 4) * 4;
            if (de->inode == 0)
                actual = 0;
            uint16_t slack = de->rec_len - actual;

            if (slack >= needed) {
                if (de->inode != 0) {
                    uint16_t old_rec_len = de->rec_len;
                    de->rec_len = actual;
                    ext2_dir_entry_t *new_de = (ext2_dir_entry_t *)(buf + offset + actual);
                    new_de->inode = child_ino;
                    new_de->rec_len = old_rec_len - actual;
                    new_de->name_len = name_len;
                    new_de->file_type = file_type;
                    memcpy(new_de->name, name, name_len);
                } else {
                    de->inode = child_ino;
                    de->name_len = name_len;
                    de->file_type = file_type;
                    memcpy(de->name, name, name_len);
                }
                ext2_write_block(fs, phys, buf);
                free(buf);
                return true;
            }
            offset += de->rec_len;
        }
    }

    uint32_t group = (dir_ino - 1) / fs->inodes_per_group;
    uint32_t new_block = ext2_alloc_block(fs, group);
    if (!new_block) {
        free(buf);
        return false;
    }

    memset(buf, 0, fs->block_size);
    ext2_dir_entry_t *de = (ext2_dir_entry_t *)buf;
    de->inode = child_ino;
    de->rec_len = fs->block_size;
    de->name_len = name_len;
    de->file_type = file_type;
    memcpy(de->name, name, name_len);
    ext2_write_block(fs, new_block, buf);

    uint32_t new_logical = dir.i_size / fs->block_size;
    ext2_set_block(fs, &dir, new_logical, new_block, group);
    dir.i_size += fs->block_size;
    dir.i_blocks += fs->block_size / 512;
    ext2_write_inode(fs, dir_ino, &dir);

    free(buf);
    return true;
}

static bool ext2_remove_dir_entry(ext2_fs_t *fs, uint32_t dir_ino, const char *name)
{
    ext2_inode_t dir;
    if (!ext2_read_inode(fs, dir_ino, &dir))
        return false;

    size_t name_len = strlen(name);
    uint32_t num_blocks = (dir.i_size + fs->block_size - 1) / fs->block_size;
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf)
        return false;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_bmap(fs, &dir, b);
        if (!phys || !ext2_read_block(fs, phys, buf))
            continue;

        uint32_t offset = 0;
        ext2_dir_entry_t *prev = NULL;
        while (offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + offset);
            if (de->rec_len == 0)
                break;
            if (de->inode != 0 && de->name_len == name_len &&
                memcmp(de->name, name, name_len) == 0) {
                if (prev)
                    prev->rec_len += de->rec_len;
                else
                    de->inode = 0;
                ext2_write_block(fs, phys, buf);
                free(buf);
                return true;
            }
            prev = de;
            offset += de->rec_len;
        }
    }

    free(buf);
    return false;
}

static void ext2_flush_metadata(ext2_fs_t *fs)
{
    // Write superblock back at byte offset 1024
    uint8_t *sb_buf = (uint8_t *)malloc(fs->block_size > 2048 ? fs->block_size : 2048);
    if (!sb_buf)
        return;

    uint64_t sb_lba = fs->lba_start + 2;
    disk_read(fs->disk_id, sb_lba, 2, sb_buf);
    memcpy(sb_buf, &fs->sb, sizeof(ext2_superblock_t));
    disk_write(fs->disk_id, sb_lba, 2, sb_buf);
    free(sb_buf);

    // Write group descriptors
    uint32_t gd_block = fs->first_data_block + 1;
    uint32_t gd_size = fs->num_groups * sizeof(ext2_group_desc_t);
    uint32_t gd_blocks = (gd_size + fs->block_size - 1) / fs->block_size;
    uint8_t *gd_buf = (uint8_t *)malloc(gd_blocks * fs->block_size);
    if (!gd_buf)
        return;
    memset(gd_buf, 0, gd_blocks * fs->block_size);
    memcpy(gd_buf, fs->group_descs, gd_size);
    for (uint32_t i = 0; i < gd_blocks; i++)
        ext2_write_block(fs, gd_block + i, gd_buf + i * fs->block_size);
    free(gd_buf);
}

// VFS driver interface

static bool ext2_mount_internal(int disk_id, uint32_t lba_start,
                                 uint32_t num_sectors, vfs_mount_t *mount)
{
    (void)num_sectors;

    uint8_t sb_buf[1024];
    // Superblock is at byte offset 1024 from partition start = LBA + 2
    if (!disk_read(disk_id, lba_start + 2, 2, sb_buf))
        return false;

    ext2_superblock_t *sb = (ext2_superblock_t *)sb_buf;
    if (sb->s_magic != EXT2_MAGIC)
        return false;

    ext2_fs_t *fs = (ext2_fs_t *)malloc(sizeof(ext2_fs_t));
    if (!fs)
        return false;
    memset(fs, 0, sizeof(ext2_fs_t));

    fs->disk_id = disk_id;
    fs->lba_start = lba_start;
    memcpy(&fs->sb, sb, sizeof(ext2_superblock_t));

    fs->block_size = 1024u << fs->sb.s_log_block_size;
    fs->sectors_per_block = fs->block_size / 512;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->first_data_block = fs->sb.s_first_data_block;
    fs->addrs_per_block = fs->block_size / 4;

    if (fs->sb.s_rev_level >= 1 && fs->sb.s_inode_size > 0)
        fs->inode_size = fs->sb.s_inode_size;
    else
        fs->inode_size = 128;

    fs->num_groups = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) /
                     fs->blocks_per_group;

    fs->block_buf = (uint8_t *)malloc(fs->block_size);
    if (!fs->block_buf) {
        free(fs);
        return false;
    }

    uint32_t gd_size = fs->num_groups * sizeof(ext2_group_desc_t);
    uint32_t gd_blocks = (gd_size + fs->block_size - 1) / fs->block_size;
    fs->group_descs = (ext2_group_desc_t *)malloc(gd_blocks * fs->block_size);
    if (!fs->group_descs) {
        free(fs->block_buf);
        free(fs);
        return false;
    }

    uint32_t gd_block = fs->first_data_block + 1;
    for (uint32_t i = 0; i < gd_blocks; i++) {
        if (!ext2_read_block(fs, gd_block + i,
                             (uint8_t *)fs->group_descs + i * fs->block_size)) {
            free(fs->group_descs);
            free(fs->block_buf);
            free(fs);
            return false;
        }
    }

    mount->fs_data = fs;
    mount->root_cluster = EXT2_ROOT_INODE;

    log_info("ext2: Mounted on disk %d, %u block groups, %u block size",
             disk_id, fs->num_groups, fs->block_size);
    return true;
}

static bool ext2_unmount_internal(vfs_mount_t *mount)
{
    if (!mount->fs_data)
        return false;

    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;
    ext2_flush_metadata(fs);
    free(fs->group_descs);
    free(fs->block_buf);
    free(fs);
    mount->fs_data = NULL;
    return true;
}

static char **ext2_list_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                            int *count)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;
    *count = 0;

    ext2_inode_t dir;
    if (!ext2_read_inode(fs, cluster, &dir))
        return NULL;

    int capacity = 16;
    char **entries = (char **)malloc(capacity * sizeof(char *));
    if (!entries)
        return NULL;

    uint32_t num_blocks = (dir.i_size + fs->block_size - 1) / fs->block_size;
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf) {
        free(entries);
        return NULL;
    }

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_bmap(fs, &dir, b);
        if (!phys || !ext2_read_block(fs, phys, buf))
            continue;

        uint32_t offset = 0;
        while (offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(buf + offset);
            if (de->rec_len == 0)
                break;

            if (de->inode != 0) {
                if (*count >= capacity) {
                    capacity *= 2;
                    char **new_entries = (char **)realloc(entries, capacity * sizeof(char *));
                    if (!new_entries) {
                        for (int i = 0; i < *count; i++)
                            free(entries[i]);
                        free(entries);
                        free(buf);
                        return NULL;
                    }
                    entries = new_entries;
                }
                char *name = (char *)malloc(de->name_len + 1);
                if (name) {
                    memcpy(name, de->name, de->name_len);
                    name[de->name_len] = '\0';
                    entries[*count] = name;
                    (*count)++;
                }
            }
            offset += de->rec_len;
        }
    }

    free(buf);
    return entries;
}

static uint8_t *ext2_read_file_common(vfs_mount_t *mount, uint32_t cluster,
                                       const char *filename, uint32_t *size,
                                       vfs_progress_fn progress, void *ctx)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;

    uint32_t file_ino;
    if (!ext2_lookup(fs, cluster, filename, &file_ino))
        return NULL;

    ext2_inode_t inode;
    if (!ext2_read_inode(fs, file_ino, &inode))
        return NULL;

    if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
        return NULL;

    *size = inode.i_size;
    uint8_t *data = (uint8_t *)malloc(*size + 1);
    if (!data)
        return NULL;

    uint32_t num_blocks = (*size + fs->block_size - 1) / fs->block_size;
    uint32_t bytes_read = 0;
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf) {
        free(data);
        return NULL;
    }

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_bmap(fs, &inode, b);
        if (!phys || !ext2_read_block(fs, phys, buf)) {
            free(buf);
            free(data);
            return NULL;
        }

        uint32_t to_copy = fs->block_size;
        if (bytes_read + to_copy > *size)
            to_copy = *size - bytes_read;
        memcpy(data + bytes_read, buf, to_copy);
        bytes_read += to_copy;

        if (progress)
            progress(bytes_read, *size, ctx);
    }

    data[*size] = '\0';
    free(buf);
    return data;
}

static uint8_t *ext2_read_file_internal(vfs_mount_t *mount, uint32_t cluster,
                                         const char *filename, uint32_t *size)
{
    return ext2_read_file_common(mount, cluster, filename, size, NULL, NULL);
}

static uint8_t *ext2_read_file_ex(vfs_mount_t *mount, uint32_t cluster,
                                   const char *filename, uint32_t *size,
                                   vfs_progress_fn progress, void *ctx)
{
    return ext2_read_file_common(mount, cluster, filename, size, progress, ctx);
}

static bool ext2_resolve_path_internal(vfs_mount_t *mount, const char *path,
                                        uint32_t *parent_cluster, char **filename)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;
    char *path_copy = strdup(path);
    if (!path_copy)
        return false;

    // Remove leading slash
    char *p = path_copy;
    while (*p == '/')
        p++;

    char *last_slash = strrchr(p, '/');
    uint32_t dir_ino = EXT2_ROOT_INODE;

    if (last_slash) {
        *last_slash = '\0';
        char *dir_path = p;
        char *tok = strtok(dir_path, "/");
        while (tok) {
            uint32_t next_ino;
            if (!ext2_lookup(fs, dir_ino, tok, &next_ino)) {
                free(path_copy);
                return false;
            }
            dir_ino = next_ino;
            tok = strtok(NULL, "/");
        }
        *filename = strdup(last_slash + 1);
    } else {
        *filename = strdup(p);
    }

    *parent_cluster = dir_ino;
    free(path_copy);
    return *filename != NULL;
}

static bool ext2_write_file_internal(vfs_mount_t *mount, uint32_t cluster,
                                      const char *filename, const uint8_t *data,
                                      uint32_t size)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;
    uint32_t group = (cluster - 1) / fs->inodes_per_group;

    uint32_t file_ino;
    bool exists = ext2_lookup(fs, cluster, filename, &file_ino);

    ext2_inode_t inode;

    if (exists) {
        if (!ext2_read_inode(fs, file_ino, &inode))
            return false;
        ext2_free_file_blocks(fs, &inode);
    } else {
        file_ino = ext2_alloc_inode(fs, group);
        if (!file_ino)
            return false;
        memset(&inode, 0, sizeof(ext2_inode_t));
        inode.i_mode = EXT2_S_IFREG | 0644;
        inode.i_links_count = 1;
        if (!ext2_add_dir_entry(fs, cluster, file_ino, filename, EXT2_FT_REG_FILE)) {
            ext2_free_inode(fs, file_ino);
            return false;
        }
    }

    memset(inode.i_block, 0, sizeof(inode.i_block));
    inode.i_size = size;
    inode.i_blocks = 0;

    uint32_t num_blocks = (size + fs->block_size - 1) / fs->block_size;
    if (num_blocks == 0 && size == 0)
        num_blocks = 0;

    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf && num_blocks > 0)
        return false;

    uint32_t bytes_written = 0;
    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t new_block = ext2_alloc_block(fs, group);
        if (!new_block) {
            if (buf) free(buf);
            return false;
        }
        ext2_set_block(fs, &inode, b, new_block, group);
        inode.i_blocks += fs->block_size / 512;

        memset(buf, 0, fs->block_size);
        uint32_t to_copy = fs->block_size;
        if (bytes_written + to_copy > size)
            to_copy = size - bytes_written;
        memcpy(buf, data + bytes_written, to_copy);
        ext2_write_block(fs, new_block, buf);
        bytes_written += to_copy;
    }

    if (buf) free(buf);
    ext2_write_inode(fs, file_ino, &inode);
    ext2_flush_metadata(fs);
    return true;
}

static bool ext2_delete_file_internal(vfs_mount_t *mount, uint32_t cluster,
                                       const char *filename)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;

    uint32_t file_ino;
    if (!ext2_lookup(fs, cluster, filename, &file_ino))
        return false;

    ext2_inode_t inode;
    if (!ext2_read_inode(fs, file_ino, &inode))
        return false;

    if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
        return false;

    ext2_remove_dir_entry(fs, cluster, filename);
    inode.i_links_count--;

    if (inode.i_links_count == 0) {
        ext2_free_file_blocks(fs, &inode);
        inode.i_size = 0;
        inode.i_blocks = 0;
        ext2_write_inode(fs, file_ino, &inode);
        ext2_free_inode(fs, file_ino);
    } else {
        ext2_write_inode(fs, file_ino, &inode);
    }

    ext2_flush_metadata(fs);
    return true;
}

static bool ext2_create_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                            const char *dirname)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;
    uint32_t group = (cluster - 1) / fs->inodes_per_group;

    uint32_t new_ino = ext2_alloc_inode(fs, group);
    if (!new_ino)
        return false;

    uint32_t new_block = ext2_alloc_block(fs, group);
    if (!new_block) {
        ext2_free_inode(fs, new_ino);
        return false;
    }

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(ext2_inode_t));
    inode.i_mode = EXT2_S_IFDIR | 0755;
    inode.i_links_count = 2; // . and parent's entry
    inode.i_size = fs->block_size;
    inode.i_blocks = fs->block_size / 512;
    inode.i_block[0] = new_block;

    // Write . and .. entries
    uint8_t *buf = (uint8_t *)malloc(fs->block_size);
    if (!buf) {
        ext2_free_block(fs, new_block);
        ext2_free_inode(fs, new_ino);
        return false;
    }
    memset(buf, 0, fs->block_size);

    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)buf;
    dot->inode = new_ino;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(buf + 12);
    dotdot->inode = cluster;
    dotdot->rec_len = fs->block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    ext2_write_block(fs, new_block, buf);
    free(buf);

    ext2_write_inode(fs, new_ino, &inode);

    if (!ext2_add_dir_entry(fs, cluster, new_ino, dirname, EXT2_FT_DIR)) {
        ext2_free_block(fs, new_block);
        ext2_free_inode(fs, new_ino);
        return false;
    }

    // Increment parent link count for ".."
    ext2_inode_t parent;
    if (ext2_read_inode(fs, cluster, &parent)) {
        parent.i_links_count++;
        ext2_write_inode(fs, cluster, &parent);
    }

    uint32_t g = (new_ino - 1) / fs->inodes_per_group;
    fs->group_descs[g].bg_used_dirs_count++;

    ext2_flush_metadata(fs);
    return true;
}

static bool ext2_delete_directory_internal(vfs_mount_t *mount, uint32_t cluster,
                                            const char *dirname)
{
    ext2_fs_t *fs = (ext2_fs_t *)mount->fs_data;

    uint32_t dir_ino;
    if (!ext2_lookup(fs, cluster, dirname, &dir_ino))
        return false;

    ext2_inode_t inode;
    if (!ext2_read_inode(fs, dir_ino, &inode))
        return false;

    if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return false;

    // Check empty (only . and ..)
    int count = 0;
    char **contents = ext2_list_directory_internal(mount, dir_ino, &count);
    if (contents) {
        bool empty = true;
        for (int i = 0; i < count; i++) {
            if (strcmp(contents[i], ".") != 0 && strcmp(contents[i], "..") != 0)
                empty = false;
            free(contents[i]);
        }
        free(contents);
        if (!empty)
            return false;
    }

    ext2_remove_dir_entry(fs, cluster, dirname);
    ext2_free_file_blocks(fs, &inode);
    inode.i_size = 0;
    inode.i_blocks = 0;
    inode.i_links_count = 0;
    ext2_write_inode(fs, dir_ino, &inode);
    ext2_free_inode(fs, dir_ino);

    // Decrement parent link count
    ext2_inode_t parent;
    if (ext2_read_inode(fs, cluster, &parent)) {
        if (parent.i_links_count > 0)
            parent.i_links_count--;
        ext2_write_inode(fs, cluster, &parent);
    }

    uint32_t g = (dir_ino - 1) / fs->inodes_per_group;
    if (fs->group_descs[g].bg_used_dirs_count > 0)
        fs->group_descs[g].bg_used_dirs_count--;

    ext2_flush_metadata(fs);
    return true;
}

static fs_driver_t ext2_driver = {
    .name = "ext2",
    .mount = ext2_mount_internal,
    .unmount = ext2_unmount_internal,
    .list_directory = ext2_list_directory_internal,
    .read_file = ext2_read_file_internal,
    .write_file = ext2_write_file_internal,
    .delete_file = ext2_delete_file_internal,
    .create_directory = ext2_create_directory_internal,
    .delete_directory = ext2_delete_directory_internal,
    .resolve_path = ext2_resolve_path_internal,
    .read_file_ex = ext2_read_file_ex,
};

void ext2_init()
{
    fs_register_driver(&ext2_driver);
}
