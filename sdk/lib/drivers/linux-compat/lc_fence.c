/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DMA fences, fence arrays, fence chains and fence unwrapping
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

struct lc_fence_waiter
{
    struct dma_fence_cb cb;
    struct completion done;
};

struct lc_private_stub
{
    struct dma_fence base;
    spinlock_t lock;
};

static atomic64_t lc_fence_context = ATOMIC64_INIT(1);
static DEFINE_SPINLOCK(lc_stub_lock);
static struct dma_fence lc_stub_fence;
static int lc_stub_ready;

static const char *lc_stub_name(struct dma_fence *fence)
{
    (void)fence;
    return "stub";
}

static const struct dma_fence_ops lc_stub_ops = {
    .get_driver_name = lc_stub_name,
    .get_timeline_name = lc_stub_name,
};

static void lc_private_stub_release(struct dma_fence *fence)
{
    kfree_rcu(container_of(fence, struct lc_private_stub, base), base.rcu);
}

static const struct dma_fence_ops lc_private_stub_ops = {
    .get_driver_name = lc_stub_name,
    .get_timeline_name = lc_stub_name,
    .release = lc_private_stub_release,
};

u64 dma_fence_context_alloc(unsigned int num)
{
    return (u64)atomic64_add_return(num, &lc_fence_context) - num;
}

void dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops, spinlock_t *lock, u64 context, u64 seqno)
{
    kref_init(&fence->refcount);
    fence->ops = ops;
    fence->lock = lock;
    INIT_LIST_HEAD(&fence->cb_list);
    fence->timestamp = 0;
    fence->context = context;
    fence->seqno = seqno;
    fence->flags = 0;
    fence->error = 0;
    __set_bit(DMA_FENCE_FLAG_INITIALIZED_BIT, &fence->flags);
    if (ops->use_64bit_seqno)
        __set_bit(DMA_FENCE_FLAG_SEQNO64_BIT, &fence->flags);
}

void dma_fence_init64(struct dma_fence *fence, const struct dma_fence_ops *ops, spinlock_t *lock, u64 context, u64 seqno)
{
    dma_fence_init(fence, ops, lock, context, seqno);
    __set_bit(DMA_FENCE_FLAG_SEQNO64_BIT, &fence->flags);
}

void dma_fence_free(struct dma_fence *fence)
{
    kfree_rcu(fence, rcu);
}

void dma_fence_release(struct kref *kref)
{
    struct dma_fence *fence = container_of(kref, struct dma_fence, refcount);

    if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) && !list_empty(&fence->cb_list))
    {
        WARN_ON(1);
        spin_lock(fence->lock);
        fence->error = -EDEADLK;
        dma_fence_signal_locked(fence);
        spin_unlock(fence->lock);
    }

    if (fence->ops->release)
        fence->ops->release(fence);
    else
        dma_fence_free(fence);
}

int dma_fence_signal_timestamp_locked(struct dma_fence *fence, ktime_t timestamp)
{
    struct dma_fence_cb *cb, *next;
    struct list_head callbacks;

    if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
        return -EINVAL;

    INIT_LIST_HEAD(&callbacks);
    list_splice_init(&fence->cb_list, &callbacks);
    fence->timestamp = timestamp;
    set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);

    list_for_each_entry_safe(cb, next, &callbacks, node)
    {
        INIT_LIST_HEAD(&cb->node);
        cb->func(fence, cb);
    }
    return 0;
}

int dma_fence_signal_timestamp(struct dma_fence *fence, ktime_t timestamp)
{
    int ret;

    if (!fence)
        return -EINVAL;
    spin_lock(fence->lock);
    ret = dma_fence_signal_timestamp_locked(fence, timestamp);
    spin_unlock(fence->lock);
    return ret;
}

int dma_fence_signal_locked(struct dma_fence *fence)
{
    return dma_fence_signal_timestamp_locked(fence, ktime_get());
}

int dma_fence_signal(struct dma_fence *fence)
{
    return dma_fence_signal_timestamp(fence, ktime_get());
}

