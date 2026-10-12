/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux allocators, pages, mappings, scatter lists, user copies and strings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

#define LC_KMALLOC_HEADER 16

struct lc_vmap_record
{
    struct list_head node;
    void *address;
    void *cookie;
};

static u64 lc_page_limit = ~0ULL;
static DEFINE_SPINLOCK(lc_vmap_lock);
static LIST_HEAD(lc_vmap_records);

void lc_set_page_allocation_limit(u64 highest_address)
{
    lc_page_limit = highest_address;
}

u64 lc_get_page_allocation_limit(void)
{
    return lc_page_limit;
}

void *lc_kmalloc(size_t size, gfp_t flags)
{
    u8 *block;

    if (size > SIZE_MAX - LC_KMALLOC_HEADER)
        return NULL;
    block = lc_nt_alloc(size + LC_KMALLOC_HEADER, (flags & __GFP_ZERO) != 0);
    if (!block)
        return NULL;
    *(size_t *)block = size;
    return block + LC_KMALLOC_HEADER;
}

void lc_kfree(const void *ptr)
{
    if (ZERO_OR_NULL_PTR(ptr) || IS_ERR(ptr))
        return;
    lc_nt_free((u8 *)ptr - LC_KMALLOC_HEADER);
}

size_t lc_ksize(const void *ptr)
{
    if (ZERO_OR_NULL_PTR(ptr))
        return 0;
    return *(const size_t *)((const u8 *)ptr - LC_KMALLOC_HEADER);
}

void *lc_krealloc(const void *ptr, size_t size, gfp_t flags)
{
    size_t old_size;
    void *block;

    if (!size)
    {
        lc_kfree(ptr);
        return ZERO_SIZE_PTR;
    }
    if (!ptr || ptr == ZERO_SIZE_PTR)
        return lc_kmalloc(size, flags);
    old_size = lc_ksize(ptr);
    if (old_size >= size)
        return (void *)ptr;
    block = lc_kmalloc(size, flags & ~__GFP_ZERO);
    if (!block)
        return NULL;
    memcpy(block, ptr, old_size);
    if (flags & __GFP_ZERO)
        memset((u8 *)block + old_size, 0, size - old_size);
    lc_kfree(ptr);
    return block;
}

void *lc_kmemdup(const void *src, size_t len, gfp_t gfp)
{
    void *p = lc_kmalloc(len, gfp);

    if (p)
        memcpy(p, src, len);
    return p;
}

char *lc_kstrdup(const char *s, gfp_t gfp)
{
    if (!s)
        return NULL;
    return lc_kmemdup(s, strlen(s) + 1, gfp);
}

int lc_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char *scratch;
    size_t scratch_size;
    va_list copy;
    int r;

    va_copy(copy, args);
    r = lc_nt_vsnprintf(buf, size, fmt, copy);
    va_end(copy);
    if (r >= 0 && (size_t)r < size)
        return r;
    if (size)
        buf[size - 1] = 0;

    for (scratch_size = size < 256 ? 512 : size * 2; scratch_size <= 1024 * 1024; scratch_size *= 2)
    {
        scratch = lc_nt_alloc(scratch_size, 0);
        if (!scratch)
            break;
        va_copy(copy, args);
        r = lc_nt_vsnprintf(scratch, scratch_size, fmt, copy);
        va_end(copy);
        lc_nt_free(scratch);
        if (r >= 0 && (size_t)r < scratch_size)
            return r;
    }
    return (int)(size ? size - 1 : 0);
}

int lc_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = lc_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return r;
}

int lc_vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    int r;

    if (!size)
        return 0;
    r = lc_vsnprintf(buf, size, fmt, args);
    return (size_t)r < size ? r : (int)(size - 1);
}

int lc_scnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = lc_vscnprintf(buf, size, fmt, args);
    va_end(args);
    return r;
}

char *lc_kvasprintf(gfp_t gfp, const char *fmt, va_list args)
{
    va_list copy;
    char *buf;
    int len;

    va_copy(copy, args);
    len = lc_vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    buf = lc_kmalloc((size_t)len + 1, gfp);
    if (!buf)
        return NULL;
    lc_vsnprintf(buf, (size_t)len + 1, fmt, args);
    return buf;
}

