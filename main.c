#include <stdio.h>
#include <assert.h>
#include "std.h"

int main(void)
{
    int *ptr = alloc(sizeof(int));
    assert(ptr);

    *ptr = 5;
    printf("%d\n", *ptr);

    dealloc(ptr);
}