bool dma_fence_is_signaled_locked(struct dma_fence *fence)
{
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
        return true;
    if (fence->ops->signaled && fence->ops->signaled(fence))
    {
        dma_fence_signal_locked(fence);
        return true;
    }
    return false;
}

bool dma_fence_is_signaled(struct dma_fence *fence)
{
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
        return true;
    if (fence->ops->signaled && fence->ops->signaled(fence))
    {
        dma_fence_signal(fence);
        return true;
    }
    return false;
}

static void lc_fence_enable_signaling_locked(struct dma_fence *fence)
{
    if (test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags))
        return;
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
        return;
    if (fence->ops->enable_signaling && !fence->ops->enable_signaling(fence))
        dma_fence_signal_locked(fence);
}

void dma_fence_enable_sw_signaling(struct dma_fence *fence)
{
    spin_lock(fence->lock);
    lc_fence_enable_signaling_locked(fence);
    spin_unlock(fence->lock);
}

int dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb, dma_fence_func_t func)
{
    int ret = 0;

    if (WARN_ON(!fence || !func))
        return -EINVAL;
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
    {
        INIT_LIST_HEAD(&cb->node);
        return -ENOENT;
    }

    spin_lock(fence->lock);
    lc_fence_enable_signaling_locked(fence);
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
    {
        ret = -ENOENT;
    }
    else
    {
        cb->func = func;
        list_add_tail(&cb->node, &fence->cb_list);
    }
    spin_unlock(fence->lock);
    if (ret)
        INIT_LIST_HEAD(&cb->node);
    return ret;
}

bool dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
    bool removed;

    spin_lock(fence->lock);
    removed = !list_empty(&cb->node);
    if (removed)
        list_del_init(&cb->node);
    spin_unlock(fence->lock);
    return removed;
}

static void lc_fence_wake(struct dma_fence *fence, struct dma_fence_cb *cb)
{
    struct lc_fence_waiter *waiter = container_of(cb, struct lc_fence_waiter, cb);

    (void)fence;
    complete_all(&waiter->done);
}

signed long dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
    struct lc_fence_waiter waiter;
    signed long ret;

    (void)intr;
    if (dma_fence_is_signaled(fence))
        return timeout ? timeout : 1;
    if (!timeout)
        return 0;

    init_completion(&waiter.done);
    if (dma_fence_add_callback(fence, &waiter.cb, lc_fence_wake))
        return timeout;

    ret = (signed long)wait_for_completion_timeout(&waiter.done, (unsigned long)timeout);
    if (!ret && !dma_fence_remove_callback(fence, &waiter.cb))
        ret = 1;
    if (ret && dma_fence_remove_callback(fence, &waiter.cb))
        WARN_ON(!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags));
    return ret;
}

signed long dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
    if (WARN_ON(timeout < 0))
        return -EINVAL;
    if (fence->ops->wait)
        return fence->ops->wait(fence, intr, timeout);
    return dma_fence_default_wait(fence, intr, timeout);
}

void dma_fence_set_deadline(struct dma_fence *fence, ktime_t deadline)
{
    if (fence->ops->set_deadline && !dma_fence_is_signaled(fence))
        fence->ops->set_deadline(fence, deadline);
}

int dma_fence_get_status(struct dma_fence *fence)
{
    int status;

    spin_lock(fence->lock);
    status = dma_fence_get_status_locked(fence);
    spin_unlock(fence->lock);
    return status;
}

struct dma_fence *dma_fence_get_stub(void)
{
    spin_lock(&lc_stub_lock);
    if (!lc_stub_ready)
    {
        dma_fence_init(&lc_stub_fence, &lc_stub_ops, &lc_stub_lock, 0, 0);
        set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &lc_stub_fence.flags);
        dma_fence_signal_locked(&lc_stub_fence);
        lc_stub_ready = 1;
    }
    spin_unlock(&lc_stub_lock);
    return dma_fence_get(&lc_stub_fence);
}

struct dma_fence *dma_fence_allocate_private_stub(ktime_t timestamp)
{
    struct lc_private_stub *stub = kzalloc(sizeof(*stub), GFP_KERNEL);

