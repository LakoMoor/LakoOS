/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wound/wait mutexes and reservation objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

DEFINE_WW_CLASS(reservation_ww_class);
static DEFINE_MUTEX(lc_ww_acquire_lock);

void ww_acquire_init(struct ww_acquire_ctx *ctx, struct ww_class *ww_class)
{
    mutex_lock(&lc_ww_acquire_lock);
    ctx->ww_class = ww_class;
    ctx->acquired = 0;
}

void ww_acquire_fini(struct ww_acquire_ctx *ctx)
{
    WARN_ON(ctx->acquired);
    mutex_unlock(&lc_ww_acquire_lock);
}

int ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
    if (ctx && READ_ONCE(lock->ctx) == ctx)
        return -EALREADY;
    mutex_lock(&lock->base);
    WRITE_ONCE(lock->ctx, ctx);
    if (ctx)
        ctx->acquired++;
    return 0;
}

int ww_mutex_trylock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
    if (ctx && READ_ONCE(lock->ctx) == ctx)
        return -EALREADY;
    if (!mutex_trylock(&lock->base))
        return 0;
    WRITE_ONCE(lock->ctx, ctx);
    if (ctx)
        ctx->acquired++;
    return 1;
}

void ww_mutex_unlock(struct ww_mutex *lock)
{
    struct ww_acquire_ctx *ctx = lock->ctx;

    if (ctx)
        ctx->acquired--;
    WRITE_ONCE(lock->ctx, NULL);
    mutex_unlock(&lock->base);
}

void dma_resv_init(struct dma_resv *obj)
{
    ww_mutex_init(&obj->lock, &reservation_ww_class);
    spin_lock_init(&obj->lc_entries_lock);
    obj->entries = NULL;
    obj->count = 0;
    obj->reserved = 0;
    obj->capacity = 0;
}

void dma_resv_fini(struct dma_resv *obj)
{
    unsigned int i;

    for (i = 0; i < obj->count; ++i)
        dma_fence_put(obj->entries[i].fence);
    kfree(obj->entries);
    obj->entries = NULL;
    obj->count = 0;
    obj->capacity = 0;
}

static int lc_resv_grow(struct dma_resv *obj, unsigned int needed, gfp_t gfp)
{
    struct dma_resv_entry *entries, *old;
    unsigned int capacity;

    if (needed <= obj->capacity)
        return 0;
    capacity = max(needed, obj->capacity ? obj->capacity * 2 : 4U);
    entries = kmalloc_array(capacity, sizeof(*entries), gfp);
    if (!entries)
        return -ENOMEM;
    spin_lock(&obj->lc_entries_lock);
    if (obj->count)
        memcpy(entries, obj->entries, obj->count * sizeof(*entries));
    old = obj->entries;
    obj->entries = entries;
    obj->capacity = capacity;
    spin_unlock(&obj->lc_entries_lock);
    kfree(old);
    return 0;
}

int dma_resv_reserve_fences(struct dma_resv *obj, unsigned int num_fences)
{
    int err = lc_resv_grow(obj, obj->count + num_fences, GFP_KERNEL);

    if (!err)
        obj->reserved = max(obj->reserved, num_fences);
    return err;
}

void dma_resv_add_fence(struct dma_resv *obj, struct dma_fence *fence, enum dma_resv_usage usage)
{
    struct dma_fence *old = NULL;
    unsigned int i;

    dma_fence_get(fence);
    spin_lock(&obj->lc_entries_lock);
    for (i = 0; i < obj->count; ++i)
    {
        struct dma_resv_entry *entry = &obj->entries[i];

        if ((entry->fence->context == fence->context && dma_fence_is_later_or_same(fence, entry->fence)) ||
            test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &entry->fence->flags))
        {
            old = entry->fence;
            entry->fence = fence;
            entry->usage = usage;
            spin_unlock(&obj->lc_entries_lock);
            dma_fence_put(old);
            return;
        }
    }
    spin_unlock(&obj->lc_entries_lock);

    if (obj->count == obj->capacity)
    {
        WARN_ON(!obj->reserved);
        if (lc_resv_grow(obj, obj->count + 1, GFP_ATOMIC))
        {
            dma_fence_wait(fence, false);
            dma_fence_put(fence);
            return;
        }
    }

    spin_lock(&obj->lc_entries_lock);
    obj->entries[obj->count].fence = fence;
    obj->entries[obj->count].usage = usage;
    obj->count++;
    spin_unlock(&obj->lc_entries_lock);
    if (obj->reserved)
        obj->reserved--;
}

