#include <unistd.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>


#define ALIGNMENT (sizeof(void *))
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define CHUNK_SIZE (4096)

#define HDR_SIZE ALIGN_UP(sizeof(block_header_t), ALIGNMENT)


static void *header_to_payload(block_header_t *h) {
    return (void *)((char *)h + HDR_SIZE);
}
static block_header_t *payload_to_header(void *p) {
    return (block_header_t *)((char *)p - HDR_SIZE);
}

/* Find a free block using first-fit */
static block_header_t *find_free_block(size_t size) {
    block_header_t *cur = free_list;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return NULL;
}

/* Remove block from free list */
static void remove_from_free_list(block_header_t *b) {
    if (!b) return;
    if (b->prev) b->prev->next = b->next;
    else free_list = b->next;
    if (b->next) b->next->prev = b->prev;
    b->prev = b->next = NULL;
}

/* Insert block into free list (LIFO) */
static void insert_into_free_list(block_header_t *b) {
    b->free = 1;
    b->prev = NULL;
    b->next = free_list;
    if (free_list) free_list->prev = b;
    free_list = b;
}

/* Extend heap with sbrk to allocate a new block of at least `size` bytes payload */
static block_header_t *request_space_from_os(size_t size) {
    size_t total = HDR_SIZE + size;
    /* grow by multiples of CHUNK_SIZE for fewer sbrk calls */
    size_t grow = ((total + CHUNK_SIZE - 1) / CHUNK_SIZE) * CHUNK_SIZE;
    void *p = sbrk(grow);
    if (p == (void *) -1) return NULL;
    /* create new block header at p */
    block_header_t *h = (block_header_t *)p;
    h->size = grow - HDR_SIZE;
    h->free = 0;
    h->prev = h->next = NULL;
    return h;
}

/* Split a block if it's substantially larger than requested */
static void split_block(block_header_t *b, size_t size) {
    /* only split if remaining space can hold a header + minimal payload (alignment) */
    if (b->size >= size + HDR_SIZE + ALIGNMENT) {
        char *new_block_addr = (char *)header_to_payload(b) + size;
        block_header_t *nb = (block_header_t *)new_block_addr;
        nb->size = b->size - size - HDR_SIZE;
        nb->free = 1;
        b->size = size;
        /* insert new block into free list in place of b */
        nb->prev = b->prev;
        nb->next = b->next;
        if (nb->prev) nb->prev->next = nb;
        else free_list = nb;
        if (nb->next) nb->next->prev = nb;
        /* ensure b is removed from free list (we'll likely do it by overwriting) */
        b->prev = b->next = NULL;
    }
}

/* Try to coalesce adjacent free blocks: coalesce with next if contiguous in memory.
   Note: this simple allocator only checks adjacency using pointer arithmetic,
   assuming headers are placed sequentially as allocated from sbrk or split. */
static void coalesce_if_possible(block_header_t *b) {
    if (!b) return;
    /* Try coalesce with next block in memory */
    char *end_of_b = (char *)header_to_payload(b) + b->size;
    block_header_t *candidate = (block_header_t *)end_of_b;
    /* crude check: candidate must be within heap and marked free, and be a plausible header.
       This is not robust against unrelated data; it's fine for our simple model. */
    if (candidate && candidate->free) {
        /* unlink candidate */
        remove_from_free_list(candidate);
        /* merge sizes */
        b->size += HDR_SIZE + candidate->size;
    }
    /* Coalescing with previous block is harder without footer or global metadata.
       We keep simple approach: when freeing a block we try to scan the free list to find
       an adjacent previous block and merge if found. */
}

