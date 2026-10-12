/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Red-black tree with augmentation hooks, and an index map with the xarray interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

#define LC_RB_BLACK ((uintptr_t)1)

static inline bool lc_rb_is_black(const struct rb_node *node) { return !node || (node->__rb_parent_color & LC_RB_BLACK); }
static inline bool lc_rb_is_red(const struct rb_node *node) { return !lc_rb_is_black(node); }
static inline void lc_rb_set_black(struct rb_node *node) { node->__rb_parent_color |= LC_RB_BLACK; }
static inline void lc_rb_set_red(struct rb_node *node) { node->__rb_parent_color &= ~LC_RB_BLACK; }
static inline uintptr_t lc_rb_color(const struct rb_node *node) { return node->__rb_parent_color & LC_RB_BLACK; }
static inline void lc_rb_set_parent(struct rb_node *node, struct rb_node *parent)
{
    node->__rb_parent_color = (uintptr_t)parent | lc_rb_color(node);
}
static inline void lc_rb_set_parent_color(struct rb_node *node, struct rb_node *parent, uintptr_t color)
{
    node->__rb_parent_color = (uintptr_t)parent | color;
}

static void lc_rb_change_child(struct rb_node *old_child, struct rb_node *new_child, struct rb_node *parent, struct rb_root *root)
{
    if (!parent)
        root->rb_node = new_child;
    else if (parent->rb_left == old_child)
        parent->rb_left = new_child;
    else
        parent->rb_right = new_child;
}

static void lc_rb_rotate_left(struct rb_node *x, struct rb_root *root, const struct lc_rb_augment *aug)
{
    struct rb_node *y = x->rb_right;
    struct rb_node *parent = rb_parent(x);

    x->rb_right = y->rb_left;
    if (y->rb_left)
        lc_rb_set_parent(y->rb_left, x);
    lc_rb_set_parent(y, parent);
    lc_rb_change_child(x, y, parent, root);
    y->rb_left = x;
    lc_rb_set_parent(x, y);
    if (aug)
        aug->rotate(x, y);
}

static void lc_rb_rotate_right(struct rb_node *x, struct rb_root *root, const struct lc_rb_augment *aug)
{
    struct rb_node *y = x->rb_left;
    struct rb_node *parent = rb_parent(x);

    x->rb_left = y->rb_right;
    if (y->rb_right)
        lc_rb_set_parent(y->rb_right, x);
    lc_rb_set_parent(y, parent);
    lc_rb_change_child(x, y, parent, root);
    y->rb_right = x;
    lc_rb_set_parent(x, y);
    if (aug)
        aug->rotate(x, y);
}

void lc_rb_insert(struct rb_node *node, struct rb_root *root, const struct lc_rb_augment *aug)
{
    struct rb_node *parent, *grandparent, *uncle;

    lc_rb_set_red(node);
    while ((parent = rb_parent(node)) && lc_rb_is_red(parent))
    {
        grandparent = rb_parent(parent);
        if (parent == grandparent->rb_left)
        {
            uncle = grandparent->rb_right;
            if (lc_rb_is_red(uncle))
            {
                lc_rb_set_black(parent);
                lc_rb_set_black(uncle);
                lc_rb_set_red(grandparent);
                node = grandparent;
                continue;
            }
            if (node == parent->rb_right)
            {
                lc_rb_rotate_left(parent, root, aug);
                node = parent;
                parent = rb_parent(node);
            }
            lc_rb_set_black(parent);
            lc_rb_set_red(grandparent);
            lc_rb_rotate_right(grandparent, root, aug);
        }
        else
        {
            uncle = grandparent->rb_left;
            if (lc_rb_is_red(uncle))
            {
                lc_rb_set_black(parent);
                lc_rb_set_black(uncle);
                lc_rb_set_red(grandparent);
                node = grandparent;
                continue;
            }
            if (node == parent->rb_left)
            {
                lc_rb_rotate_right(parent, root, aug);
                node = parent;
                parent = rb_parent(node);
            }
            lc_rb_set_black(parent);
            lc_rb_set_red(grandparent);
            lc_rb_rotate_left(grandparent, root, aug);
        }
    }
    lc_rb_set_black(root->rb_node);
}