struct dma_fence *dma_resv_iter_first(struct dma_resv_iter *cursor)
{
    cursor->index = 0;
    cursor->is_restarted = true;
    return dma_resv_iter_next(cursor);
}

struct dma_fence *dma_resv_iter_next(struct dma_resv_iter *cursor)
{
    struct dma_resv *obj = cursor->obj;

    while (cursor->index < obj->count)
    {
        struct dma_resv_entry *entry = &obj->entries[cursor->index++];

        if (entry->usage <= cursor->usage)
        {
            if (cursor->fence)
                cursor->is_restarted = false;
            cursor->fence = entry->fence;
            cursor->fence_usage = entry->usage;
            return entry->fence;
        }
    }
    cursor->fence = NULL;
    return NULL;
}

static unsigned int lc_resv_snapshot(struct dma_resv *obj, enum dma_resv_usage usage, struct dma_fence ***out)
{
    struct dma_fence **fences = NULL;
    unsigned int count = 0, capacity, i;

    for (;;)
    {
        capacity = READ_ONCE(obj->count);
        kfree(fences);
        fences = capacity ? kmalloc_array(capacity, sizeof(*fences), GFP_KERNEL) : NULL;
        if (capacity && !fences)
        {
            *out = NULL;
            return 0;
        }
        spin_lock(&obj->lc_entries_lock);
        if (obj->count > capacity)
        {
            spin_unlock(&obj->lc_entries_lock);
            continue;
        }
        for (i = 0; i < obj->count; ++i)
        {
            if (obj->entries[i].usage <= usage)
                fences[count++] = dma_fence_get(obj->entries[i].fence);
        }
        spin_unlock(&obj->lc_entries_lock);
        break;
    }
    *out = fences;
    return count;
}

bool dma_resv_test_signaled(struct dma_resv *obj, enum dma_resv_usage usage)
{
    struct dma_fence **fences;
    unsigned int count, i;
    bool signaled = true;

    count = lc_resv_snapshot(obj, usage, &fences);
    for (i = 0; i < count; ++i)
    {
        if (signaled && !dma_fence_is_signaled(fences[i]))
            signaled = false;
        dma_fence_put(fences[i]);
    }
    kfree(fences);
    return signaled;
}

long dma_resv_wait_timeout(struct dma_resv *obj, enum dma_resv_usage usage, bool intr, unsigned long timeout)
{
    struct dma_fence **fences;
    unsigned int count, i;
    long ret = timeout ? (long)timeout : 1;

    count = lc_resv_snapshot(obj, usage, &fences);
    for (i = 0; i < count; ++i)
    {
        if (ret > 0)
        {
            long r = dma_fence_wait_timeout(fences[i], intr, timeout ? ret : 0);

            ret = r;
        }
        dma_fence_put(fences[i]);
    }
    kfree(fences);
    return ret;
}

int dma_resv_get_singleton(struct dma_resv *obj, enum dma_resv_usage usage, struct dma_fence **fence)
{
    struct dma_fence **fences;
    struct dma_fence_array *array;
    unsigned int count;

    count = lc_resv_snapshot(obj, usage, &fences);
    if (!count)
    {
        kfree(fences);
        *fence = NULL;
        return 0;
    }
    if (count == 1)
    {
        *fence = fences[0];
        kfree(fences);
        return 0;
    }
    array = dma_fence_array_create((int)count, fences, dma_fence_context_alloc(1), 1, false);
    if (!array)
    {
        while (count--)
            dma_fence_put(fences[count]);
        kfree(fences);
        return -ENOMEM;
    }
    *fence = &array->base;
    return 0;
}
