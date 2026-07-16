#include "c26.h"
#include "c26_block.h"
#include "c26_fs.h"

/* C26FS v2 on-disk layout (512-byte sectors):
 *   sector 0      superblock (holds directory + bitmap checksums)
 *   sectors 1-4   directory: 64 entries x 32 bytes
 *   sectors 5-8   allocation bitmap: one bit per disk sector
 *   sectors 9+    file data, contiguous first-fit extents
 * scripts/fsinstall.py implements the same layout host-side; keep them in
 * sync.
 */

#define C26_FS_MAGIC 0x46363243U /* 'C26F' */
#define C26_FS_VERSION 2U
#define C26_FS_DIR_START 1U
#define C26_FS_DIR_SECTORS 4U
#define C26_FS_MAP_START 5U
#define C26_FS_MAP_SECTORS 4U
#define C26_FS_DATA_START 9U
#define C26_FS_MAP_BYTES (C26_FS_MAP_SECTORS * C26_BLOCK_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t map_start;
    uint32_t map_sectors;
    uint32_t data_start;
    uint32_t generation;
    uint32_t dir_checksum;
    uint32_t map_checksum;
    uint32_t checksum;
    uint8_t reserved[460];
} fs_superblock_t;

typedef struct {
    char name[16]; /* NUL-terminated; name[0] == 0 means free */
    uint32_t size;
    uint32_t start_sector;
    uint32_t sector_count;
    uint32_t checksum;
} fs_entry_t;

_Static_assert(sizeof(fs_superblock_t) == C26_BLOCK_SECTOR_SIZE,
               "superblock must fill one sector");
_Static_assert(sizeof(fs_entry_t) == 32, "directory entry must be 32 bytes");
_Static_assert(sizeof(fs_entry_t) * C26_FS_FILE_COUNT ==
                   C26_FS_DIR_SECTORS * C26_BLOCK_SECTOR_SIZE,
               "directory must fill its sectors exactly");

static fs_superblock_t superblock __attribute__((aligned(512)));
static fs_entry_t directory[C26_FS_FILE_COUNT] __attribute__((aligned(512)));
static uint8_t bitmap[C26_FS_MAP_BYTES] __attribute__((aligned(512)));
static uint8_t sector_buffer[C26_BLOCK_SECTOR_SIZE] __attribute__((aligned(512)));
static int mounted;

static uint32_t checksum_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t checksum = 2166136261U;
    for (size_t i = 0; i < size; i++) {
        checksum ^= bytes[i];
        checksum *= 16777619U;
    }
    return checksum;
}

static uint32_t super_checksum(void)
{
    uint32_t saved = superblock.checksum;
    superblock.checksum = 0;
    uint32_t result = checksum_bytes(&superblock, sizeof(superblock));
    superblock.checksum = saved;
    return result;
}

static int bit_used(uint32_t sector)
{
    return (bitmap[sector / 8] >> (sector % 8)) & 1;
}

static void mark_run(uint32_t start, uint32_t count, int used)
{
    for (uint32_t sector = start; sector < start + count; sector++) {
        if (used) {
            bitmap[sector / 8] |= (uint8_t)(1U << (sector % 8));
        } else {
            bitmap[sector / 8] &= (uint8_t)~(1U << (sector % 8));
        }
    }
}

static uint32_t alloc_run(uint32_t count)
{
    uint32_t run_start = 0;
    uint32_t run_length = 0;
    for (uint32_t sector = superblock.data_start;
         sector < superblock.total_sectors; sector++) {
        if (bit_used(sector)) {
            run_length = 0;
            continue;
        }
        if (run_length == 0) {
            run_start = sector;
        }
        if (++run_length == count) {
            mark_run(run_start, count, 1);
            return run_start;
        }
    }
    return 0;
}

static int names_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static size_t valid_name_length(const char *name)
{
    size_t length = 0;
    while (name != 0 && name[length] != '\0') {
        char ch = name[length];
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9') &&
            ch != '_' && ch != '-') {
            return 0;
        }
        if (++length > C26_FS_NAME_MAX) {
            return 0;
        }
    }
    return length;
}

static fs_entry_t *find_entry(const char *name)
{
    for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
        if (directory[i].name[0] != '\0' &&
            names_equal(directory[i].name, name)) {
            return &directory[i];
        }
    }
    return 0;
}

