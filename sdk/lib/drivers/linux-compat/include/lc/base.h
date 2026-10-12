/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux kernel types, macros and helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef u8 __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef s8 __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;
typedef u16 __le16;
typedef u32 __le32;
typedef u64 __le64;
typedef u16 __be16;
typedef u32 __be32;
typedef u64 __be64;
typedef u64 aligned_u64 __attribute__((aligned(8)));
typedef s64 aligned_s64 __attribute__((aligned(8)));
typedef u64 aligned_be64 __attribute__((aligned(8)));
typedef u64 aligned_le64 __attribute__((aligned(8)));
typedef u64 phys_addr_t;
typedef u64 dma_addr_t;
typedef u64 resource_size_t;
typedef unsigned int gfp_t;
typedef s64 ktime_t;
typedef s64 loff_t;
typedef long long __kernel_loff_t;
typedef size_t __kernel_size_t;
typedef intptr_t __kernel_ssize_t;
typedef unsigned long __kernel_ulong_t;
typedef long __kernel_long_t;
typedef int pid_t;
typedef unsigned short umode_t;
typedef intptr_t ssize_t;

#define __user
#define __iomem
#define __force
#define __must_check
#define __rcu
#define __init
#define __exit
#define __maybe_unused __attribute__((unused))
#define __always_unused __attribute__((unused))
#undef __always_inline
#define __always_inline inline __attribute__((always_inline))
#define noinline __attribute__((noinline))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __cold
#define __pure
#define __read_mostly
#define __counted_by(x)
#define __nonstring
#define __guarded_by(x)
#define __acquires(x)
#define __releases(x)
#define __must_hold(x)
#define fallthrough __attribute__((fallthrough))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define __diag_push()
#define __diag_pop()
#define __diag_ignore_all(a, b)
#define __is_constexpr(x) __builtin_constant_p(x)
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define barrier() __asm__ __volatile__("" ::: "memory")
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL_IF_KUNIT(x)
#define VISIBLE_IF_KUNIT static
#define module_init(fn) int __LC_CAT(lc_module_init_, fn)(void) { return fn(); }
#define module_exit(fn) void __LC_CAT(lc_module_exit_, fn)(void) { fn(); }
int lc_compat_init(void);
void lc_compat_exit(void);
#define for_each_if(condition) if (!(condition)) {} else
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_FIRMWARE(x)
#define MODULE_DEVICE_TABLE(a, b)
#define MODULE_IMPORT_NS(x)
#define MODULE_PARM_DESC(a, b)
#define module_param(name, type, perm) void *lc_module_param_##name(void) { return &name; }
#define module_param_named(name, var, type, perm) void *lc_module_param_##name(void) { return &var; }
#define module_param_string(name, var, len, perm)
#define THIS_MODULE NULL
#define IS_ENABLED(x) 0
#define IS_BUILTIN(x) 0
#define IS_REACHABLE(x) 0

#define U8_MAX 0xFFU
#define U16_MAX 0xFFFFU
#define U32_MAX 0xFFFFFFFFU
#define U64_MAX 0xFFFFFFFFFFFFFFFFULL
#define S32_MAX 0x7FFFFFFF
#define S64_MAX 0x7FFFFFFFFFFFFFFFLL
#define UINT_MAX 0xFFFFFFFFU
#define INT_MAX 0x7FFFFFFF
#define INT_MIN (-INT_MAX - 1)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#ifndef SIZE_MAX
#define SIZE_MAX __SIZE_MAX__
#endif
#define U64_C(x) x##ULL
#define _AC(X, Y) (X##Y)
#define _UL(x) (_AC(x, UL))
#define _ULL(x) (_AC(x, ULL))
#define _BITUL(x) (_UL(1) << (x))
#define _BITULL(x) (_ULL(1) << (x))
#define UL(x) _UL(x)
#define ULL(x) _ULL(x)