char *lc_kasprintf(gfp_t gfp, const char *fmt, ...)
{
    va_list args;
    char *buf;

    va_start(args, fmt);
    buf = lc_kvasprintf(gfp, fmt, args);
    va_end(args);
    return buf;
}

int lc_kstrtou64(const char *s, unsigned int base, u64 *res)
{
    u64 value = 0;
    bool any = false;

    if (base == 0)
    {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        {
            base = 16;
            s += 2;
        }
        else if (s[0] == '0' && s[1])
        {
            base = 8;
            s++;
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
    }

    for (; *s && *s != '\n'; ++s)
    {
        unsigned int digit;

        if (*s >= '0' && *s <= '9')
            digit = (unsigned int)(*s - '0');
        else if (*s >= 'a' && *s <= 'z')
            digit = (unsigned int)(*s - 'a') + 10;
        else if (*s >= 'A' && *s <= 'Z')
            digit = (unsigned int)(*s - 'A') + 10;
        else
            return -EINVAL;
        if (digit >= base)
            return -EINVAL;
        if (__builtin_mul_overflow(value, base, &value) || __builtin_add_overflow(value, digit, &value))
            return -ERANGE;
        any = true;
    }
    if (*s == '\n' && s[1])
        return -EINVAL;
    if (!any)
        return -EINVAL;
    *res = value;
    return 0;
}

char *lc_strsep(char **s, const char *ct)
{
    char *start = *s, *p;

    if (!start)
        return NULL;
    for (p = start; *p; ++p)
    {
        if (strchr(ct, *p))
        {
            *p = 0;
            *s = p + 1;
            return start;
        }
    }
    *s = NULL;
    return start;
}

struct kmem_cache *lc_kmem_cache_create(const char *name, size_t size)
{
    struct kmem_cache *cache = kzalloc(sizeof(*cache), GFP_KERNEL);

    if (!cache)
        return NULL;
    cache->name = name;
    cache->size = size;
    return cache;
}

void lc_kmem_cache_destroy(struct kmem_cache *cache)
{
    kfree(cache);
}

struct page *lc_alloc_pages(gfp_t gfp, unsigned int order)
{
    size_t count = (size_t)1 << order;
    struct page *pages;
    u64 *pfns;
    void *allocation;
    size_t i;

    (void)gfp;
    pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
    pfns = kcalloc(count, sizeof(*pfns), GFP_KERNEL);
    if (!pages || !pfns)
        goto fail;
    allocation = lc_nt_alloc_pages(count << PAGE_SHIFT, lc_page_limit, LC_NT_CACHE_WRITECOMBINED, pfns, count);
    if (!allocation)
        goto fail;
    for (i = 0; i < count; ++i)
    {
        pages[i].pfn = pfns[i];
        pages[i].allocation = i == 0 ? allocation : NULL;
    }
    kfree(pfns);
    return pages;

fail:
    kfree(pfns);
    kfree(pages);
    return NULL;
}

void lc_free_pages(struct page *page, unsigned int order)
{
    size_t count = (size_t)1 << order, i;

    if (!page)
        return;
    for (i = 0; i < count; ++i)
    {
        if (page[i].kva)
            lc_nt_unmap_pfns(page[i].kva, page[i].map_cookie);
    }
    lc_nt_free_pages(page->allocation);
    kfree(page);
}

void *lc_page_address(struct page *page)
{
    if (!page->kva)
        page->kva = lc_nt_map_pfns(&page->pfn, 1, LC_NT_CACHE_WRITECOMBINED, 0, &page->map_cookie);
    return page->kva;
}

void *lc_vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{
    struct lc_vmap_record *record;
    u64 *pfns;
    unsigned int i;

    (void)flags;
    record = kzalloc(sizeof(*record), GFP_KERNEL);
    pfns = kcalloc(count, sizeof(*pfns), GFP_KERNEL);
    if (!record || !pfns)
    {
        kfree(pfns);
        kfree(record);
        return NULL;
    }
    for (i = 0; i < count; ++i)
        pfns[i] = pages[i]->pfn;
    record->address = lc_nt_map_pfns(pfns, count, prot.cache, 0, &record->cookie);
    kfree(pfns);
    if (!record->address)
    {
        kfree(record);
        return NULL;
    }
    spin_lock(&lc_vmap_lock);
    list_add(&record->node, &lc_vmap_records);
    spin_unlock(&lc_vmap_lock);
    return record->address;
}

void *lc_vmap_pfns(const u64 *pfns, unsigned int count, int cache)
{
    struct lc_vmap_record *record = kzalloc(sizeof(*record), GFP_KERNEL);

    if (!record)
        return NULL;
    record->address = lc_nt_map_pfns(pfns, count, cache, 0, &record->cookie);
    if (!record->address)
    {
        kfree(record);
        return NULL;
    }
    spin_lock(&lc_vmap_lock);
    list_add(&record->node, &lc_vmap_records);
    spin_unlock(&lc_vmap_lock);
    return record->address;
}

void lc_vunmap(const void *addr)
{
    struct lc_vmap_record *record, *found = NULL;

    if (!addr)
        return;
    spin_lock(&lc_vmap_lock);
    list_for_each_entry(record, &lc_vmap_records, node)
    {
        if (record->address == addr)
        {
            list_del(&record->node);
            found = record;
            break;
        }
    }
    spin_unlock(&lc_vmap_lock);
    if (WARN_ON(!found))
        return;
    lc_nt_unmap_pfns(found->address, found->cookie);
    kfree(found);
}

int lc_sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp)
{
    table->sgl = kcalloc(nents ? nents : 1, sizeof(*table->sgl), gfp);
    if (!table->sgl)
        return -ENOMEM;
    if (nents)
        table->sgl[nents - 1].last = true;
    table->nents = nents;
    table->orig_nents = nents;
    return 0;
}