static void lc_rb_erase_fixup(struct rb_node *node, struct rb_node *parent, struct rb_root *root, const struct lc_rb_augment *aug)
{
    struct rb_node *sibling;

    while (node != root->rb_node && lc_rb_is_black(node))
    {
        if (node == parent->rb_left)
        {
            sibling = parent->rb_right;
            if (lc_rb_is_red(sibling))
            {
                lc_rb_set_black(sibling);
                lc_rb_set_red(parent);
                lc_rb_rotate_left(parent, root, aug);
                sibling = parent->rb_right;
            }
            if (lc_rb_is_black(sibling->rb_left) && lc_rb_is_black(sibling->rb_right))
            {
                lc_rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
                continue;
            }
            if (lc_rb_is_black(sibling->rb_right))
            {
                lc_rb_set_black(sibling->rb_left);
                lc_rb_set_red(sibling);
                lc_rb_rotate_right(sibling, root, aug);
                sibling = parent->rb_right;
            }
            lc_rb_set_parent_color(sibling, rb_parent(sibling), lc_rb_color(parent));
            lc_rb_set_black(parent);
            lc_rb_set_black(sibling->rb_right);
            lc_rb_rotate_left(parent, root, aug);
            node = root->rb_node;
            break;
        }
        else
        {
            sibling = parent->rb_left;
            if (lc_rb_is_red(sibling))
            {
                lc_rb_set_black(sibling);
                lc_rb_set_red(parent);
                lc_rb_rotate_right(parent, root, aug);
                sibling = parent->rb_left;
            }
            if (lc_rb_is_black(sibling->rb_left) && lc_rb_is_black(sibling->rb_right))
            {
                lc_rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
                continue;
            }
            if (lc_rb_is_black(sibling->rb_left))
            {
                lc_rb_set_black(sibling->rb_right);
                lc_rb_set_red(sibling);
                lc_rb_rotate_left(sibling, root, aug);
                sibling = parent->rb_left;
            }
            lc_rb_set_parent_color(sibling, rb_parent(sibling), lc_rb_color(parent));
            lc_rb_set_black(parent);
            lc_rb_set_black(sibling->rb_left);
            lc_rb_rotate_right(parent, root, aug);
            node = root->rb_node;
            break;
        }
    }
    if (node)
        lc_rb_set_black(node);
}

void lc_rb_erase(struct rb_node *node, struct rb_root *root, const struct lc_rb_augment *aug)
{
    struct rb_node *child, *parent, *successor;
    bool removed_black;

    if (!node->rb_left || !node->rb_right)
    {
        child = node->rb_left ? node->rb_left : node->rb_right;
        parent = rb_parent(node);
        removed_black = lc_rb_is_black(node);
        lc_rb_change_child(node, child, parent, root);
        if (child)
            lc_rb_set_parent(child, parent);
    }
    else
    {
        successor = node->rb_right;
        while (successor->rb_left)
            successor = successor->rb_left;
        removed_black = lc_rb_is_black(successor);
        child = successor->rb_right;
        if (successor == node->rb_right)
        {
            parent = successor;
        }
        else
        {
            parent = rb_parent(successor);
            parent->rb_left = child;
            if (child)
                lc_rb_set_parent(child, parent);
            successor->rb_right = node->rb_right;
            lc_rb_set_parent(node->rb_right, successor);
        }
        successor->rb_left = node->rb_left;
        lc_rb_set_parent(node->rb_left, successor);
        lc_rb_change_child(node, successor, rb_parent(node), root);
        successor->__rb_parent_color = node->__rb_parent_color;
        if (aug)
            aug->copy(node, successor);
    }

    if (aug && parent)
        aug->propagate(parent, NULL);
    if (removed_black)
        lc_rb_erase_fixup(child, parent, root, aug);
}

struct rb_node *rb_first(const struct rb_root *root)
{
    struct rb_node *node = root->rb_node;

    if (!node)
        return NULL;
    while (node->rb_left)
        node = node->rb_left;
    return node;
}

struct rb_node *rb_last(const struct rb_root *root)
{
    struct rb_node *node = root->rb_node;

    if (!node)
        return NULL;
    while (node->rb_right)
        node = node->rb_right;
    return node;
}

struct rb_node *rb_next(const struct rb_node *node)
{
    struct rb_node *parent;

    if (RB_EMPTY_NODE(node))
        return NULL;
    if (node->rb_right)
    {
        node = node->rb_right;
        while (node->rb_left)
            node = node->rb_left;
        return (struct rb_node *)node;
    }
    while ((parent = rb_parent(node)) && node == parent->rb_right)
        node = parent;
    return parent;
}

