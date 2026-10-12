/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux memory allocation, pages, DMA mapping, MMIO and user copies over NT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define __GFP_ZERO 0x100u
#define __GFP_NOWARN 0x200u
#define __GFP_RETRY_MAYFAIL 0x400u
#define __GFP_NORETRY 0x800u
#define __GFP_HIGHMEM 0x1000u
#define __GFP_DMA32 0x2000u
#define __GFP_COMP 0x4000u
#define __GFP_ACCOUNT 0x8000u
#define __GFP_NOFAIL 0x10000u
#define GFP_KERNEL 0x1u
#define GFP_ATOMIC 0x2u
#define GFP_NOWAIT 0x4u
#define GFP_NOFS GFP_KERNEL
#define GFP_NOIO GFP_KERNEL
#define GFP_USER GFP_KERNEL
#define GFP_HIGHUSER GFP_KERNEL
#define GFP_DMA32 __GFP_DMA32
#define GFP_KERNEL_ACCOUNT GFP_KERNEL

#define ZERO_SIZE_PTR ((void *)16)
#define ZERO_OR_NULL_PTR(x) ((uintptr_t)(x) <= (uintptr_t)ZERO_SIZE_PTR)
void *lc_kmalloc(size_t size, gfp_t flags);
void lc_set_page_allocation_limit(u64 highest_address);
u64 lc_get_page_allocation_limit(void);
void *lc_vmap_pfns(const u64 *pfns, unsigned int count, int cache);
void lc_kfree(const void *ptr);
size_t lc_ksize(const void *ptr);
void *lc_krealloc(const void *ptr, size_t size, gfp_t flags);
static inline void *kmalloc(size_t size, gfp_t flags) { return lc_kmalloc(size, flags); }
static inline void *kzalloc(size_t size, gfp_t flags) { return lc_kmalloc(size, flags | __GFP_ZERO); }
static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
    size_t bytes;
    if (__builtin_mul_overflow(n, size, &bytes))
        return NULL;
    return lc_kmalloc(bytes, flags);
}
static inline void *kcalloc(size_t n, size_t size, gfp_t flags) { return kmalloc_array(n, size, flags | __GFP_ZERO); }
static inline void *krealloc(const void *p, size_t size, gfp_t flags) { return lc_krealloc(p, size, flags); }
static inline void *krealloc_array(void *p, size_t n, size_t size, gfp_t flags)
{
    size_t bytes;
    if (__builtin_mul_overflow(n, size, &bytes))
        return NULL;
    return lc_krealloc(p, bytes, flags);
}
static inline void kfree(const void *p) { lc_kfree(p); }
static inline void kfree_sensitive(const void *p) { lc_kfree(p); }
#define kvmalloc kmalloc
#define kvzalloc kzalloc
#define kvmalloc_array kmalloc_array
#define kvcalloc kcalloc
#define kvrealloc(p, size, flags) krealloc(p, size, flags)
#define kvfree kfree
#define vmalloc(size) kmalloc(size, GFP_KERNEL)
#define vzalloc(size) kzalloc(size, GFP_KERNEL)
#define vfree kfree
#define ksize lc_ksize
#define __lc_obj_gfp(...) __lc_obj_gfp_(__VA_ARGS__ __VA_OPT__(,) GFP_KERNEL)
#define __lc_obj_gfp_(gfp, ...) (gfp)
#define kmalloc_obj(v, ...) ((typeof(v) *)kmalloc(sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kzalloc_obj(v, ...) ((typeof(v) *)kzalloc(sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kmalloc_objs(v, n, ...) ((typeof(v) *)kmalloc_array(n, sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kzalloc_objs(v, n, ...) ((typeof(v) *)kcalloc(n, sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kvmalloc_objs(v, n, ...) ((typeof(v) *)kmalloc_array(n, sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kvzalloc_objs(v, n, ...) ((typeof(v) *)kcalloc(n, sizeof(v), __lc_obj_gfp(__VA_ARGS__)))
#define kzalloc_flex(v, member, n, ...) ((typeof(v) *)kzalloc(struct_size(&(v), member, n), __lc_obj_gfp(__VA_ARGS__)))
void *lc_kmemdup(const void *src, size_t len, gfp_t gfp);
char *lc_kstrdup(const char *s, gfp_t gfp);
char *lc_kasprintf(gfp_t gfp, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *lc_kvasprintf(gfp_t gfp, const char *fmt, va_list args);
#define kmemdup(s, l, g) lc_kmemdup(s, l, g)
#define kstrdup(s, g) lc_kstrdup(s, g)
#define kstrdup_const(s, g) lc_kstrdup(s, g)
#define kfree_const(p) kfree(p)
#define kasprintf(g, fmt, ...) lc_kasprintf(g, fmt, ##__VA_ARGS__)
#define kvasprintf(g, fmt, a) lc_kvasprintf(g, fmt, a)
#define kmemleak_alloc(p, s, m, g) do { } while (0)
#define kmemleak_free(p) do { } while (0)
#define kmemleak_ignore(p) do { } while (0)
DEFINE_FREE(kfree, void *, if (_T) kfree(_T))
DEFINE_FREE(kvfree, void *, if (_T) kvfree(_T))

struct kmem_cache
{
    size_t size;
    const char *name;
};
struct kmem_cache *lc_kmem_cache_create(const char *name, size_t size);
void lc_kmem_cache_destroy(struct kmem_cache *cache);
#define KMEM_CACHE(s, flags) lc_kmem_cache_create(#s, sizeof(struct s))
#define kmem_cache_create(name, size, align, flags, ctor) lc_kmem_cache_create(name, size)
#define kmem_cache_destroy(c) lc_kmem_cache_destroy(c)
#define kmem_cache_alloc(c, g) kmalloc((c)->size, g)
#define kmem_cache_zalloc(c, g) kzalloc((c)->size, g)
#define kmem_cache_free(c, p) kfree(p)
#define SLAB_HWCACHE_ALIGN 0
#define SLAB_TYPESAFE_BY_RCU 0

struct page
{
    u64 pfn;
    void *allocation;
    void *kva;
    void *map_cookie;
};
typedef struct { int cache; } pgprot_t;
#define PAGE_KERNEL ((pgprot_t){ LC_NT_CACHE_CACHED })
#define PAGE_KERNEL_IO ((pgprot_t){ LC_NT_CACHE_UNCACHED })
static inline pgprot_t pgprot_writecombine(pgprot_t p) { (void)p; return (pgprot_t){ LC_NT_CACHE_WRITECOMBINED }; }
static inline pgprot_t pgprot_noncached(pgprot_t p) { (void)p; return (pgprot_t){ LC_NT_CACHE_UNCACHED }; }
#define VM_MAP 0x4u
#define VM_IO 0x1u
#define VM_PFNMAP 0x2u
#define VM_DONTEXPAND 0x8u
#define VM_DONTDUMP 0x10u
#define VM_WRITE 0x20u
#define VM_READ 0x40u
#define VM_SHARED 0x80u
struct page *lc_alloc_pages(gfp_t gfp, unsigned int order);
void lc_free_pages(struct page *page, unsigned int order);
#define alloc_page(g) lc_alloc_pages(g, 0)
#define alloc_pages(g, o) lc_alloc_pages(g, o)
#define __free_page(p) lc_free_pages(p, 0)
#define __free_pages(p, o) lc_free_pages(p, o)
#define put_page(p) lc_free_pages(p, 0)
static inline phys_addr_t page_to_phys(const struct page *page) { return (phys_addr_t)page->pfn << PAGE_SHIFT; }
static inline unsigned long page_to_pfn(const struct page *page) { return (unsigned long)page->pfn; }
void *lc_vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot);
void lc_vunmap(const void *addr);
void *lc_page_address(struct page *page);
#define vmap(p, c, f, prot) lc_vmap(p, c, f, prot)
#define vunmap(a) lc_vunmap(a)
#define page_address(p) lc_page_address(p)