    if (!stub)
        return NULL;
    spin_lock_init(&stub->lock);
    dma_fence_init(&stub->base, &lc_private_stub_ops, &stub->lock, 0, 0);
    set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &stub->base.flags);
    dma_fence_signal_timestamp(&stub->base, timestamp);
    return &stub->base;
}

static const char *lc_array_name(struct dma_fence *fence)
{
    (void)fence;
    return "array";
}

struct lc_array_cb
{
    struct dma_fence_cb cb;
    struct dma_fence_array *array;
};

static void lc_array_child_done(struct dma_fence *child, struct dma_fence_cb *cb)
{
    struct dma_fence_array *array = container_of(cb, struct lc_array_cb, cb)->array;

    if (child->error)
        cmpxchg(&array->base.error, 0, child->error);
    if (atomic_dec_and_test(&array->num_pending))
        dma_fence_signal(&array->base);
    dma_fence_put(&array->base);
}

static bool lc_array_enable_signaling(struct dma_fence *fence)
{
    struct dma_fence_array *array = container_of(fence, struct dma_fence_array, base);
    unsigned int i;

    for (i = 0; i < array->num_fences; ++i)
    {
        struct lc_array_cb *cb = (struct lc_array_cb *)array->callbacks + i;

        cb->array = array;
        dma_fence_get(&array->base);
        if (dma_fence_add_callback(array->fences[i], &cb->cb, lc_array_child_done))
        {
            int error = array->fences[i]->error;

            dma_fence_put(&array->base);
            if (error)
                cmpxchg(&array->base.error, 0, error);
            if (atomic_dec_and_test(&array->num_pending))
                return false;
        }
    }
    return true;
}

static bool lc_array_signaled(struct dma_fence *fence)
{
    struct dma_fence_array *array = container_of(fence, struct dma_fence_array, base);

    return atomic_read(&array->num_pending) <= 0;
}

static void lc_array_release(struct dma_fence *fence)
{
    struct dma_fence_array *array = container_of(fence, struct dma_fence_array, base);
    unsigned int i;

    for (i = 0; i < array->num_fences; ++i)
        dma_fence_put(array->fences[i]);
    kfree(array->fences);
    kfree(array->callbacks);
    dma_fence_free(fence);
}

const struct dma_fence_ops dma_fence_array_ops = {
    .get_driver_name = lc_array_name,
    .get_timeline_name = lc_array_name,
    .enable_signaling = lc_array_enable_signaling,
    .signaled = lc_array_signaled,
    .release = lc_array_release,
};

struct dma_fence_array *dma_fence_array_create(int num_fences, struct dma_fence **fences, u64 context, unsigned int seqno,
                                               bool signal_on_any)
{
    struct dma_fence_array *array;

    if (num_fences <= 0)
        return NULL;
    array = kzalloc(sizeof(*array), GFP_KERNEL);
    if (!array)
        return NULL;
    array->callbacks = kcalloc((size_t)num_fences, sizeof(struct lc_array_cb), GFP_KERNEL);
    if (!array->callbacks)
    {
        kfree(array);
        return NULL;
    }
    spin_lock_init(&array->lock);
    dma_fence_init(&array->base, &dma_fence_array_ops, &array->lock, context, seqno);
    array->num_fences = (unsigned int)num_fences;
    atomic_set(&array->num_pending, signal_on_any ? 1 : num_fences);
    array->fences = fences;
    return array;
}

struct dma_fence *dma_fence_array_first(struct dma_fence *head)
{
    struct dma_fence_array *array;

    if (!head)
        return NULL;
    array = to_dma_fence_array(head);
    if (!array)
        return head;
    return array->num_fences ? array->fences[0] : NULL;
}

struct dma_fence *dma_fence_array_next(struct dma_fence *head, unsigned int index)
{
    struct dma_fence_array *array = to_dma_fence_array(head);

    if (!array || index >= array->num_fences)
        return NULL;
    return array->fences[index];
}

static const char *lc_chain_name(struct dma_fence *fence)
{
    (void)fence;
    return "chain";
}

static struct dma_fence *lc_chain_get_prev(struct dma_fence_chain *chain)
{
    struct dma_fence *prev;

