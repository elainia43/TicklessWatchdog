#include "k_kern.h"

void *pool_alloc(struct kpool *pool) {
    void *obj = NULL;

    K_ASSERT(pool != NULL && pool->obj_size > 0);

    if (!kpool_list_empty(&pool->free_blocks)) {  // if there are cached free blocks, use it
        obj = (void *)kpool_list_pop(&pool->free_blocks);
    } else if (pool->bound_index < pool->pool_size) {  // if there are no free blocks, but still have space in the pool
        obj = pool->storage + pool->bound_index * pool->obj_size;
        pool->bound_index++;
    } else {
        return NULL;  // no free blocks and no space in the pool
    }

#ifdef KPOOL_DEBUG
    size_t index = (obj - pool->storage) / pool->obj_size;
    K_ASSERT(bitmap_test_bit(pool->blocks, index) == 0);
    bitmap_set_bit(pool->blocks, index);
#endif

    return obj;
}

void pool_free(struct kpool *pool, void *obj) {
    size_t index;

    K_ASSERT(pool != NULL && obj != NULL);

    // K_ASSERT(pool_belong(pool, obj));
    if (!pool_belong(pool, obj)) {
        return;
    }

#ifdef KPOOL_DEBUG
    index = (obj - pool->storage) / pool->obj_size;
    K_ASSERT(bitmap_test_bit(pool->blocks, index) == 1);
    bitmap_clear_bit(pool->blocks, index);
#endif

    // add the block to the free list
    kpool_list_push(&pool->free_blocks, obj);
}

int pool_belong(const struct kpool *pool, const void *obj) {
    return (pool->storage <= obj) && (obj + pool->obj_size <= pool->storage + pool->pool_size) && ((obj - pool->storage) % pool->obj_size == 0);
}
