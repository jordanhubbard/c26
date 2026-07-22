/* Host-side tests for C26FS v2: src/fs.c compiled as-is against the
 * in-memory block shim, exercising format, persistence, allocation reuse,
 * delete/rename, and corruption detection. */

#include "host_shim.h"

#include <stdio.h>
#include <string.h>

#include "../src/fs.c"

static int failures;

static void expect(int condition, const char *label)
{
    if (!condition) {
        failures++;
        printf("FAIL %s\n", label);
    }
}

int main(void)
{
    char buffer[C26_FS_FILE_MAX];
    size_t size;

    shim_disk_reset();
    shim_output_reset();

    expect(c26_fs_init(), "format fresh disk");
    expect(c26_fs_online(), "online after format");
    expect(c26_fs_count() == 0, "empty after format");
    expect(strstr(shim_output, "formatted new disk") != NULL,
           "format announced");

    expect(c26_fs_save("ALPHA", "hello tomorrow", 14), "save alpha");
    expect(c26_fs_load("ALPHA", buffer, sizeof(buffer), &size), "load alpha");
    expect(size == 14 && memcmp(buffer, "hello tomorrow", 14) == 0,
           "alpha roundtrip");

    /* Overwrite: shrink in place, then grow past the old extent. */
    expect(c26_fs_save("ALPHA", "tiny", 4), "shrink alpha");
    expect(c26_fs_load("ALPHA", buffer, sizeof(buffer), &size) && size == 4,
           "shrunk alpha loads");
    static char big[3000];
    memset(big, 'x', sizeof(big));
    expect(c26_fs_save("ALPHA", big, sizeof(big)), "grow alpha");
    expect(c26_fs_load("ALPHA", buffer, sizeof(buffer), &size) &&
               size == sizeof(big) && buffer[2999] == 'x',
           "grown alpha loads");

    expect(c26_fs_save("BETA", "second", 6), "save beta");
    expect(c26_fs_count() == 2, "two files");

    /* Delete frees the extent for reuse. */
    expect(c26_fs_delete("ALPHA"), "delete alpha");
    expect(!c26_fs_load("ALPHA", buffer, sizeof(buffer), &size),
           "alpha gone");
    expect(c26_fs_count() == 1, "one file after delete");
    expect(c26_fs_save("GAMMA", big, sizeof(big)), "reuse freed space");

    /* Rename. */
    expect(c26_fs_rename("BETA", "DELTA"), "rename beta");
    expect(!c26_fs_load("BETA", buffer, sizeof(buffer), &size),
           "old name gone");
    expect(c26_fs_load("DELTA", buffer, sizeof(buffer), &size) && size == 6,
           "new name loads");
    expect(!c26_fs_rename("DELTA", "GAMMA"), "rename collision rejected");

    /* Remount: everything survives a second init over the same disk. */
    expect(c26_fs_init(), "remount");
    expect(c26_fs_count() == 2, "count survives remount");
    expect(c26_fs_load("DELTA", buffer, sizeof(buffer), &size) && size == 6,
           "delta survives remount");

    /* Validation. */
    expect(!c26_fs_save("bad name!", "x", 1), "invalid name rejected");
    expect(!c26_fs_save("TOOBIG", buffer, C26_FS_FILE_MAX + 1),
           "oversize rejected");
    expect(!c26_fs_save("EMPTY", buffer, 0), "empty rejected");

    /* Directory capacity: fill every slot, then one more must fail. */
    size_t before = c26_fs_count();
    char name[8];
    for (size_t i = before; i < C26_FS_FILE_COUNT; i++) {
        snprintf(name, sizeof(name), "F%zu", i);
        expect(c26_fs_save(name, "x", 1), "fill directory");
    }
    expect(!c26_fs_save("OVERFLOW", "x", 1), "directory full rejected");

    /* Corruption: flip one data byte of DELTA on disk; load must fail. */
    int corrupted = 0;
    for (size_t sector = 9; sector < 64 && !corrupted; sector++) {
        uint8_t *data = shim_disk + sector * 512;
        if (memcmp(data, "second", 6) == 0) {
            data[0] ^= 0xff;
            corrupted = 1;
        }
    }
    expect(corrupted, "found delta on disk");
    expect(!c26_fs_load("DELTA", buffer, sizeof(buffer), &size),
           "corruption detected");

    /* Crash safety: a transaction that commits its log then crashes before
       installing must be recovered (replayed) on the next mount. */
    shim_disk_reset();
    shim_output_reset();
    expect(c26_fs_init(), "recovery: fresh disk");
    expect(c26_fs_save("REC", "AAAA", 4), "recovery: save v1");
    fs_test_crash = 1; /* crash after commit, before install */
    expect(!c26_fs_save("REC", "BBBB", 4), "recovery: save v2 crashes");
    expect(c26_fs_init(), "recovery: remount replays");
    expect(strstr(shim_output, "recovered from log") != NULL,
           "recovery: replay announced");
    expect(c26_fs_load("REC", buffer, sizeof(buffer), &size) && size == 4 &&
               memcmp(buffer, "BBBB", 4) == 0,
           "recovery: committed write survived the crash");

    /* And a transaction that crashes BEFORE committing must leave the old file
       wholly intact — no half-write, no corruption. */
    shim_disk_reset();
    shim_output_reset();
    expect(c26_fs_init(), "atomicity: fresh disk");
    expect(c26_fs_save("REC", "AAAA", 4), "atomicity: save v1");
    fs_test_crash = 2; /* crash before commit */
    expect(!c26_fs_save("REC", "BBBB", 4), "atomicity: save v2 crashes");
    expect(c26_fs_init(), "atomicity: remount");
    expect(strstr(shim_output, "recovered from log") == NULL,
           "atomicity: nothing to replay");
    expect(c26_fs_load("REC", buffer, sizeof(buffer), &size) && size == 4 &&
               memcmp(buffer, "AAAA", 4) == 0,
           "atomicity: old file intact after uncommitted crash");

    if (failures == 0) {
        printf("test_fs: all assertions passed\n");
        return 0;
    }
    printf("test_fs: %d failure(s)\n", failures);
    return 1;
}