struct scatterlist
{
    struct page *page;
    unsigned int offset;
    unsigned int length;
    dma_addr_t dma_address;
    unsigned int dma_length;
    bool last;
};
struct sg_table
{
    struct scatterlist *sgl;
    unsigned int nents;
    unsigned int orig_nents;
};
#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->dma_length)
static inline struct page *sg_page(struct scatterlist *sg) { return sg->page; }
static inline struct scatterlist *sg_next(struct scatterlist *sg) { return sg->last ? NULL : sg + 1; }
static inline void sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len, unsigned int offset)
{
    sg->page = page;
    sg->offset = offset;
    sg->length = len;
}
static inline phys_addr_t sg_phys(struct scatterlist *sg) { return page_to_phys(sg->page) + sg->offset; }
int lc_sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp);
void lc_sg_free_table(struct sg_table *table);
#define sg_alloc_table(t, n, g) lc_sg_alloc_table(t, n, g)
#define sg_free_table(t) lc_sg_free_table(t)
#define for_each_sg(sglist, sg, nr, i) for ((i) = 0, (sg) = (sglist); (i) < (nr); (i)++, (sg) = sg_next(sg))
#define for_each_sgtable_sg(sgt, sg, i) for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#define for_each_sgtable_dma_sg(sgt, sg, i) for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)
struct sg_dma_page_iter
{
    struct scatterlist *sg;
    unsigned int sg_index;
    unsigned int nents;
    unsigned long page_offset;
};
static inline bool lc_sg_dma_page_iter_next(struct sg_dma_page_iter *it)
{
    while (it->sg)
    {
        unsigned long pages = (it->sg->dma_length + PAGE_SIZE - 1) >> PAGE_SHIFT;
        if (it->page_offset + 1 < pages)
        {
            it->page_offset++;
            return true;
        }
        if (++it->sg_index >= it->nents)
        {
            it->sg = NULL;
            return false;
        }
        it->sg = sg_next(it->sg);
        it->page_offset = 0;
        if (it->sg && it->sg->dma_length)
            return true;
    }
    return false;
}
static inline bool lc_sg_dma_page_iter_start(struct sg_dma_page_iter *it, struct sg_table *sgt, unsigned long pgoffset)
{
    it->sg = sgt->nents ? sgt->sgl : NULL;
    it->sg_index = 0;
    it->nents = sgt->nents;
    it->page_offset = 0;
    while (it->sg)
    {
        unsigned long pages = (it->sg->dma_length + PAGE_SIZE - 1) >> PAGE_SHIFT;
        if (pgoffset < pages)
        {
            it->page_offset = pgoffset;
            return true;
        }
        pgoffset -= pages;
        if (++it->sg_index >= it->nents)
            break;
        it->sg = sg_next(it->sg);
    }
    it->sg = NULL;
    return false;
}
#define for_each_sgtable_dma_page(sgt, dma_iter, pgoffset) \
    for (bool __lc_more = lc_sg_dma_page_iter_start(dma_iter, sgt, pgoffset); __lc_more; \
         __lc_more = lc_sg_dma_page_iter_next(dma_iter))
