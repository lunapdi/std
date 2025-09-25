#ifndef STD_H
#define STD_H

#define _DEFAULT_SOURCE 1
#include <stdint.h>
#include <pthread.h>

typedef struct allocator {
    void *self;
    void *(*alloc)(void *self, size_t size);
    void (*dealloc)(void *self, void *ptr);
} allocator_i;

typedef struct fixed_buf_allocator {
    char *buf;
    size_t size;
    size_t pos;
} fixed_buf_allocator_t;

allocator_i init_fixed_buf_allocator_i(fixed_buf_allocator_t *a);

static allocator_i global_allocator;

void *alloc(size_t size);
void dealloc(void *ptr);

#endif /* STD_H */
