//
// Created by yukino on 2023/5/1.
//

#ifndef RESP_SERVER_ZMALLOC_H
#define RESP_SERVER_ZMALLOC_H

#include <string.h>
#include <malloc.h>

#define zmalloc_usable(p) malloc_usable_size(p)

void *zmalloc(size_t size);
void *zcalloc(size_t size);
void *zrealloc(void *ptr, size_t size);
void zfree(void *ptr);

#endif //RESP_SERVER_ZMALLOC_H