void lc_sg_free_table(struct sg_table *table)
{
    kfree(table->sgl);
    table->sgl = NULL;
    table->nents = 0;
    table->orig_nents = 0;
}

dma_addr_t lc_dma_map_page(struct device *dev, struct page *page, size_t offset, size_t size, enum dma_data_direction dir)
{
    (void)dev;
    (void)size;
    (void)dir;
    return page_to_phys(page) + offset;
}

int lc_dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs)
{
    struct scatterlist *sg;
    unsigned int i;

    (void)dev;
    (void)dir;
    (void)attrs;
    for_each_sgtable_sg(sgt, sg, i)
    {
        sg->dma_address = sg_phys(sg);
        sg->dma_length = sg->length;
    }
    sgt->nents = sgt->orig_nents;
    return 0;
}

int lc_dma_set_mask(struct device *dev, u64 mask)
{
    dev->lc_dma_mask = mask;
    dev->dma_mask = &dev->lc_dma_mask;
    dev->coherent_dma_mask = mask;
    return 0;
}

unsigned long lc_copy_from_user(void *to, const void __user *from, unsigned long n)
{
    return lc_nt_copy_from_user(to, from, n) ? n : 0;
}

unsigned long lc_copy_to_user(void __user *to, const void *from, unsigned long n)
{
    return lc_nt_copy_to_user(to, from, n) ? n : 0;
}

unsigned long lc_clear_user(void __user *to, unsigned long n)
{
    return lc_nt_clear_user(to, n) ? n : 0;
}

int lc_copy_struct_from_user(void *dst, size_t ksize, const void __user *src, size_t usize)
{
    size_t size = min(ksize, usize);

    if (usize < ksize)
    {
        memset((u8 *)dst + size, 0, ksize - size);
    }
    else if (usize > ksize)
    {
        u8 chunk[64];
        size_t offset = ksize;

        while (offset < usize)
        {
            size_t len = min(sizeof(chunk), usize - offset);

            if (lc_nt_copy_from_user(chunk, (const u8 __user *)src + offset, len))
                return -EFAULT;
            if (!mem_is_zero(chunk, len))
                return -E2BIG;
            offset += len;
        }
    }
    if (lc_nt_copy_from_user(dst, src, size))
        return -EFAULT;
    return 0;
}

void *lc_memdup_user(const void __user *src, size_t len)
{
    void *p = kmalloc(len, GFP_KERNEL);

    if (!p)
        return ERR_PTR(-ENOMEM);
    if (lc_nt_copy_from_user(p, src, len))
    {
        kfree(p);
        return ERR_PTR(-EFAULT);
    }
    return p;
}
