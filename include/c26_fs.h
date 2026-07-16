#ifndef C26_FS_H
#define C26_FS_H

#include <stddef.h>
#include <stdint.h>

#define C26_FS_NAME_MAX 15U
#define C26_FS_FILE_MAX (128U * 1024U)
#define C26_FS_FILE_COUNT 64U

int c26_fs_init(void);
int c26_fs_online(void);
size_t c26_fs_count(void);
int c26_fs_entry(size_t index, const char **name, uint32_t *size);
int c26_fs_save(const char *name, const void *data, size_t size);
int c26_fs_load(const char *name, void *data, size_t capacity, size_t *size);
int c26_fs_delete(const char *name);
int c26_fs_rename(const char *old_name, const char *new_name);

#endif
