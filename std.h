#ifndef STD_H
#define STD_H

#define _DEFAULT_SOURCE 1
#include <stdint.h>
#include <pthread.h>

typedef struct block_header {
    struct block_header *prev;
    struct block_header *next;
    int64_t size; /* sign bit == free flag */
} block_header_t;

typedef struct base_allocator {
    block_header_t *free_list;
    pthread_mutex_t mutex;
} base_allocator_t;


void *base_alloc(base_allocator_t *a, size_t size);
void base_dealloc(base_allocator_t *a, void *ptr);

#endif /* STD_H */