    rcu_read_lock();
    prev = dma_fence_get_rcu_safe(&chain->prev);
    rcu_read_unlock();
    return prev;
}

struct dma_fence *dma_fence_chain_walk(struct dma_fence *fence)
{
    struct dma_fence_chain *chain = to_dma_fence_chain(fence);
    struct dma_fence *prev;

    if (!chain)
    {
        dma_fence_put(fence);
        return NULL;
    }

    for (;;)
    {
        struct dma_fence_chain *prev_chain;
        struct dma_fence *replacement, *dropped = NULL;

        prev = lc_chain_get_prev(chain);
        prev_chain = to_dma_fence_chain(prev);
        if (!prev_chain || !dma_fence_is_signaled(prev_chain->fence))
            break;

        replacement = lc_chain_get_prev(prev_chain);
        if (cmpxchg(&chain->prev, prev, replacement) == prev)
            dropped = prev;
        else
            dma_fence_put(replacement);
        dma_fence_put(dropped);
        dma_fence_put(prev);
    }

    dma_fence_put(fence);
    return prev;
}

int dma_fence_chain_find_seqno(struct dma_fence **pfence, u64 seqno)
{
    struct dma_fence_chain *head;
    struct dma_fence *node;

    if (!seqno)
        return 0;
    head = to_dma_fence_chain(*pfence);
    if (!head || head->base.seqno < seqno)
        return -EINVAL;

    node = dma_fence_get(&head->base);
    for (;;)
    {
        struct dma_fence_chain *chain = to_dma_fence_chain(node);
        struct dma_fence *prev;

        if (!chain || chain->base.context != head->base.context || chain->prev_seqno < seqno)
            break;
        prev = lc_chain_get_prev(chain);
        dma_fence_put(node);
        node = prev;
        if (!node)
            break;
    }

    dma_fence_put(*pfence);
    *pfence = node;
    return 0;
}

static bool lc_chain_enable_signaling(struct dma_fence *fence);

static void lc_chain_rearm(struct work_struct *work)
{
    struct dma_fence_chain *chain = container_of(work, struct dma_fence_chain, work);

    if (!lc_chain_enable_signaling(&chain->base))
        dma_fence_signal(&chain->base);
    dma_fence_put(&chain->base);
}

static void lc_chain_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
    struct dma_fence_chain *chain = container_of(cb, struct dma_fence_chain, cb);

    (void)fence;
    queue_work(system_wq, &chain->work);
}

static bool lc_chain_enable_signaling(struct dma_fence *fence)
{
    struct dma_fence_chain *head = to_dma_fence_chain(fence);
    struct dma_fence *node;

    dma_fence_get(&head->base);
    dma_fence_chain_for_each(node, &head->base)
    {
        struct dma_fence *contained = dma_fence_chain_contained(node);

        dma_fence_get(contained);
        if (!dma_fence_add_callback(contained, &head->cb, lc_chain_cb))
        {
            dma_fence_put(contained);
            dma_fence_put(node);
            return true;
        }
        dma_fence_put(contained);
    }
    dma_fence_put(&head->base);
    return false;
}

static bool lc_chain_signaled(struct dma_fence *fence)
{
    struct dma_fence *node;

    dma_fence_chain_for_each(node, fence)
    {
        struct dma_fence *contained = dma_fence_chain_contained(node);

        if (!dma_fence_is_signaled(contained))
        {
            dma_fence_put(node);
            return false;
        }
    }
    return true;
}

static void lc_chain_release(struct dma_fence *fence)
{
    struct dma_fence_chain *chain = container_of(fence, struct dma_fence_chain, base);
    struct dma_fence *prev = chain->prev;

    while (prev)
    {
        struct dma_fence_chain *prev_chain = to_dma_fence_chain(prev);
        struct dma_fence *next;

        if (!prev_chain || kref_read(&prev->refcount) > 1)
            break;
        next = prev_chain->prev;
        prev_chain->prev = NULL;
        dma_fence_put(prev);
        prev = next;
    }
    dma_fence_put(prev);
    dma_fence_put(chain->fence);
    dma_fence_free(fence);
}

