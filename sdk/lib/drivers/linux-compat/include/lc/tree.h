/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux red-black tree, interval tree and xarray interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct rb_node
{
    uintptr_t __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));
struct rb_root { struct rb_node *rb_node; };
struct rb_root_cached
{
    struct rb_root rb_root;
    struct rb_node *rb_leftmost;
};
struct lc_rb_augment
{
    void (*propagate)(struct rb_node *node, struct rb_node *stop);
    void (*copy)(struct rb_node *old, struct rb_node *new_node);
    void (*rotate)(struct rb_node *old, struct rb_node *new_node);
};

#define RB_ROOT (struct rb_root){ NULL }
#define RB_ROOT_CACHED (struct rb_root_cached){ { NULL }, NULL }
#define rb_entry(ptr, type, member) container_of(ptr, type, member)
#define rb_entry_safe(ptr, type, member) ({ typeof(ptr) __lc_p = (ptr); __lc_p ? rb_entry(__lc_p, type, member) : NULL; })
#define rb_parent(r) ((struct rb_node *)((r)->__rb_parent_color & ~(uintptr_t)3))
#define RB_EMPTY_ROOT(root) (READ_ONCE((root)->rb_node) == NULL)
#define RB_EMPTY_NODE(node) ((node)->__rb_parent_color == (uintptr_t)(node))
#define RB_CLEAR_NODE(node) ((node)->__rb_parent_color = (uintptr_t)(node))
#define rb_first_cached(root) ((root)->rb_leftmost)

static inline void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **link)
{
    node->__rb_parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;
    *link = node;
}
void lc_rb_insert(struct rb_node *node, struct rb_root *root, const struct lc_rb_augment *aug);
void lc_rb_erase(struct rb_node *node, struct rb_root *root, const struct lc_rb_augment *aug);
struct rb_node *rb_first(const struct rb_root *root);
struct rb_node *rb_last(const struct rb_root *root);
struct rb_node *rb_next(const struct rb_node *node);
struct rb_node *rb_prev(const struct rb_node *node);
void rb_replace_node(struct rb_node *victim, struct rb_node *new_node, struct rb_root *root);
static inline void rb_insert_color(struct rb_node *node, struct rb_root *root) { lc_rb_insert(node, root, NULL); }
static inline void rb_erase(struct rb_node *node, struct rb_root *root) { lc_rb_erase(node, root, NULL); }
static inline void rb_insert_color_cached(struct rb_node *node, struct rb_root_cached *root, bool leftmost)
{
    if (leftmost)
        root->rb_leftmost = node;
    lc_rb_insert(node, &root->rb_root, NULL);
}
static inline struct rb_node *rb_erase_cached(struct rb_node *node, struct rb_root_cached *root)
{
    struct rb_node *leftmost = NULL;
    if (root->rb_leftmost == node)
        leftmost = root->rb_leftmost = rb_next(node);
    lc_rb_erase(node, &root->rb_root, NULL);
    return leftmost;
}
static inline struct rb_node *rb_add_cached(struct rb_node *node, struct rb_root_cached *tree,
                                            bool (*less)(struct rb_node *, const struct rb_node *))
{
    struct rb_node **link = &tree->rb_root.rb_node;
    struct rb_node *parent = NULL;
    bool leftmost = true;
    while (*link)
    {
        parent = *link;
        if (less(node, parent))
        {
            link = &parent->rb_left;
        }
        else
        {
            link = &parent->rb_right;
            leftmost = false;
        }
    }
    rb_link_node(node, parent, link);
    rb_insert_color_cached(node, tree, leftmost);
    return leftmost ? node : NULL;
}
static inline void rb_add(struct rb_node *node, struct rb_root *tree, bool (*less)(struct rb_node *, const struct rb_node *))
{
    struct rb_node **link = &tree->rb_node;
    struct rb_node *parent = NULL;
    while (*link)
    {
        parent = *link;
        link = less(node, parent) ? &parent->rb_left : &parent->rb_right;
    }
    rb_link_node(node, parent, link);
    rb_insert_color(node, tree);
}
static inline struct rb_node *rb_find(const void *key, const struct rb_root *tree, int (*cmp)(const void *key, const struct rb_node *))
{
    struct rb_node *node = tree->rb_node;
    while (node)
    {
        int c = cmp(key, node);
        if (c < 0)
            node = node->rb_left;
        else if (c > 0)
            node = node->rb_right;
        else
            return node;
    }
    return NULL;
}
#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
    for (struct rb_node *__lc_n = lc_rb_first_postorder(root), *__lc_nx; \
         __lc_n && ((pos) = rb_entry(__lc_n, typeof(*(pos)), field), __lc_nx = lc_rb_next_postorder(__lc_n), \
                    (n) = __lc_nx ? rb_entry(__lc_nx, typeof(*(pos)), field) : NULL, 1); __lc_n = __lc_nx)