#define BITS_PER_LONG (__SIZEOF_LONG__ * 8)
#define BITS_PER_LONG_LONG 64
#define BITS_PER_BYTE 8
#define BIT(nr) (1ULL << (nr))
#define BIT_ULL(nr) (1ULL << (nr))
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define GENMASK(h, l) GENMASK_ULL(h, l)
#define GENMASK_ULL(h, l) ((unsigned long long)((~0ULL >> (63 - (h))) & ~((1ULL << (l)) - 1ULL)))
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(mask, reg) ((typeof(mask))(((reg) & (mask)) >> __bf_shf(mask)))
#define FIELD_PREP(mask, val) (((typeof(mask))(val) << __bf_shf(mask)) & (mask))
#define FIELD_MAX(mask) ((typeof(mask))((mask) >> __bf_shf(mask)))
#define FIELD_FIT(mask, val) (!((((typeof(mask))(val)) << __bf_shf(mask)) & ~(mask)))
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#define upper_32_bits(n) ((u32)((u64)(n) >> 32))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#define typeof_member(T, m) typeof(((T *)0)->m)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define container_of_const(ptr, type, member) container_of(ptr, type, member)
#define BUILD_BUG_ON(cond) _Static_assert(!(cond), "BUILD_BUG_ON")
#define BUILD_BUG_ON_MSG(cond, msg) _Static_assert(!(cond), msg)
#define static_assert(expr, ...) _Static_assert(expr, #expr)
#define __bitwise

#define ALIGN(x, a) ((typeof(x))(((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1)))
#define ALIGN_DOWN(x, a) ((x) & ~((typeof(x))(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)
#define PTR_ALIGN(p, a) ((typeof(p))ALIGN((uintptr_t)(p), (a)))
#define round_up(x, y) ALIGN(x, y)
#define round_down(x, y) ((x) & ~((typeof(x))((y) - 1)))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(n, d) DIV_ROUND_UP((unsigned long long)(n), (d))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y) ((x) - ((x) % (y)))
#define do_div(n, base) ({ u32 __base = (base); u32 __rem = (u32)((n) % __base); (n) = (n) / __base; __rem; })
#define div_u64(a, b) ((u64)(a) / (u32)(b))
#define div64_u64(a, b) ((u64)(a) / (u64)(b))
#define mult_frac(x, n, d) (((x) / (d)) * (n) + (((x) % (d)) * (n)) / (d))

#undef min
#undef max
#define min(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); __a < __b ? __a : __b; })
#define max(a, b) ({ typeof(a) __a = (a); typeof(b) __b = (b); __a > __b ? __a : __b; })
#define min_t(t, a, b) ({ t __a = (a); t __b = (b); __a < __b ? __a : __b; })
#define max_t(t, a, b) ({ t __a = (a); t __b = (b); __a > __b ? __a : __b; })
#define min3(a, b, c) min(min(a, b), c)
#define max3(a, b, c) max(max(a, b), c)
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define clamp_val(v, lo, hi) clamp_t(typeof(v), v, lo, hi)
#define swap(a, b) do { typeof(a) __t = (a); (a) = (b); (b) = __t; } while (0)
#define abs(x) ({ typeof(x) __x = (x); __x < 0 ? -__x : __x; })

#define check_add_overflow(a, b, d) __builtin_add_overflow(a, b, d)
#define check_sub_overflow(a, b, d) __builtin_sub_overflow(a, b, d)
#define check_mul_overflow(a, b, d) __builtin_mul_overflow(a, b, d)
static inline size_t size_mul(size_t a, size_t b)
{
    size_t r;
    return __builtin_mul_overflow(a, b, &r) ? SIZE_MAX : r;
}
static inline size_t size_add(size_t a, size_t b)
{
    size_t r;
    return __builtin_add_overflow(a, b, &r) ? SIZE_MAX : r;
}
static inline size_t size_sub(size_t a, size_t b)
{
    return (a < b || a == SIZE_MAX || b == SIZE_MAX) ? SIZE_MAX : a - b;
}
#define array_size(a, b) size_mul(a, b)
#define struct_size(p, member, count) \
    (__builtin_constant_p(count) ? sizeof(*(p)) + sizeof(*(p)->member) * (count) : size_add(sizeof(*(p)), size_mul(sizeof(*(p)->member), count)))
#define struct_size_t(type, member, count) size_add(sizeof(type), size_mul(sizeof(((type *)0)->member[0]), count))
#define flex_array_size(p, member, count) \
    (__builtin_constant_p(count) ? sizeof(*(p)->member) * (count) : size_mul(sizeof(*(p)->member), count))

#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) do { *(volatile typeof(x) *)&(x) = (val); } while (0)
#define mb() lc_nt_memory_barrier()
#define rmb() lc_nt_memory_barrier()
#define wmb() lc_nt_memory_barrier()
#define smp_mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)
#define dma_rmb() rmb()
#define dma_wmb() wmb()
#define smp_store_release(p, v) do { __atomic_store_n(p, v, __ATOMIC_RELEASE); } while (0)
#define smp_load_acquire(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#define smp_mb__after_atomic() smp_mb()
#define smp_mb__before_atomic() smp_mb()

