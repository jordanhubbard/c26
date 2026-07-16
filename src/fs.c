#include "c26.h"
#include "c26_block.h"
#include "c26_fs.h"

#define C26_FS_MAGIC 0x53363243U
#define C26_FS_DIR_MAGIC 0x44363243U
#define C26_FS_VERSION 1U
#define C26_FS_DIRECTORY_SECTOR 1U
#define C26_FS_DATA_SECTOR 2U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t directory_sector;
    uint32_t generation;
    uint32_t checksum;
    uint8_t reserved[484];
} fs_superblock_t;

typedef struct {
    uint8_t used;
    uint8_t name_length;
    uint16_t flags;
    char name[16];
    uint32_t start_sector;
    uint32_t size;
    uint32_t capacity;
    uint32_t checksum;
    uint32_t reserved;
} fs_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t next_free_sector;
    uint32_t checksum;
    uint8_t reserved[12];
    fs_entry_t entries[C26_FS_FILE_COUNT];
} fs_directory_t;

_Static_assert(sizeof(fs_superblock_t) == C26_BLOCK_SECTOR_SIZE,
               "superblock must fill one sector");
_Static_assert(sizeof(fs_directory_t) == C26_BLOCK_SECTOR_SIZE,
               "directory must fill one sector");

static fs_superblock_t superblock __attribute__((aligned(512)));
static fs_directory_t directory __attribute__((aligned(512)));
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

static uint32_t directory_checksum(void)
{
    uint32_t saved = directory.checksum;
    directory.checksum = 0;
    uint32_t result = checksum_bytes(&directory, sizeof(directory));
    directory.checksum = saved;
    return result;
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
        if (directory.entries[i].used &&
            names_equal(directory.entries[i].name, name)) {
            return &directory.entries[i];
        }
    }
    return 0;
}

static int write_metadata(void)
{
    directory.checksum = directory_checksum();
    superblock.generation++;
    superblock.checksum = super_checksum();
    return c26_block_write(C26_FS_DIRECTORY_SECTOR, &directory) &&
           c26_block_write(0, &superblock) && c26_block_flush();
}

static int format_filesystem(void)
{
    memset(&superblock, 0, sizeof(superblock));
    memset(&directory, 0, sizeof(directory));
    superblock.magic = C26_FS_MAGIC;
    superblock.version = C26_FS_VERSION;
    superblock.sector_size = C26_BLOCK_SECTOR_SIZE;
    uint64_t capacity = c26_block_sector_count();
    superblock.total_sectors = capacity > UINT32_MAX ? UINT32_MAX :
                                                       (uint32_t)capacity;
    superblock.directory_sector = C26_FS_DIRECTORY_SECTOR;
    directory.magic = C26_FS_DIR_MAGIC;
    directory.version = C26_FS_VERSION;
    directory.next_free_sector = C26_FS_DATA_SECTOR;
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
        superblock.directory_sector != C26_FS_DIRECTORY_SECTOR ||
        superblock.total_sectors > c26_block_sector_count() ||
        superblock.total_sectors <= C26_FS_DATA_SECTOR ||
        superblock.checksum != super_checksum()) {
        return 0;
    }
    if (directory.magic != C26_FS_DIR_MAGIC ||
        directory.version != C26_FS_VERSION ||
        directory.count > C26_FS_FILE_COUNT ||
        directory.next_free_sector < C26_FS_DATA_SECTOR ||
        directory.next_free_sector > superblock.total_sectors ||
        directory.checksum != directory_checksum()) {
        return 0;
    }
    return 1;
}

int c26_fs_init(void)
{
    mounted = 0;
    if (!c26_block_online()) {
        c26_puts("C26FS: no block device\n");
        return 0;
    }
    if (!c26_block_read(0, &superblock) ||
        !c26_block_read(C26_FS_DIRECTORY_SECTOR, &directory) ||
        (!metadata_valid() && !format_filesystem())) {
        c26_puts("C26FS: mount failed\n");
        return 0;
    }
    mounted = metadata_valid();
    if (mounted) {
        c26_puts("C26FS: mounted ");
        c26_put_uint(directory.count);
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
    return mounted ? directory.count : 0;
}

int c26_fs_entry(size_t index, const char **name, uint32_t *size)
{
    if (!mounted) return 0;
    size_t seen = 0;
    for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
        if (!directory.entries[i].used) continue;
        if (seen++ != index) continue;
        if (name != 0) *name = directory.entries[i].name;
        if (size != 0) *size = directory.entries[i].size;
        return 1;
    }
    return 0;
}

int c26_fs_save(const char *name, const void *data, size_t size)
{
    size_t name_length = valid_name_length(name);
    if (!mounted || name_length == 0 || data == 0 || size > C26_FS_FILE_MAX) {
        return 0;
    }
    fs_entry_t *entry = find_entry(name);
    int is_new = entry == 0;
    if (is_new) {
        for (size_t i = 0; i < C26_FS_FILE_COUNT; i++) {
            if (!directory.entries[i].used) {
                entry = &directory.entries[i];
                break;
            }
        }
    }
    if (entry == 0) return 0;

    uint32_t sectors = (uint32_t)((size + C26_BLOCK_SECTOR_SIZE - 1) /
                                  C26_BLOCK_SECTOR_SIZE);
    if (sectors == 0) sectors = 1;
    uint32_t capacity = sectors * C26_BLOCK_SECTOR_SIZE;
    if (is_new || entry->capacity < size) {
        if (directory.next_free_sector > superblock.total_sectors - sectors) {
            return 0;
        }
        entry->start_sector = directory.next_free_sector;
        entry->capacity = capacity;
        directory.next_free_sector += sectors;
    }

    const uint8_t *bytes = data;
    for (uint32_t sector = 0; sector < sectors; sector++) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        size_t offset = (size_t)sector * C26_BLOCK_SECTOR_SIZE;
        size_t remaining = size > offset ? size - offset : 0;
        size_t chunk = remaining > C26_BLOCK_SECTOR_SIZE ?
                       C26_BLOCK_SECTOR_SIZE : remaining;
        if (chunk != 0) memcpy(sector_buffer, bytes + offset, chunk);
        if (!c26_block_write(entry->start_sector + sector, sector_buffer)) {
            return 0;
        }
    }

    if (is_new) {
        memset(entry, 0, sizeof(*entry));
        entry->used = 1;
        entry->name_length = (uint8_t)name_length;
        memcpy(entry->name, name, name_length);
        entry->start_sector = directory.next_free_sector - sectors;
        entry->capacity = capacity;
        directory.count++;
    }
    entry->size = (uint32_t)size;
    entry->checksum = checksum_bytes(data, size);
    return write_metadata();
}

int c26_fs_load(const char *name, void *data, size_t capacity, size_t *size)
{
    if (!mounted || data == 0) return 0;
    fs_entry_t *entry = find_entry(name);
    if (entry == 0 || entry->size > capacity || entry->size > entry->capacity) {
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