static inline dma_addr_t sg_page_iter_dma_address(struct sg_dma_page_iter *it)
{
    return it->sg->dma_address + ((dma_addr_t)it->page_offset << PAGE_SHIFT);
}

enum dma_data_direction
{
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE = 1,
    DMA_FROM_DEVICE = 2,
    DMA_NONE = 3,
};
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_ATTR_SKIP_CPU_SYNC (1UL << 5)
#define DMA_MAPPING_ERROR (~(dma_addr_t)0)
struct device;
dma_addr_t lc_dma_map_page(struct device *dev, struct page *page, size_t offset, size_t size, enum dma_data_direction dir);
int lc_dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir, unsigned long attrs);
#define dma_map_page(d, p, o, s, dir) lc_dma_map_page(d, p, o, s, dir)
#define dma_unmap_page(d, a, s, dir) do { (void)(d); (void)(a); } while (0)
#define dma_mapping_error(d, a) ((a) == DMA_MAPPING_ERROR)
#define dma_map_sgtable(d, sgt, dir, attrs) lc_dma_map_sgtable(d, sgt, dir, attrs)
#define dma_unmap_sgtable(d, sgt, dir, attrs) do { (void)(d); (void)(sgt); } while (0)
#define dma_sync_sgtable_for_cpu(d, sgt, dir) do { (void)(d); (void)(sgt); } while (0)
#define dma_sync_sgtable_for_device(d, sgt, dir) do { (void)(d); (void)(sgt); } while (0)
#define dma_sync_single_for_device(d, a, s, dir) do { (void)(d); (void)(a); } while (0)
#define dma_sync_single_for_cpu(d, a, s, dir) do { (void)(d); (void)(a); } while (0)
int lc_dma_set_mask(struct device *dev, u64 mask);
#define dma_set_mask(d, m) lc_dma_set_mask(d, m)
#define dma_set_mask_and_coherent(d, m) lc_dma_set_mask(d, m)
#define dma_set_coherent_mask(d, m) lc_dma_set_mask(d, m)
#define dma_set_max_seg_size(d, s) do { (void)(d); (void)(s); } while (0)

