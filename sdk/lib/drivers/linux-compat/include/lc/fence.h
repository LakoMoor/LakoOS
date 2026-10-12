/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux DMA fences, fence containers, reservation objects and file stubs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct dma_fence;
struct dma_fence_cb;
typedef void (*dma_fence_func_t)(struct dma_fence *fence, struct dma_fence_cb *cb);

struct dma_fence_cb
{
    struct list_head node;
    dma_fence_func_t func;
};

struct dma_fence_ops
{
    bool use_64bit_seqno;
    const char *(*get_driver_name)(struct dma_fence *fence);
    const char *(*get_timeline_name)(struct dma_fence *fence);
    bool (*enable_signaling)(struct dma_fence *fence);
    bool (*signaled)(struct dma_fence *fence);
    signed long (*wait)(struct dma_fence *fence, bool intr, signed long timeout);
    void (*release)(struct dma_fence *fence);
    void (*set_deadline)(struct dma_fence *fence, ktime_t deadline);
};

struct dma_fence
{
    spinlock_t *lock;
    const struct dma_fence_ops *ops;
    struct list_head cb_list;
    ktime_t timestamp;
    struct rcu_head rcu;
    u64 context;
    u64 seqno;
    unsigned long flags;
    struct kref refcount;
    int error;
};

enum dma_fence_flag_bits
{
    DMA_FENCE_FLAG_INITIALIZED_BIT,
    DMA_FENCE_FLAG_SEQNO64_BIT,
    DMA_FENCE_FLAG_SIGNALED_BIT,
    DMA_FENCE_FLAG_TIMESTAMP_BIT,
    DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
    DMA_FENCE_FLAG_USER_BITS,
};

u64 dma_fence_context_alloc(unsigned int num);
void dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops, spinlock_t *lock, u64 context, u64 seqno);
void dma_fence_init64(struct dma_fence *fence, const struct dma_fence_ops *ops, spinlock_t *lock, u64 context, u64 seqno);
void dma_fence_release(struct kref *kref);
void dma_fence_free(struct dma_fence *fence);
static inline struct dma_fence *dma_fence_get(struct dma_fence *fence)
{
    if (fence)
        kref_get(&fence->refcount);
    return fence;
}
static inline void dma_fence_put(struct dma_fence *fence)
{
    if (fence)
        kref_put(&fence->refcount, dma_fence_release);
}
static inline struct dma_fence *dma_fence_get_rcu(struct dma_fence *fence)
{
    return kref_get_unless_zero(&fence->refcount) ? fence : NULL;
}
static inline struct dma_fence *dma_fence_get_rcu_safe(struct dma_fence **fencep)
{
    for (;;)
    {
        struct dma_fence *fence = READ_ONCE(*fencep);
        if (!fence)
            return NULL;
        if (!dma_fence_get_rcu(fence))
            continue;
        if (fence == READ_ONCE(*fencep))
            return fence;
        dma_fence_put(fence);
    }
}
int dma_fence_signal_timestamp_locked(struct dma_fence *fence, ktime_t timestamp);
int dma_fence_signal_timestamp(struct dma_fence *fence, ktime_t timestamp);
int dma_fence_signal_locked(struct dma_fence *fence);
int dma_fence_signal(struct dma_fence *fence);
bool dma_fence_is_signaled_locked(struct dma_fence *fence);
bool dma_fence_is_signaled(struct dma_fence *fence);
void dma_fence_enable_sw_signaling(struct dma_fence *fence);
int dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb, dma_fence_func_t func);
bool dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb);
signed long dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout);
signed long dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout);
static inline signed long dma_fence_wait(struct dma_fence *fence, bool intr)
{
    signed long ret = dma_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);
    return ret < 0 ? ret : 0;
}
void dma_fence_set_deadline(struct dma_fence *fence, ktime_t deadline);
int dma_fence_get_status(struct dma_fence *fence);
struct dma_fence *dma_fence_get_stub(void);
struct dma_fence *dma_fence_allocate_private_stub(ktime_t timestamp);
static inline void dma_fence_set_error(struct dma_fence *fence, int error) { fence->error = error; }
static inline int dma_fence_get_status_locked(struct dma_fence *fence)
{
    if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
        return fence->error ? fence->error : 1;
    return 0;
}
static inline ktime_t dma_fence_timestamp(struct dma_fence *fence)
{
    if (!test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags))
        return ktime_get();
    return fence->timestamp;
}
static inline bool __dma_fence_is_later(struct dma_fence *fence, u64 f1, u64 f2)
{
    if (test_bit(DMA_FENCE_FLAG_SEQNO64_BIT, &fence->flags))
        return f1 > f2;
    return (int)(lower_32_bits(f1) - lower_32_bits(f2)) > 0;
}
static inline bool dma_fence_is_later(struct dma_fence *f1, struct dma_fence *f2)
{
    if (f1->context != f2->context)
        return false;
    return __dma_fence_is_later(f1, f1->seqno, f2->seqno);
}
static inline bool dma_fence_is_later_or_same(struct dma_fence *f1, struct dma_fence *f2)
{
    return f1 == f2 || dma_fence_is_later(f1, f2);
}
static inline struct dma_fence *dma_fence_later(struct dma_fence *f1, struct dma_fence *f2)
{
    return dma_fence_is_later(f1, f2) ? f1 : f2;
}
#define dma_fence_lock_irqsave(f, flags) spin_lock_irqsave((f)->lock, flags)
#define dma_fence_unlock_irqrestore(f, flags) spin_unlock_irqrestore((f)->lock, flags)
static inline const char *dma_fence_driver_name(struct dma_fence *f) { return f->ops->get_driver_name ? f->ops->get_driver_name(f) : "fence"; }
static inline const char *dma_fence_timeline_name(struct dma_fence *f) { return f->ops->get_timeline_name ? f->ops->get_timeline_name(f) : "timeline"; }