/* When freeing, we search free list for adjacent block that precedes `b` */
static void coalesce_with_prev_if_possible(block_header_t *b) {
    block_header_t *cur = free_list;
    while (cur) {
        char *end_of_cur = (char *)header_to_payload(cur) + cur->size;
        if (end_of_cur == (char *)b) {
            /* cur directly precedes b in memory -> merge cur and b */
            remove_from_free_list(b); /* b might be temporarily in list; ensure clean */
            remove_from_free_list(cur);
            cur->size += HDR_SIZE + b->size;
            insert_into_free_list(cur);
            b = cur; /* merged result */
            break;
        }
        cur = cur->next;
    }
    /* also attempt to coalesce forward */
    coalesce_if_possible(b);
}

/* Public API */

void *malloc(size_t size) {
    if (size == 0) return NULL;
    LOCK();
    size_t req = ALIGN_UP(size, ALIGNMENT);
    block_header_t *b = find_free_block(req);
    if (b) {
        /* found a free block */
        remove_from_free_list(b);
        /* If block is much larger, split it and keep remainder in free list */
        split_block(b, req);
        b->free = 0;
        UNLOCK();
        return header_to_payload(b);
    }
    /* no suitable free block -> request from OS */
    block_header_t *newb = request_space_from_os(req);
    if (!newb) {
        UNLOCK();
        return NULL;
    }
    /* if the new block is bigger than requested (because we rounded growth), consider splitting */
    if (newb->size > req) {
        /* if remainder big enough, split and insert remainder into free list */
        size_t original_size = newb->size;
        /* set newb->size to requested */
        newb->size = req;
        /* remainder block header located after payload */
        char *rem_addr = (char *)header_to_payload(newb) + req;
        block_header_t *rem = (block_header_t *)rem_addr;
        rem->size = original_size - req - HDR_SIZE;
        rem->free = 1;
        rem->prev = rem->next = NULL;
        insert_into_free_list(rem);
    }
    newb->free = 0;
    UNLOCK();
    return header_to_payload(newb);
}

void free(void *ptr) {
    if (!ptr) return;
    LOCK();
    block_header_t *b = payload_to_header(ptr);
    b->free = 1;
    /* Insert into free list then attempt to coalesce */
    insert_into_free_list(b);
    /* try to merge with adjacent blocks */
    coalesce_with_prev_if_possible(b);
    UNLOCK();
}

void *calloc(size_t nmemb, size_t size) {
    /* check overflow */
    if (nmemb == 0 || size == 0) return NULL;
    if (nmemb > ((size_t)-1) / size) return NULL;
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (!p) return NULL;
    memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    LOCK();
    block_header_t *b = payload_to_header(ptr);
    size_t req = ALIGN_UP(size, ALIGNMENT);
    if (b->size >= req) {
        /* current block large enough; optionally split */
        split_block(b, req);
        UNLOCK();
        return ptr;
    } else {
        /* try to see if next block is free and big enough to expand in place */
        char *end_of_b = (char *)header_to_payload(b) + b->size;
        block_header_t *next = (block_header_t *)end_of_b;
        if (next && next->free && (b->size + HDR_SIZE + next->size) >= req) {
            /* absorb next */
            remove_from_free_list(next);
            b->size += HDR_SIZE + next->size;
            split_block(b, req);
            UNLOCK();
            return ptr;
        }
    }
    UNLOCK();
    /* otherwise allocate new block, copy, free old */
    void *newp = malloc(size);
    if (!newp) return NULL;
    memcpy(newp, ptr, (b->size < req) ? b->size : req);
    free(ptr);
    return newp;
}

/* Simple test/demo when compiled as standalone */
#ifdef SIMPLE_ALLOC_DEMO
#include <stdio.h>

int main(void) {
    printf("simple allocator demo\n");
    char *a = malloc(20);
    char *b = malloc(50);
    strcpy(a, "hello allocator");
    printf("a: %s\n", a);
    free(a);
    char *c = malloc(8); /* should reuse freed space perhaps */
    strcpy(c, "C!");
    printf("c: %s\n", c);
    free(b);
    free(c);
    void *p = calloc(10, 8);
    printf("calloc returned %p\n", p);
    free(p);
    printf("done\n");
    return 0;
}
#
