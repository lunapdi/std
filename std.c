#include "std.h"

void noop_dealloc(void *self, void *ptr)
{
    (void)self;
    (void)ptr;
}

void *fixed_buf_allocator_alloc(void *self, size_t size)
{
    fixed_buf_allocator_t *a = self;
    size_t pos = a->pos;

    if (a->pos + size >= a->size) {
        return NULL;
    }

    a->pos += size;
    return a->buf + pos;
}

allocator_i init_fixed_buf_allocator_i(fixed_buf_allocator_t *a)
{
    allocator_i i;
    i.self = a;
    i.alloc = fixed_buf_allocator_alloc;
    i.dealloc = noop_dealloc;

    return i;
}

static char default_fixed_buf[32 * 1024];
static fixed_buf_allocator_t default_fixed_buf_allocator = {
    default_fixed_buf,
    sizeof(default_fixed_buf),
    0,
};

static allocator_i global_allocator = {
    &default_fixed_buf_allocator,
    fixed_buf_allocator_alloc,
    noop_dealloc,
};

void *alloc(size_t size)
{
    return global_allocator.alloc(global_allocator.self, size);
}

void dealloc(void *ptr)
{
    global_allocator.dealloc(global_allocator.self, ptr);
}