static int read_sectors(uint32_t start, uint32_t count, void *data)
{
    uint8_t *bytes = data;
    for (uint32_t i = 0; i < count; i++) {
        if (!c26_block_read(start + i,
                            bytes + (size_t)i * C26_BLOCK_SECTOR_SIZE)) {
            return 0;
        }
    }
    return 1;
}

static int write_sectors(uint32_t start, uint32_t count, const void *data)
{
    const uint8_t *bytes = data;
    for (uint32_t i = 0; i < count; i++) {
        if (!c26_block_write(start + i,
                             bytes + (size_t)i * C26_BLOCK_SECTOR_SIZE)) {
            return 0;
        }
    }
    return 1;
}

static int write_metadata(void)
{
    superblock.generation++;
    superblock.dir_checksum = checksum_bytes(directory, sizeof(directory));
    superblock.map_checksum = checksum_bytes(bitmap, sizeof(bitmap));
    superblock.checksum = super_checksum();
    return write_sectors(C26_FS_DIR_START, C26_FS_DIR_SECTORS, directory) &&
           write_sectors(C26_FS_MAP_START, C26_FS_MAP_SECTORS, bitmap) &&
           c26_block_write(0, &superblock) && c26_block_flush();
}

static int format_filesystem(void)
{
    memset(&superblock, 0, sizeof(superblock));
    memset(directory, 0, sizeof(directory));
    memset(bitmap, 0, sizeof(bitmap));
    superblock.magic = C26_FS_MAGIC;
    superblock.version = C26_FS_VERSION;
    superblock.sector_size = C26_BLOCK_SECTOR_SIZE;
    uint64_t capacity = c26_block_sector_count();
    if (capacity > C26_FS_MAP_BYTES * 8ULL) {
        capacity = C26_FS_MAP_BYTES * 8ULL;
    }
    superblock.total_sectors = (uint32_t)capacity;
    superblock.dir_start = C26_FS_DIR_START;
    superblock.dir_sectors = C26_FS_DIR_SECTORS;
    superblock.map_start = C26_FS_MAP_START;
    superblock.map_sectors = C26_FS_MAP_SECTORS;
    superblock.data_start = C26_FS_DATA_START;
    mark_run(0, C26_FS_DATA_START, 1);
    if (!write_metadata()) {
        return 0;
    }
    c26_puts("C26FS: formatted new disk\n");
    return 1;
}

static int metadata_valid(void)
{
    if (superblock.magic != C26_FS_MAGIC ||
        superblock.version != C26_FS_VERSION ||
        superblock.sector_size != C26_BLOCK_SECTOR_SIZE ||
        superblock.dir_start != C26_FS_DIR_START ||
        superblock.dir_sectors != C26_FS_DIR_SECTORS ||
        superblock.map_start != C26_FS_MAP_START ||
        superblock.map_sectors != C26_FS_MAP_SECTORS ||
        superblock.data_start != C26_FS_DATA_START ||
        superblock.total_sectors > c26_block_sector_count() ||
        superblock.total_sectors <= C26_FS_DATA_START ||
        superblock.checksum != super_checksum()) {
        return 0;
    }
    return superblock.dir_checksum ==
               checksum_bytes(directory, sizeof(directory)) &&
           superblock.map_checksum == checksum_bytes(bitmap, sizeof(bitmap));
}

static size_t file_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
        if (directory[i].name[0] != '\0') {
            count++;
        }
    }
    return count;
}

int c26_fs_init(void)
{
    mounted = 0;
    if (!c26_block_online()) {
        c26_puts("C26FS: no block device\n");
        return 0;
    }
    if (!c26_block_read(0, &superblock) ||
        !read_sectors(C26_FS_DIR_START, C26_FS_DIR_SECTORS, directory) ||
        !read_sectors(C26_FS_MAP_START, C26_FS_MAP_SECTORS, bitmap) ||
        (!metadata_valid() && !format_filesystem())) {
        c26_puts("C26FS: mount failed\n");
        return 0;
    }
    mounted = metadata_valid();
    if (mounted) {
        c26_puts("C26FS: mounted ");
        c26_put_uint(file_count());
        c26_puts(" file(s)\n");
    }
    return mounted;
}

int c26_fs_online(void)
{
    return mounted;
}

size_t c26_fs_count(void)
{
    return mounted ? file_count() : 0;
}

