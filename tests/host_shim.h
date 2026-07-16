#ifndef C26_HOST_SHIM_H
#define C26_HOST_SHIM_H

/* Host-side shims standing in for the machine so kernel logic modules
 * (src/basic.c, src/fs.c) compile and run as ordinary processes. */

#include <stddef.h>
#include <stdint.h>

/* Captured console output. */
extern char shim_output[65536];
extern size_t shim_output_length;
void shim_output_reset(void);

/* Characters c26_io_pump will deliver, via the hook the test installs. */
void shim_pending_input(const char *text);
extern void (*shim_pump_hook)(char ch);

/* The in-memory virtio disk behind the block shims. */
#define SHIM_DISK_SECTORS 16384u
extern uint8_t shim_disk[SHIM_DISK_SECTORS * 512];
void shim_disk_reset(void);

#endif