struct rb_node *lc_rb_first_postorder(const struct rb_root *root);
struct rb_node *lc_rb_next_postorder(const struct rb_node *node);

#define INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITSTART, ITLAST, ITSTATIC, ITPREFIX) \
static inline ITTYPE ITPREFIX##_lc_subtree(const ITSTRUCT *node)                                 \
{                                                                                                 \
    ITTYPE max = ITLAST(node), sub;                                                               \
    if (node->ITRB.rb_left) {                                                                     \
        sub = rb_entry(node->ITRB.rb_left, ITSTRUCT, ITRB)->ITSUBTREE;                            \
        if (sub > max)                                                                            \
            max = sub;                                                                            \
    }                                                                                             \
    if (node->ITRB.rb_right) {                                                                    \
        sub = rb_entry(node->ITRB.rb_right, ITSTRUCT, ITRB)->ITSUBTREE;                           \
        if (sub > max)                                                                            \
            max = sub;                                                                            \
    }                                                                                             \
    return max;                                                                                   \
}                                                                                                 \
static void ITPREFIX##_lc_propagate(struct rb_node *rb, struct rb_node *stop)                    \
{                                                                                                 \
    while (rb != stop) {                                                                          \
        ITSTRUCT *node = rb_entry(rb, ITSTRUCT, ITRB);                                            \
        node->ITSUBTREE = ITPREFIX##_lc_subtree(node);                                            \
        rb = rb_parent(&node->ITRB);                                                              \
    }                                                                                             \
}                                                                                                 \
static void ITPREFIX##_lc_copy(struct rb_node *old_rb, struct rb_node *new_rb)                   \
{                                                                                                 \
    rb_entry(new_rb, ITSTRUCT, ITRB)->ITSUBTREE = rb_entry(old_rb, ITSTRUCT, ITRB)->ITSUBTREE;   \
}                                                                                                 \
static void ITPREFIX##_lc_rotate(struct rb_node *old_rb, struct rb_node *new_rb)                 \
{                                                                                                 \
    ITSTRUCT *old_node = rb_entry(old_rb, ITSTRUCT, ITRB);                                        \
    ITSTRUCT *new_node = rb_entry(new_rb, ITSTRUCT, ITRB);                                        \
    new_node->ITSUBTREE = old_node->ITSUBTREE;                                                    \
    old_node->ITSUBTREE = ITPREFIX##_lc_subtree(old_node);                                        \
}                                                                                                 \
static const struct lc_rb_augment ITPREFIX##_lc_augment = {                                      \
    ITPREFIX##_lc_propagate, ITPREFIX##_lc_copy, ITPREFIX##_lc_rotate                             \
};                                                                                                \
ITSTATIC void ITPREFIX##_insert(ITSTRUCT *node, struct rb_root_cached *root)                     \
{                                                                                                 \
    struct rb_node **link = &root->rb_root.rb_node, *rb_parent_node = NULL;                       \
    ITTYPE start = ITSTART(node), last = ITLAST(node);                                            \
    ITSTRUCT *parent;                                                                             \
    bool leftmost = true;                                                                         \
    while (*link) {                                                                               \
        rb_parent_node = *link;                                                                   \
        parent = rb_entry(rb_parent_node, ITSTRUCT, ITRB);                                        \
        if (parent->ITSUBTREE < last)                                                             \
            parent->ITSUBTREE = last;                                                             \
        if (start < ITSTART(parent)) {                                                            \
            link = &parent->ITRB.rb_left;                                                         \
        } else {                                                                                  \
            link = &parent->ITRB.rb_right;                                                        \
            leftmost = false;                                                                     \
        }                                                                                         \
    }                                                                                             \
    node->ITSUBTREE = last;                                                                       \
    rb_link_node(&node->ITRB, rb_parent_node, link);                                              \
    if (leftmost)                                                                                 \
        root->rb_leftmost = &node->ITRB;                                                          \
    lc_rb_insert(&node->ITRB, &root->rb_root, &ITPREFIX##_lc_augment);                            \
}                                                                                                 \
ITSTATIC void ITPREFIX##_remove(ITSTRUCT *node, struct rb_root_cached *root)                     \
{                                                                                                 \
    if (root->rb_leftmost == &node->ITRB)                                                         \
        root->rb_leftmost = rb_next(&node->ITRB);                                                 \
    lc_rb_erase(&node->ITRB, &root->rb_root, &ITPREFIX##_lc_augment);                             \
}                                                                                                 \
static ITSTRUCT *ITPREFIX##_lc_subtree_search(ITSTRUCT *node, ITTYPE start, ITTYPE last)         \
{                                                                                                 \
    for (;;) {                                                                                    \
        if (node->ITRB.rb_left) {                                                                 \
            ITSTRUCT *left = rb_entry(node->ITRB.rb_left, ITSTRUCT, ITRB);                        \
            if (start <= left->ITSUBTREE) {                                                       \
                node = left;                                                                      \
                continue;                                                                         \
            }                                                                                     \
        }                                                                                         \
        if (ITSTART(node) <= last) {                                                              \
            if (start <= ITLAST(node))                                                            \
                return node;                                                                      \
            if (node->ITRB.rb_right) {                                                            \
                node = rb_entry(node->ITRB.rb_right, ITSTRUCT, ITRB);                             \
                if (start <= node->ITSUBTREE)                                                     \
                    continue;                                                                     \
            }                                                                                     \
        }                                                                                         \
        return NULL;                                                                              \
    }                                                                                             \
}                                                                                                 \
ITSTATIC ITSTRUCT *ITPREFIX##_iter_first(struct rb_root_cached *root, ITTYPE start, ITTYPE last) \
{                                                                                                 \
    ITSTRUCT *node, *leftmost;                                                                    \
    if (!root->rb_root.rb_node)                                                                   \
        return NULL;                                                                              \
    node = rb_entry(root->rb_root.rb_node, ITSTRUCT, ITRB);                                       \
    if (node->ITSUBTREE < start)                                                                  \
        return NULL;                                                                              \
    leftmost = rb_entry(root->rb_leftmost, ITSTRUCT, ITRB);                                       \
    if (ITSTART(leftmost) > last)                                                                 \
        return NULL;                                                                              \
    return ITPREFIX##_lc_subtree_search(node, start, last);                                       \
}                                                                                                 \
ITSTATIC ITSTRUCT *ITPREFIX##_iter_next(ITSTRUCT *node, ITTYPE start, ITTYPE last)               \
{                                                                                                 \
    struct rb_node *rb = node->ITRB.rb_right, *prev;                                              \
    for (;;) {                                                                                    \
        if (rb) {                                                                                 \
            ITSTRUCT *right = rb_entry(rb, ITSTRUCT, ITRB);                                       \
            if (start <= right->ITSUBTREE)                                                        \
                return ITPREFIX##_lc_subtree_search(right, start, last);                          \
        }                                                                                         \
        do {                                                                                      \
            rb = rb_parent(&node->ITRB);                                                          \
            if (!rb)                                                                              \
                return NULL;                                                                      \
            prev = &node->ITRB;                                                                   \
            node = rb_entry(rb, ITSTRUCT, ITRB);                                                  \
            rb = node->ITRB.rb_right;                                                             \
        } while (prev == rb);                                                                     \
        if (last < ITSTART(node))                                                                 \
            return NULL;                                                                          \
        if (start <= ITLAST(node))                                                                \
            return node;                                                                          \
    }                                                                                             \
}

#define XA_FLAGS_ALLOC (1U << 0)
#define XA_FLAGS_ALLOC1 (1U << 1)
#define XA_FLAGS_LOCK_IRQ 0
#define XA_PRESENT ((xa_mark_t)8U)
typedef unsigned int xa_mark_t;
struct xa_limit { u32 max; u32 min; };
#define XA_LIMIT(lo, hi) (struct xa_limit){ .min = (lo), .max = (hi) }
#define xa_limit_32b XA_LIMIT(0, UINT_MAX)
#define xa_limit_31b XA_LIMIT(0, INT_MAX)
#define xa_limit_16b XA_LIMIT(0, U16_MAX)
struct lc_xa_entry
{
    unsigned long index;
    void *entry;
};
struct xarray
{
    spinlock_t xa_lock;
    spinlock_t lc_lock;
    unsigned int xa_flags;
    struct lc_xa_entry *entries;
    unsigned int count;
    unsigned int capacity;
    u32 next;
};
#define XARRAY_INIT(name, flags) { __SPIN_LOCK_UNLOCKED(name), __SPIN_LOCK_UNLOCKED(name), flags, NULL, 0, 0, 0 }
#define DEFINE_XARRAY_FLAGS(name, flags) struct xarray name = XARRAY_INIT(name, flags)
#define DEFINE_XARRAY(name) DEFINE_XARRAY_FLAGS(name, 0)
#define DEFINE_XARRAY_ALLOC(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC)
#define DEFINE_XARRAY_ALLOC1(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC1)
void xa_init_flags(struct xarray *xa, unsigned int flags);
static inline void xa_init(struct xarray *xa) { xa_init_flags(xa, 0); }
void xa_destroy(struct xarray *xa);
void *xa_load(struct xarray *xa, unsigned long index);
void *__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp);
void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp);
void *__xa_erase(struct xarray *xa, unsigned long index);
void *xa_erase(struct xarray *xa, unsigned long index);
int __xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, gfp_t gfp);
int xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, gfp_t gfp);
int xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, u32 *next, gfp_t gfp);
int xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp);
void *xa_find(struct xarray *xa, unsigned long *index, unsigned long max, xa_mark_t filter);
void *xa_find_after(struct xarray *xa, unsigned long *index, unsigned long max, xa_mark_t filter);
static inline bool xa_empty(struct xarray *xa) { return READ_ONCE(xa->count) == 0; }
static inline bool xa_is_err(const void *entry) { return IS_ERR(entry); }
static inline int xa_err(void *entry) { return IS_ERR(entry) ? (int)PTR_ERR(entry) : 0; }
#define xa_lock(xa) spin_lock(&(xa)->xa_lock)
#define xa_unlock(xa) spin_unlock(&(xa)->xa_lock)
#define xa_lock_irq(xa) spin_lock(&(xa)->xa_lock)
#define xa_unlock_irq(xa) spin_unlock(&(xa)->xa_lock)
#define xa_lock_irqsave(xa, f) spin_lock_irqsave(&(xa)->xa_lock, f)
#define xa_unlock_irqrestore(xa, f) spin_unlock_irqrestore(&(xa)->xa_lock, f)
#define xa_for_each_range(xa, index, entry, start, last) \
    for (index = start, entry = xa_find(xa, &index, last, XA_PRESENT); entry; entry = xa_find_after(xa, &index, last, XA_PRESENT))
#define xa_for_each_start(xa, index, entry, start) xa_for_each_range(xa, index, entry, start, ULONG_MAX)
#define xa_for_each(xa, index, entry) xa_for_each_start(xa, index, entry, 0)