int c26_fs_entry(size_t index, const char **name, uint32_t *size)
{
    if (!mounted) return 0;
    size_t seen = 0;
    for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
        if (directory[i].name[0] == '\0') continue;
        if (seen++ != index) continue;
        if (name != 0) *name = directory[i].name;
        if (size != 0) *size = directory[i].size;
        return 1;
    }
    return 0;
}

int c26_fs_save(const char *name, const void *data, size_t size)
{
    size_t name_length = valid_name_length(name);
    if (!mounted || name_length == 0 || data == 0 || size == 0 ||
        size > C26_FS_FILE_MAX) {
        return 0;
    }
    uint32_t sectors = (uint32_t)((size + C26_BLOCK_SECTOR_SIZE - 1) /
                                  C26_BLOCK_SECTOR_SIZE);
    fs_entry_t *entry = find_entry(name);
    if (entry == 0) {
        for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
            if (directory[i].name[0] == '\0') {
                entry = &directory[i];
                break;
            }
        }
        if (entry == 0) return 0;
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, name, name_length);
    }

    uint32_t old_start = entry->start_sector;
    uint32_t old_count = entry->sector_count;
    uint32_t start = old_start;
    if (sectors > old_count) {
        start = alloc_run(sectors);
        if (start == 0) {
            if (entry->size == 0) entry->name[0] = '\0';
            return 0;
        }
    }

    const uint8_t *bytes = data;
    for (uint32_t sector = 0; sector < sectors; sector++) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        size_t offset = (size_t)sector * C26_BLOCK_SECTOR_SIZE;
        size_t remaining = size - offset;
        size_t chunk = remaining > C26_BLOCK_SECTOR_SIZE ?
                       C26_BLOCK_SECTOR_SIZE : remaining;
        memcpy(sector_buffer, bytes + offset, chunk);
        if (!c26_block_write(start + sector, sector_buffer)) {
            if (start != old_start) mark_run(start, sectors, 0);
            if (entry->size == 0) entry->name[0] = '\0';
            return 0;
        }
    }

    if (start != old_start) {
        if (old_count != 0) mark_run(old_start, old_count, 0);
        entry->start_sector = start;
        entry->sector_count = sectors;
    }
    entry->size = (uint32_t)size;
    entry->checksum = checksum_bytes(data, size);
    return write_metadata();
}

int c26_fs_load(const char *name, void *data, size_t capacity, size_t *size)
{
    if (!mounted || data == 0) return 0;
    fs_entry_t *entry = find_entry(name);
    if (entry == 0 || entry->size > capacity ||
        entry->size > (size_t)entry->sector_count * C26_BLOCK_SECTOR_SIZE) {
        return 0;
    }
    uint8_t *bytes = data;
    uint32_t sectors = (entry->size + C26_BLOCK_SECTOR_SIZE - 1) /
                       C26_BLOCK_SECTOR_SIZE;
    for (uint32_t sector = 0; sector < sectors; sector++) {
        if (!c26_block_read(entry->start_sector + sector, sector_buffer)) {
            return 0;
        }
        size_t offset = (size_t)sector * C26_BLOCK_SECTOR_SIZE;
        size_t remaining = entry->size - offset;
        size_t chunk = remaining > C26_BLOCK_SECTOR_SIZE ?
                       C26_BLOCK_SECTOR_SIZE : remaining;
        memcpy(bytes + offset, sector_buffer, chunk);
    }
    if (checksum_bytes(data, entry->size) != entry->checksum) return 0;
    if (size != 0) *size = entry->size;
    return 1;
}

int c26_fs_delete(const char *name)
{
    if (!mounted) return 0;
    fs_entry_t *entry = find_entry(name);
    if (entry == 0) return 0;
    if (entry->sector_count != 0) {
        mark_run(entry->start_sector, entry->sector_count, 0);
    }
    memset(entry, 0, sizeof(*entry));
    return write_metadata();
}

int c26_fs_rename(const char *old_name, const char *new_name)
{
    size_t new_length = valid_name_length(new_name);
    if (!mounted || new_length == 0) return 0;
    fs_entry_t *entry = find_entry(old_name);
    if (entry == 0 || find_entry(new_name) != 0) return 0;
    memset(entry->name, 0, sizeof(entry->name));
    memcpy(entry->name, new_name, new_length);
    return write_metadata();
}