struct dma_fence_array
{
    struct dma_fence base;
    spinlock_t lock;
    unsigned int num_fences;
    atomic_t num_pending;
    struct dma_fence **fences;
    void *callbacks;
};
extern const struct dma_fence_ops dma_fence_array_ops;
static inline bool dma_fence_is_array(struct dma_fence *fence) { return fence->ops == &dma_fence_array_ops; }
static inline struct dma_fence_array *to_dma_fence_array(struct dma_fence *fence)
{
    return fence && dma_fence_is_array(fence) ? container_of(fence, struct dma_fence_array, base) : NULL;
}
struct dma_fence_array *dma_fence_array_create(int num_fences, struct dma_fence **fences, u64 context, unsigned int seqno,
                                               bool signal_on_any);
struct dma_fence *dma_fence_array_first(struct dma_fence *head);
struct dma_fence *dma_fence_array_next(struct dma_fence *head, unsigned int index);

struct dma_fence_chain
{
    struct dma_fence base;
    struct dma_fence *prev;
    u64 prev_seqno;
    struct dma_fence *fence;
    struct dma_fence_cb cb;
    struct work_struct work;
    spinlock_t lock;
};
extern const struct dma_fence_ops dma_fence_chain_ops;
static inline bool dma_fence_is_chain(struct dma_fence *fence) { return fence->ops == &dma_fence_chain_ops; }
static inline bool dma_fence_is_container(struct dma_fence *fence) { return dma_fence_is_array(fence) || dma_fence_is_chain(fence); }
static inline struct dma_fence_chain *to_dma_fence_chain(struct dma_fence *fence)
{
    return fence && dma_fence_is_chain(fence) ? container_of(fence, struct dma_fence_chain, base) : NULL;
}
static inline struct dma_fence *dma_fence_chain_contained(struct dma_fence *fence)
{
    struct dma_fence_chain *chain = to_dma_fence_chain(fence);
    return chain ? chain->fence : fence;
}
static inline struct dma_fence_chain *dma_fence_chain_alloc(void) { return kmalloc(sizeof(struct dma_fence_chain), GFP_KERNEL); }
static inline void dma_fence_chain_free(struct dma_fence_chain *chain) { kfree(chain); }
struct dma_fence *dma_fence_chain_walk(struct dma_fence *fence);
int dma_fence_chain_find_seqno(struct dma_fence **pfence, u64 seqno);
void dma_fence_chain_init(struct dma_fence_chain *chain, struct dma_fence *prev, struct dma_fence *fence, u64 seqno);
#define dma_fence_chain_for_each(iter, head) \
    for (iter = dma_fence_get(head); iter; iter = dma_fence_chain_walk(iter))

struct dma_fence_unwrap
{
    struct dma_fence *chain;
    struct dma_fence *array;
    unsigned int index;
};
struct dma_fence *dma_fence_unwrap_first(struct dma_fence *head, struct dma_fence_unwrap *cursor);
struct dma_fence *dma_fence_unwrap_next(struct dma_fence_unwrap *cursor);
#define dma_fence_unwrap_for_each(fence, cursor, head) \
    for (fence = dma_fence_unwrap_first(head, cursor); fence; fence = dma_fence_unwrap_next(cursor))