#define MAX_ERRNO 4095
#undef EPERM
#undef ENOENT
#undef ESRCH
#undef EINTR
#undef EIO
#undef ENXIO
#undef E2BIG
#undef EBADF
#undef EAGAIN
#undef ENOMEM
#undef EACCES
#undef EFAULT
#undef EBUSY
#undef EEXIST
#undef ENODEV
#undef EINVAL
#undef ENOSPC
#undef ERANGE
#undef EDEADLK
#undef ENOSYS
#undef ENODATA
#undef ETIME
#undef EOVERFLOW
#undef EOPNOTSUPP
#undef ETIMEDOUT
#undef ECANCELED
#undef ENOTSUPP
#undef ERESTARTSYS
#undef EMFILE
#undef ENOTTY
#undef ENAMETOOLONG
#undef EALREADY
#undef ENOLINK
#undef ENOEXEC
#undef EPIPE
#undef ENOTEMPTY
#undef EPROBE_DEFER
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define ENOSPC 28
#define ERANGE 34
#define EDEADLK 35
#define ENOSYS 38
#define ENODATA 61
#define ETIME 62
#define EOVERFLOW 75
#define EOPNOTSUPP 95
#define ETIMEDOUT 110
#define ECANCELED 125
#define ENOTSUPP 524
#define ERESTARTSYS 512
#define EMFILE 24
#define ENOTTY 25
#define ENAMETOOLONG 36
#define EALREADY 114
#define ENOLINK 67
#define ENOEXEC 8
#define EPIPE 32
#define ENOTEMPTY 39
#define EPROBE_DEFER 517

#define IS_ERR_VALUE(x) unlikely((uintptr_t)(x) >= (uintptr_t)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return (void *)(intptr_t)error; }
static inline long PTR_ERR(const void *ptr) { return (long)(intptr_t)ptr; }
static inline bool IS_ERR(const void *ptr) { return IS_ERR_VALUE((uintptr_t)ptr); }
static inline bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR(ptr); }
static inline void *ERR_CAST(const void *ptr) { return (void *)ptr; }
static inline int PTR_ERR_OR_ZERO(const void *ptr) { return IS_ERR(ptr) ? (int)PTR_ERR(ptr) : 0; }

