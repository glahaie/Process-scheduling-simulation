/* malloc.h
 * Pour tester les allocations et désallocations
 * Par Guillaume Lahaie
 */

#ifndef MALLOC_H
#define MALLOC_H
#include <stdlib.h>
#include <stdio.h>

void * my_malloc(size_t size);
void * my_realloc(void *ptr, size_t size);
void my_free(void *ptr);


void * my_malloc(size_t size) {

    void *temp;
    fprintf(stderr,"malloc de grandeur %d appele\n", (int)size);
    temp = malloc(size);
    return temp;
}


void * my_realloc(void *ptr, size_t size) {

    void * temp;
    fprintf(stderr,"realloc de grandeur %d appele\n", (int)size);
    temp = realloc(ptr, size);
    ptr = temp;
    return ptr;
}


void my_free(void *ptr) {

    fprintf(stderr,"free appele\n");
    free(ptr);
    return;
}

#endif