struct rb_node *rb_prev(const struct rb_node *node)
{
    struct rb_node *parent;

    if (RB_EMPTY_NODE(node))
        return NULL;
    if (node->rb_left)
    {
        node = node->rb_left;
        while (node->rb_right)
            node = node->rb_right;
        return (struct rb_node *)node;
    }
    while ((parent = rb_parent(node)) && node == parent->rb_left)
        node = parent;
    return parent;
}

void rb_replace_node(struct rb_node *victim, struct rb_node *new_node, struct rb_root *root)
{
    struct rb_node *parent = rb_parent(victim);

    *new_node = *victim;
    if (victim->rb_left)
        lc_rb_set_parent(victim->rb_left, new_node);
    if (victim->rb_right)
        lc_rb_set_parent(victim->rb_right, new_node);
    lc_rb_change_child(victim, new_node, parent, root);
}

static struct rb_node *lc_rb_left_deepest(const struct rb_node *node)
{
    for (;;)
    {
        if (node->rb_left)
            node = node->rb_left;
        else if (node->rb_right)
            node = node->rb_right;
        else
            return (struct rb_node *)node;
    }
}

struct rb_node *lc_rb_first_postorder(const struct rb_root *root)
{
    return root->rb_node ? lc_rb_left_deepest(root->rb_node) : NULL;
}

struct rb_node *lc_rb_next_postorder(const struct rb_node *node)
{
    const struct rb_node *parent;

    if (!node)
        return NULL;
    parent = rb_parent(node);
    if (parent && node == parent->rb_left && parent->rb_right)
        return lc_rb_left_deepest(parent->rb_right);
    return (struct rb_node *)parent;
}

static unsigned int lc_xa_lower_bound(struct xarray *xa, unsigned long index)
{
    unsigned int low = 0, high = xa->count;

    while (low < high)
    {
        unsigned int mid = low + (high - low) / 2;

        if (xa->entries[mid].index < index)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

static int lc_xa_insert_at(struct xarray *xa, unsigned int pos, unsigned long index, void *entry)
{
    if (xa->count == xa->capacity)
    {
        unsigned int capacity = xa->capacity ? xa->capacity * 2 : 16;
        struct lc_xa_entry *entries = kmalloc_array(capacity, sizeof(*entries), GFP_ATOMIC);

        if (!entries)
            return -ENOMEM;
        if (xa->count)
            memcpy(entries, xa->entries, xa->count * sizeof(*entries));
        kfree(xa->entries);
        xa->entries = entries;
        xa->capacity = capacity;
    }
    memmove(&xa->entries[pos + 1], &xa->entries[pos], (xa->count - pos) * sizeof(*xa->entries));
    xa->entries[pos].index = index;
    xa->entries[pos].entry = entry;
    xa->count++;
    return 0;
}

static void *lc_xa_remove_at(struct xarray *xa, unsigned int pos)
{
    void *entry = xa->entries[pos].entry;

    memmove(&xa->entries[pos], &xa->entries[pos + 1], (xa->count - pos - 1) * sizeof(*xa->entries));
    xa->count--;
    return entry;
}

void xa_init_flags(struct xarray *xa, unsigned int flags)
{
    spin_lock_init(&xa->xa_lock);
    spin_lock_init(&xa->lc_lock);
    xa->xa_flags = flags;
    xa->entries = NULL;
    xa->count = 0;
    xa->capacity = 0;
    xa->next = 0;
}

void xa_destroy(struct xarray *xa)
{
    spin_lock(&xa->lc_lock);
    kfree(xa->entries);
    xa->entries = NULL;
    xa->count = 0;
    xa->capacity = 0;
    spin_unlock(&xa->lc_lock);
}

void *xa_load(struct xarray *xa, unsigned long index)
{
    void *entry = NULL;
    unsigned int pos;

    spin_lock(&xa->lc_lock);
    pos = lc_xa_lower_bound(xa, index);
    if (pos < xa->count && xa->entries[pos].index == index)
        entry = xa->entries[pos].entry;
    spin_unlock(&xa->lc_lock);
    return entry;
}

void *__xa_erase(struct xarray *xa, unsigned long index)
{
    void *entry = NULL;
    unsigned int pos;

    spin_lock(&xa->lc_lock);
    pos = lc_xa_lower_bound(xa, index);
    if (pos < xa->count && xa->entries[pos].index == index)
        entry = lc_xa_remove_at(xa, pos);
    spin_unlock(&xa->lc_lock);
    return entry;
}

void *xa_erase(struct xarray *xa, unsigned long index)
{
    void *entry;

    xa_lock(xa);
    entry = __xa_erase(xa, index);
    xa_unlock(xa);
    return entry;
}

void *__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
    void *old = NULL;
    unsigned int pos;
    int err = 0;

    (void)gfp;
    if (!entry)
        return __xa_erase(xa, index);
    spin_lock(&xa->lc_lock);
    pos = lc_xa_lower_bound(xa, index);
    if (pos < xa->count && xa->entries[pos].index == index)
    {
        old = xa->entries[pos].entry;
        xa->entries[pos].entry = entry;
    }
    else
    {
        err = lc_xa_insert_at(xa, pos, index, entry);
    }
    spin_unlock(&xa->lc_lock);
    return err ? ERR_PTR(err) : old;
}

void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
    void *old;

    xa_lock(xa);
    old = __xa_store(xa, index, entry, gfp);
    xa_unlock(xa);
    return old;
}

int xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
    unsigned int pos;
    int err;

    (void)gfp;
    xa_lock(xa);
    spin_lock(&xa->lc_lock);
    pos = lc_xa_lower_bound(xa, index);
    if (pos < xa->count && xa->entries[pos].index == index)
        err = -EBUSY;
    else
        err = lc_xa_insert_at(xa, pos, index, entry);
    spin_unlock(&xa->lc_lock);
    xa_unlock(xa);
    return err;
}

static int lc_xa_alloc_from(struct xarray *xa, u32 *id, void *entry, u32 first, u32 last)
{
    unsigned int pos;
    u64 candidate = first;

    if (first > last)
        return -EBUSY;
    pos = lc_xa_lower_bound(xa, first);
    while (pos < xa->count && xa->entries[pos].index == candidate)
    {
        candidate++;
        pos++;
    }
    if (candidate > last)
        return -EBUSY;
    *id = (u32)candidate;
    return lc_xa_insert_at(xa, pos, (unsigned long)candidate, entry);
}

int __xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, gfp_t gfp)
{
    u32 first = limit.min;
    int err;

    (void)gfp;
    if ((xa->xa_flags & XA_FLAGS_ALLOC1) && first == 0)
        first = 1;
    spin_lock(&xa->lc_lock);
    err = lc_xa_alloc_from(xa, id, entry, first, limit.max);
    spin_unlock(&xa->lc_lock);
    return err;
}

int xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, gfp_t gfp)
{
    int err;

    xa_lock(xa);
    err = __xa_alloc(xa, id, entry, limit, gfp);
    xa_unlock(xa);
    return err;
}

int xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit, u32 *next, gfp_t gfp)
{
    u32 first = max(limit.min, *next);
    int err;

    (void)gfp;
    if ((xa->xa_flags & XA_FLAGS_ALLOC1) && first == 0)
        first = 1;
    xa_lock(xa);
    spin_lock(&xa->lc_lock);
    err = lc_xa_alloc_from(xa, id, entry, first, limit.max);
    if (err == -EBUSY && first > limit.min)
    {
        err = lc_xa_alloc_from(xa, id, entry, limit.min ? limit.min : ((xa->xa_flags & XA_FLAGS_ALLOC1) ? 1 : 0), limit.max);
        if (!err)
            err = 1;
    }
    if (err >= 0)
        *next = *id + 1;
    spin_unlock(&xa->lc_lock);
    xa_unlock(xa);
    return err;
}

void *xa_find(struct xarray *xa, unsigned long *index, unsigned long max, xa_mark_t filter)
{
    void *entry = NULL;
    unsigned int pos;

    (void)filter;
    spin_lock(&xa->lc_lock);
    pos = lc_xa_lower_bound(xa, *index);
    if (pos < xa->count && xa->entries[pos].index <= max)
    {
        *index = xa->entries[pos].index;
        entry = xa->entries[pos].entry;
    }
    spin_unlock(&xa->lc_lock);
    return entry;
}

void *xa_find_after(struct xarray *xa, unsigned long *index, unsigned long max, xa_mark_t filter)
{
    unsigned long next;
    void *entry;

    if (*index == ULONG_MAX)
        return NULL;
    next = *index + 1;
    if (next > max)
        return NULL;
    entry = xa_find(xa, &next, max, filter);
    if (entry)
        *index = next;
    return entry;
}
