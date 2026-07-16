#ifndef C26_BLOCK_H
#define C26_BLOCK_H

#include <stdint.h>

#define C26_BLOCK_SECTOR_SIZE 512U

int c26_block_init(void);
int c26_block_online(void);
uint64_t c26_block_sector_count(void);
int c26_block_read(uint64_t sector, void *buffer);
int c26_block_write(uint64_t sector, const void *buffer);
int c26_block_flush(void);

#endif