struct dma_fence *__dma_fence_unwrap_merge(unsigned int num_fences, struct dma_fence **fences, struct dma_fence_unwrap *cursors);
#define dma_fence_unwrap_merge(...) ({ \
    struct dma_fence *__lc_f[] = { __VA_ARGS__ }; \
    struct dma_fence_unwrap __lc_c[ARRAY_SIZE(__lc_f)]; \
    __dma_fence_unwrap_merge(ARRAY_SIZE(__lc_f), __lc_f, __lc_c); })

struct ww_class { int dummy; };
struct ww_acquire_ctx
{
    struct ww_class *ww_class;
    unsigned int acquired;
};
struct ww_mutex
{
    struct mutex base;
    struct ww_acquire_ctx *ctx;
};
#define DEFINE_WW_CLASS(name) struct ww_class name = { 0 }
#define DEFINE_WD_CLASS(name) struct ww_class name = { 0 }
static inline void ww_mutex_init(struct ww_mutex *lock, struct ww_class *ww_class)
{
    (void)ww_class;
    lc_mutex_init(&lock->base);
    lock->ctx = NULL;
}
void ww_acquire_init(struct ww_acquire_ctx *ctx, struct ww_class *ww_class);
void ww_acquire_fini(struct ww_acquire_ctx *ctx);
static inline void ww_acquire_done(struct ww_acquire_ctx *ctx) { (void)ctx; }
int ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx);
static inline int ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) { return ww_mutex_lock(lock, ctx); }
static inline void ww_mutex_lock_slow(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) { (void)ww_mutex_lock(lock, ctx); }
static inline int ww_mutex_lock_slow_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx) { return ww_mutex_lock(lock, ctx); }
int ww_mutex_trylock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx);
void ww_mutex_unlock(struct ww_mutex *lock);
static inline bool ww_mutex_is_locked(struct ww_mutex *lock) { return lc_mutex_is_locked(&lock->base); }
static inline void ww_mutex_destroy(struct ww_mutex *lock) { (void)lock; }

enum dma_resv_usage
{
    DMA_RESV_USAGE_KERNEL,
    DMA_RESV_USAGE_WRITE,
    DMA_RESV_USAGE_READ,
    DMA_RESV_USAGE_BOOKKEEP,
};
static inline enum dma_resv_usage dma_resv_usage_rw(bool write) { return write ? DMA_RESV_USAGE_READ : DMA_RESV_USAGE_WRITE; }
struct dma_resv_entry
{
    struct dma_fence *fence;
    enum dma_resv_usage usage;
};
struct dma_resv
{
    struct ww_mutex lock;
    spinlock_t lc_entries_lock;
    struct dma_resv_entry *entries;
    unsigned int count;
    unsigned int reserved;
    unsigned int capacity;
};
extern struct ww_class reservation_ww_class;
void dma_resv_init(struct dma_resv *obj);
void dma_resv_fini(struct dma_resv *obj);
static inline int dma_resv_lock(struct dma_resv *obj, struct ww_acquire_ctx *ctx) { return ww_mutex_lock(&obj->lock, ctx); }
static inline int dma_resv_lock_interruptible(struct dma_resv *obj, struct ww_acquire_ctx *ctx) { return ww_mutex_lock(&obj->lock, ctx); }
static inline void dma_resv_lock_slow(struct dma_resv *obj, struct ww_acquire_ctx *ctx) { (void)ww_mutex_lock(&obj->lock, ctx); }
static inline int dma_resv_lock_slow_interruptible(struct dma_resv *obj, struct ww_acquire_ctx *ctx) { return ww_mutex_lock(&obj->lock, ctx); }
static inline bool dma_resv_trylock(struct dma_resv *obj) { return ww_mutex_trylock(&obj->lock, NULL); }
static inline void dma_resv_unlock(struct dma_resv *obj) { ww_mutex_unlock(&obj->lock); }
static inline bool dma_resv_is_locked(struct dma_resv *obj) { return ww_mutex_is_locked(&obj->lock); }
static inline struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *obj) { return READ_ONCE(obj->lock.ctx); }
#define dma_resv_held(obj) dma_resv_is_locked(obj)
#define dma_resv_assert_held(obj) do { (void)(obj); } while (0)
int dma_resv_reserve_fences(struct dma_resv *obj, unsigned int num_fences);
void dma_resv_add_fence(struct dma_resv *obj, struct dma_fence *fence, enum dma_resv_usage usage);
bool dma_resv_test_signaled(struct dma_resv *obj, enum dma_resv_usage usage);
long dma_resv_wait_timeout(struct dma_resv *obj, enum dma_resv_usage usage, bool intr, unsigned long timeout);
int dma_resv_get_singleton(struct dma_resv *obj, enum dma_resv_usage usage, struct dma_fence **fence);
struct dma_resv_iter
{
    struct dma_resv *obj;
    enum dma_resv_usage usage;
    struct dma_fence *fence;
    enum dma_resv_usage fence_usage;
    unsigned int index;
    bool is_restarted;
};
static inline void dma_resv_iter_begin(struct dma_resv_iter *cursor, struct dma_resv *obj, enum dma_resv_usage usage)
{
    cursor->obj = obj;
    cursor->usage = usage;
    cursor->fence = NULL;
    cursor->index = 0;
    cursor->is_restarted = true;
}
static inline void dma_resv_iter_end(struct dma_resv_iter *cursor) { (void)cursor; }
static inline enum dma_resv_usage dma_resv_iter_usage(struct dma_resv_iter *cursor) { return cursor->fence_usage; }
static inline bool dma_resv_iter_is_restarted(struct dma_resv_iter *cursor) { return cursor->is_restarted; }
struct dma_fence *dma_resv_iter_first(struct dma_resv_iter *cursor);
struct dma_fence *dma_resv_iter_next(struct dma_resv_iter *cursor);
#define dma_resv_for_each_fence(cursor, obj, usage, fence) \
    for (dma_resv_iter_begin(cursor, obj, usage), fence = dma_resv_iter_first(cursor); fence; fence = dma_resv_iter_next(cursor))