static inline unsigned long __ffs(u64 word) { return (unsigned long)__builtin_ctzll(word); }
static inline int fls(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
static inline int fls64(u64 x) { return x ? 64 - __builtin_clzll(x) : 0; }
static inline unsigned long __fls(u64 word) { return (unsigned long)(63 - __builtin_clzll(word)); }
static inline int lc_ffs(int x) { return __builtin_ffs(x); }
#define ffs lc_ffs
static inline unsigned int hweight32(u32 w) { return __builtin_popcount(w); }
static inline unsigned int hweight64(u64 w) { return __builtin_popcountll(w); }
static inline bool is_power_of_2(u64 n) { return n != 0 && (n & (n - 1)) == 0; }
static inline unsigned int ilog2(u64 n) { return 63 - __builtin_clzll(n); }
static inline u64 roundup_pow_of_two(u64 n) { return n <= 1 ? 1 : 1ULL << (64 - __builtin_clzll(n - 1)); }
static inline u64 rounddown_pow_of_two(u64 n) { return 1ULL << __fls(n); }
#define order_base_2(n) ((n) > 1 ? ilog2((n) - 1) + 1 : 0)
static inline void set_bit(long nr, volatile unsigned long *addr) { __atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST); }
static inline void clear_bit(long nr, volatile unsigned long *addr) { __atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr), __ATOMIC_SEQ_CST); }
static inline bool test_bit(long nr, const volatile unsigned long *addr) { return (addr[BIT_WORD(nr)] & BIT_MASK(nr)) != 0; }
static inline bool test_and_set_bit(long nr, volatile unsigned long *addr) { return (__atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0; }
static inline bool test_and_clear_bit(long nr, volatile unsigned long *addr) { return (__atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr), __ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0; }
static inline void __set_bit(long nr, volatile unsigned long *addr) { addr[BIT_WORD(nr)] |= BIT_MASK(nr); }
static inline void __clear_bit(long nr, volatile unsigned long *addr) { addr[BIT_WORD(nr)] &= ~BIT_MASK(nr); }

static inline bool mem_is_zero(const void *s, size_t n)
{
    const u8 *p = (const u8 *)s;
    while (n--)
        if (*p++)
            return false;
    return true;
}

static inline void *memchr_inv(const void *s, int c, size_t n)
{
    const u8 *p = (const u8 *)s;
    for (; n; ++p, --n)
        if (*p != (u8)c)
            return (void *)p;
    return NULL;
}

static inline u64 u64_to_user_ptr_value(u64 x) { return x; }
#define u64_to_user_ptr(x) ((void __user *)(uintptr_t)(x))

#define HZ 1000
#define NSEC_PER_USEC 1000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_MSEC 1000L
#define USEC_PER_SEC 1000000L
#define MSEC_PER_SEC 1000L
#define MAX_SCHEDULE_TIMEOUT LONG_MAX
#define MAX_JIFFY_OFFSET ((LONG_MAX >> 1) - 1)

#define SZ_1 0x00000001
#define SZ_2 0x00000002
#define SZ_4 0x00000004
#define SZ_8 0x00000008
#define SZ_16 0x00000010
#define SZ_32 0x00000020
#define SZ_64 0x00000040
#define SZ_128 0x00000080
#define SZ_256 0x00000100
#define SZ_512 0x00000200
#define SZ_1K 0x00000400
#define SZ_2K 0x00000800
#define SZ_4K 0x00001000
#define SZ_8K 0x00002000
#define SZ_16K 0x00004000
#define SZ_32K 0x00008000
#define SZ_64K 0x00010000
#define SZ_128K 0x00020000
#define SZ_256K 0x00040000
#define SZ_512K 0x00080000
#define SZ_1M 0x00100000
#define SZ_2M 0x00200000
#define SZ_4M 0x00400000
#define SZ_8M 0x00800000
#define SZ_16M 0x01000000
#define SZ_32M 0x02000000
#define SZ_64M 0x04000000
#define SZ_128M 0x08000000
#define SZ_256M 0x10000000
#define SZ_512M 0x20000000
#define SZ_1G 0x40000000
#define SZ_2G 0x80000000ULL
#define SZ_4G 0x100000000ULL
#define SZ_8G 0x200000000ULL
#define SZ_16G 0x400000000ULL
#define SZ_32G 0x800000000ULL
#define SZ_64G 0x1000000000ULL
#define SZ_128G 0x2000000000ULL
#define SZ_256G 0x4000000000ULL
#define SZ_512G 0x8000000000ULL
#define SZ_1T 0x10000000000ULL
#define SZ_2T 0x20000000000ULL
#define SZ_4T 0x40000000000ULL

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1ULL))
#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)
#define PAGE_ALIGNED(addr) IS_ALIGNED((uintptr_t)(addr), PAGE_SIZE)
#define PFN_DOWN(x) ((x) >> PAGE_SHIFT)
#define PFN_UP(x) (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)

static inline size_t strscpy_impl(char *dst, const char *src, size_t size)
{
    size_t len = 0;
    if (!size)
        return (size_t)-E2BIG;
    while (len + 1 < size && src[len])
    {
        dst[len] = src[len];
        len++;
    }
    dst[len] = 0;
    return src[len] ? (size_t)-E2BIG : len;
}
#define strscpy(dst, src, ...) ((ssize_t)strscpy_impl((dst), (src), sizeof(dst)))

int lc_kstrtou64(const char *s, unsigned int base, u64 *res);
static inline int kstrtou16(const char *s, unsigned int base, u16 *res)
{
    u64 v;
    int r = lc_kstrtou64(s, base, &v);
    if (r)
        return r;
    if (v > U16_MAX)
        return -ERANGE;
    *res = (u16)v;
    return 0;
}
static inline int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
    u64 v;
    int r = lc_kstrtou64(s, base, &v);
    if (r)
        return r;
    if (v > UINT_MAX)
        return -ERANGE;
    *res = (unsigned int)v;
    return 0;
}
char *lc_strsep(char **s, const char *ct);
#define strsep lc_strsep
int lc_snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int lc_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int lc_scnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int lc_vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
#define snprintf lc_snprintf
#define vsnprintf lc_vsnprintf
#define scnprintf lc_scnprintf
#define vscnprintf lc_vscnprintf
#define sprintf(buf, fmt, ...) lc_snprintf(buf, SIZE_MAX, fmt, ##__VA_ARGS__)