struct iosys_map
{
    union
    {
        void *vaddr_iomem;
        void *vaddr;
    };
    bool is_iomem;
};
#define IOSYS_MAP_INIT_VADDR(v) { { .vaddr = (v) }, .is_iomem = false }
static inline void iosys_map_set_vaddr(struct iosys_map *m, void *v) { m->vaddr = v; m->is_iomem = false; }
static inline bool iosys_map_is_null(const struct iosys_map *m) { return !m->vaddr; }
static inline bool iosys_map_is_set(const struct iosys_map *m) { return m->vaddr != NULL; }
static inline void iosys_map_clear(struct iosys_map *m) { m->vaddr = NULL; m->is_iomem = false; }

static inline u32 readl(const volatile void __iomem *addr) { return lc_nt_read32((volatile void *)addr); }
static inline u64 readq(const volatile void __iomem *addr) { return lc_nt_read64((volatile void *)addr); }
static inline void writel(u32 v, volatile void __iomem *addr) { lc_nt_write32(addr, v); }
static inline void writeq(u64 v, volatile void __iomem *addr) { lc_nt_write64(addr, v); }
#define readl_relaxed(a) readl(a)
#define writel_relaxed(v, a) writel(v, a)
#define ioread32(a) readl(a)
#define ioread64(a) readq(a)
#define iowrite32(v, a) writel(v, a)
#define iowrite64(v, a) writeq(v, a)
#define memcpy_toio(d, s, n) memcpy((void *)(d), s, n)
#define memcpy_fromio(d, s, n) memcpy(d, (const void *)(s), n)
#define memset_io(d, c, n) memset((void *)(d), c, n)

unsigned long lc_copy_from_user(void *to, const void __user *from, unsigned long n);
unsigned long lc_copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long lc_clear_user(void __user *to, unsigned long n);
#define copy_from_user(to, from, n) lc_copy_from_user(to, from, n)
#define copy_to_user(to, from, n) lc_copy_to_user(to, from, n)
#define clear_user(to, n) lc_clear_user(to, n)
#define access_ok(p, n) ((void)(p), (void)(n), 1)
#define get_user(x, p) ({ typeof(*(p)) __lc_v; int __lc_e = lc_copy_from_user(&__lc_v, p, sizeof(__lc_v)) ? -EFAULT : 0; \
    if (!__lc_e) (x) = __lc_v; __lc_e; })
#define put_user(x, p) ({ typeof(*(p)) __lc_v = (x); lc_copy_to_user(p, &__lc_v, sizeof(__lc_v)) ? -EFAULT : 0; })
int lc_copy_struct_from_user(void *dst, size_t ksize, const void __user *src, size_t usize);
#define copy_struct_from_user(d, k, s, u) lc_copy_struct_from_user(d, k, s, u)
void *lc_memdup_user(const void __user *src, size_t len);
#define memdup_user(s, l) lc_memdup_user(s, l)
#define vmemdup_user(s, l) lc_memdup_user(s, l)
