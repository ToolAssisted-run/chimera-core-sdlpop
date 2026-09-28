/* coroutine.h - the game's own stack (coroutine.c). */
#ifndef CHIMERA_COROUTINE_H
#define CHIMERA_COROUTINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A new stack of `size` bytes whose first switch-in calls chimera_co_entry()
 * (defined by the driver; it must never return). Returns the stack pointer to
 * switch to, or NULL. */
void *chimera_co_create(size_t size);

/* Saves the running context's stack pointer into *save_sp and continues the
 * context whose stack pointer is load_sp. Returns when something switches back. */
void chimera_co_switch(void **save_sp, void *load_sp);

void chimera_co_entry(void);

#ifdef __cplusplus
}
#endif

#endif