static void lc_chain_set_deadline(struct dma_fence *fence, ktime_t deadline)
{
    struct dma_fence *node;

    dma_fence_chain_for_each(node, fence)
        dma_fence_set_deadline(dma_fence_chain_contained(node), deadline);
}

const struct dma_fence_ops dma_fence_chain_ops = {
    .use_64bit_seqno = true,
    .get_driver_name = lc_chain_name,
    .get_timeline_name = lc_chain_name,
    .enable_signaling = lc_chain_enable_signaling,
    .signaled = lc_chain_signaled,
    .release = lc_chain_release,
    .set_deadline = lc_chain_set_deadline,
};

void dma_fence_chain_init(struct dma_fence_chain *chain, struct dma_fence *prev, struct dma_fence *fence, u64 seqno)
{
    struct dma_fence_chain *prev_chain = to_dma_fence_chain(prev);
    u64 context;

    spin_lock_init(&chain->lock);
    chain->prev = prev;
    chain->fence = fence;
    chain->prev_seqno = 0;
    INIT_WORK(&chain->work, lc_chain_rearm);

    if (prev_chain && seqno > prev->seqno)
    {
        context = prev->context;
        chain->prev_seqno = prev->seqno;
    }
    else
    {
        context = dma_fence_context_alloc(1);
        if (prev_chain)
            seqno = max(prev->seqno, seqno);
    }
    dma_fence_init64(&chain->base, &dma_fence_chain_ops, &chain->lock, context, seqno);
}

static struct dma_fence *lc_unwrap_enter(struct dma_fence_unwrap *cursor)
{
    cursor->array = dma_fence_chain_contained(cursor->chain);
    cursor->index = 0;
    return dma_fence_array_first(cursor->array);
}

struct dma_fence *dma_fence_unwrap_first(struct dma_fence *head, struct dma_fence_unwrap *cursor)
{
    cursor->chain = dma_fence_get(head);
    return lc_unwrap_enter(cursor);
}

struct dma_fence *dma_fence_unwrap_next(struct dma_fence_unwrap *cursor)
{
    struct dma_fence *next = dma_fence_array_next(cursor->array, ++cursor->index);

    if (next)
        return next;
    cursor->chain = dma_fence_chain_walk(cursor->chain);
    return lc_unwrap_enter(cursor);
}

struct dma_fence *__dma_fence_unwrap_merge(unsigned int num_fences, struct dma_fence **fences, struct dma_fence_unwrap *cursors)
{
    struct dma_fence **out, *fence;
    struct dma_fence_array *array;
    unsigned int count = 0, capacity = 0, i, j;

    for (i = 0; i < num_fences; ++i)
    {
        dma_fence_unwrap_for_each(fence, &cursors[i], fences[i])
            capacity++;
    }
    if (!capacity)
        return dma_fence_allocate_private_stub(ktime_get());

    out = kcalloc(capacity, sizeof(*out), GFP_KERNEL);
    if (!out)
        return NULL;

    for (i = 0; i < num_fences; ++i)
    {
        dma_fence_unwrap_for_each(fence, &cursors[i], fences[i])
        {
            bool merged = false;

            if (dma_fence_is_signaled(fence))
                continue;
            for (j = 0; j < count; ++j)
            {
                if (out[j]->context == fence->context)
                {
                    if (__dma_fence_is_later(fence, fence->seqno, out[j]->seqno))
                    {
                        dma_fence_put(out[j]);
                        out[j] = dma_fence_get(fence);
                    }
                    merged = true;
                    break;
                }
            }
            if (!merged && count < capacity)
                out[count++] = dma_fence_get(fence);
        }
    }

    if (!count)
    {
        kfree(out);
        return dma_fence_allocate_private_stub(ktime_get());
    }
    if (count == 1)
    {
        fence = out[0];
        kfree(out);
        return fence;
    }

    array = dma_fence_array_create((int)count, out, dma_fence_context_alloc(1), 1, false);
    if (!array)
    {
        for (j = 0; j < count; ++j)
            dma_fence_put(out[j]);
        kfree(out);
        return NULL;
    }
    return &array->base;
}
