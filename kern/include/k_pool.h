
#ifndef _K_POOL_H_
#define _K_POOL_H_

#include <stdint.h>
#include <stdlib.h>

/*
********************************************************************************
*                            Pool Management
********************************************************************************
*/
// define this macro to enable pool alloc debug
#define KPOOL_DEBUG 1

// structure for the pool list of free blocks

typedef struct kpool_link {
    struct kpool_link *next;
} kpool_link_t;

typedef struct kpool_list {
    struct kpool_link *next_free;
    int count;
} kpool_list_t;

static inline int kpool_list_empty(kpool_list_t *list) {
    return list->next_free == NULL;
}

static inline void kpool_list_push(kpool_list_t *list, void *obj) {
    ((kpool_link_t *)obj)->next = list->next_free;
    list->next_free = (kpool_link_t *)obj;
    list->count++;
}

static inline void *kpool_list_pop(kpool_list_t *list) {
    void *first = list->next_free;
    if (!first) {
        return NULL;
    }
    list->next_free = ((kpool_link_t *)first)->next;
    list->count--;
    return first;
}

/** Pool structure */
typedef struct kpool {
    void *storage;             // pointer to the memory of the pool (array of objects)
    kpool_list_t free_blocks;  // list of free blocks
    size_t obj_size;           // size of the object in the pool (in bytes)
    size_t pool_size;          // size of the pool (count of objects)
    size_t bound_index;        // boundary index of beginning of non-allocated memory
#ifdef KPOOL_DEBUG
    BITMAP_DEFINE(blocks, KCONF_POOL_MAX_OBJECTS);
#endif
} kpool_t;

#ifdef KPOOL_DEBUG
#define POOL_BLOCKS_INIT .blocks = {0},
#else
#define POOL_BLOCKS_INIT
#endif

// Create pool descriptor with specific attributes, like alignment: __attribute__ ((aligned (OBJECT_TYPE_ALIGN))))
#define DEFINE_POOL_ATTR(name, object_type, size, attr) \
    static union {                                      \
        object_type object;                             \
        kpool_link_t free_link;                         \
    } __pool_storage_##name[size] attr;                 \
    static struct kpool name = {                        \
        .storage = __pool_storage_##name,               \
        .free_blocks = {0},                             \
        .obj_size = sizeof(__pool_storage_##name[0]),   \
        .pool_size = sizeof(__pool_storage_##name),     \
        .bound_index = 0,                               \
        POOL_BLOCKS_INIT};

// Create pool descriptor. Use this macro if you don't need any specific attributes.
#define DEFINE_POOL(name, object_type, size) \
    DEFINE_POOL_ATTR(name, object_type, size, )

// allcoate an object from the pool and return it to the caller
extern void *pool_alloc(struct kpool *pool);

// free an object and return it to the pool
extern void pool_free(struct kpool *pool, void *obj);

// check if the object belongs to the pool
extern int pool_belong(const struct kpool *pool, const void *obj);

// heap memory allocation: just use malloc/free in stdlib
#define k_malloc(size) malloc(size)
#define k_free(ptr) free(ptr)

#endif /* _K_POOL_H_ */