struct inode;
struct file_operations;
struct file
{
    void *private_data;
    const struct file_operations *f_op;
};
struct poll_table_struct;
struct vm_area_struct
{
    uintptr_t vm_start;
    uintptr_t vm_end;
    unsigned long vm_pgoff;
    unsigned long vm_flags;
    pgprot_t vm_page_prot;
    void *vm_private_data;
    const void *vm_ops;
    struct file *vm_file;
    void *lc_cookie;
};
struct file_operations
{
    void *owner;
    int (*open)(struct inode *inode, struct file *file);
    int (*release)(struct inode *inode, struct file *file);
    ssize_t (*read)(struct file *file, char __user *buf, size_t count, loff_t *pos);
    loff_t (*llseek)(struct file *file, loff_t offset, int whence);
    long (*unlocked_ioctl)(struct file *file, unsigned int cmd, unsigned long arg);
    long (*compat_ioctl)(struct file *file, unsigned int cmd, unsigned long arg);
    int (*mmap)(struct file *file, struct vm_area_struct *vma);
    unsigned int (*poll)(struct file *file, struct poll_table_struct *wait);
    ssize_t (*write)(struct file *file, const char __user *buf, size_t count, loff_t *pos);
};
#define O_CLOEXEC 02000000
#define O_RDWR 02
static inline int get_unused_fd_flags(unsigned int flags) { (void)flags; return -EMFILE; }
static inline void put_unused_fd(unsigned int fd) { (void)fd; }
static inline void fd_install(unsigned int fd, struct file *file) { (void)fd; (void)file; }
static inline struct file *fget(unsigned int fd) { (void)fd; return NULL; }
static inline void fput(struct file *file) { (void)file; }
static inline struct file *anon_inode_getfile(const char *name, const struct file_operations *fops, void *priv, int flags)
{
    (void)name;
    (void)fops;
    (void)priv;
    (void)flags;
    return ERR_PTR(-ENOSYS);
}
struct fd { struct file *file; };
#define fd_file(f) ((f).file)
#define fd_empty(f) (!(f).file)
DEFINE_CLASS(fd, struct fd, (void)_T, ((struct fd){ fget(fd) }), int fd)
struct sync_file { struct file *file; };
static inline struct sync_file *sync_file_create(struct dma_fence *fence) { (void)fence; return NULL; }
static inline struct dma_fence *sync_file_get_fence(int fd) { (void)fd; return NULL; }
struct eventfd_ctx;
static inline struct eventfd_ctx *eventfd_ctx_fdget(int fd) { (void)fd; return ERR_PTR(-ENOSYS); }
static inline void eventfd_signal(struct eventfd_ctx *ctx) { (void)ctx; }
static inline void eventfd_ctx_put(struct eventfd_ctx *ctx) { (void)ctx; }
struct dma_buf;
struct dma_buf_attachment;
