/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boundary between the Linux compatibility layer and the NT kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define LC_NT_SPINLOCK_SIZE 16
#define LC_NT_EVENT_SIZE 32
#define LC_NT_TIMER_SIZE 192

typedef struct lc_nt_spinlock { uint64_t opaque[LC_NT_SPINLOCK_SIZE / 8]; } lc_nt_spinlock;
typedef struct lc_nt_event { uint64_t opaque[LC_NT_EVENT_SIZE / 8]; } lc_nt_event;
typedef struct lc_nt_timer { uint64_t opaque[LC_NT_TIMER_SIZE / 8]; } lc_nt_timer;

void lc_nt_spin_init(lc_nt_spinlock *lock);
void lc_nt_spin_acquire(lc_nt_spinlock *lock);
void lc_nt_spin_release(lc_nt_spinlock *lock);

void lc_nt_event_init(lc_nt_event *event, int synchronization, int signaled);
void lc_nt_event_set(lc_nt_event *event);
void lc_nt_event_clear(lc_nt_event *event);
int lc_nt_event_wait(lc_nt_event *event, int64_t timeout_100ns);

void *lc_nt_current_thread(void);
void *lc_nt_current_thread_id(void);
int lc_nt_set_thread_exit_callback(void (*callback)(void *thread_id));
void lc_nt_clear_thread_exit_callback(void);
int lc_nt_create_thread(void (*routine)(void *), void *context, void **handle);
void lc_nt_wait_thread(void *handle);
void lc_nt_yield(void);
int lc_nt_at_passive(void);
int lc_nt_at_dispatch_or_above(void);
unsigned int lc_nt_processor_count(void);

void lc_nt_timer_init(lc_nt_timer *timer, void (*routine)(void *), void *context);
void lc_nt_timer_set(lc_nt_timer *timer, int64_t due_100ns);
int lc_nt_timer_cancel(lc_nt_timer *timer);

uint64_t lc_nt_time_ns(void);
void lc_nt_stall_us(uint32_t us);
void lc_nt_sleep_100ns(int64_t interval);

void *lc_nt_alloc(size_t size, int zero);
void lc_nt_free(void *ptr);

#define LC_NT_CACHE_CACHED 0
#define LC_NT_CACHE_UNCACHED 1
#define LC_NT_CACHE_WRITECOMBINED 2

void *lc_nt_alloc_pages(size_t size, uint64_t highest_address, int cache, uint64_t *pfns, size_t pfn_count);
void lc_nt_free_pages(void *allocation);
void *lc_nt_map_pfns(const uint64_t *pfns, size_t count, int cache, int user, void **map_cookie);
void lc_nt_unmap_pfns(void *va, void *map_cookie);
void *lc_nt_map_io(uint64_t phys, size_t size, int cache);
void lc_nt_unmap_io(void *va, size_t size);

int lc_nt_copy_from_user(void *dst, const void *src, size_t size);
int lc_nt_copy_to_user(void *dst, const void *src, size_t size);
int lc_nt_clear_user(void *dst, size_t size);

int lc_nt_read_file(const char *name, void **data, size_t *size);
void lc_nt_free_file(void *data);

void lc_nt_irq_mask(uint32_t vector, uint8_t irql);
void lc_nt_irq_unmask(uint32_t vector, uint8_t irql, int level_sensitive);

void lc_nt_rcu_synchronize(void);
void lc_nt_rcu_read_lock(void);
void lc_nt_rcu_read_unlock(void);

void lc_nt_memory_barrier(void);
uint32_t lc_nt_read32(volatile void *address);
uint64_t lc_nt_read64(volatile void *address);
void lc_nt_write32(volatile void *address, uint32_t value);
void lc_nt_write64(volatile void *address, uint64_t value);

void lc_nt_log(int level, const char *text);
void lc_nt_bug(const char *file, int line) __attribute__((noreturn));
int lc_nt_in_interrupt(void);
int lc_nt_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
uint32_t lc_nt_random(void);
uint32_t lc_nt_current_pid(void);
