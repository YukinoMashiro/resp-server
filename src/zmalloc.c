//
// Created by yukino on 2023/5/1.
//

#include <string.h>
#include <stdlib.h>

#include "zmalloc.h"

void *zmalloc(size_t size) {
    void *ptr = malloc(size);
    return ptr;
}
void *zcalloc(size_t size) {
    void *ptr = calloc(1, size);
    return ptr;
}

void *zrealloc(void *ptr, size_t size) {
    void *_ptr = realloc(ptr, size);
    return _ptr;
}

void zfree(void *ptr) {
    if (NULL == ptr) {
        return;
    }
    free(ptr);
}
