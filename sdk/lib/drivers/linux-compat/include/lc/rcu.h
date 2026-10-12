/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux RCU, lock-less lists and printing over NT primitives
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct rcu_head
{
    struct rcu_head *next;
    void (*func)(struct rcu_head *head);
    size_t lc_offset;
};
typedef void (*rcu_callback_t)(struct rcu_head *head);

void lc_rcu_read_lock(void);
void lc_rcu_read_unlock(void);
void lc_call_rcu(struct rcu_head *head, rcu_callback_t func);
void lc_rcu_barrier(void);
void lc_synchronize_rcu(void);
void lc_kvfree_rcu_cb(struct rcu_head *head);
#define rcu_read_lock() lc_rcu_read_lock()
#define rcu_read_unlock() lc_rcu_read_unlock()
#define call_rcu(head, func) lc_call_rcu(head, func)
#define rcu_barrier() lc_rcu_barrier()
#define synchronize_rcu() lc_synchronize_rcu()
#define rcu_dereference(p) READ_ONCE(p)
#define rcu_dereference_raw(p) READ_ONCE(p)
#define rcu_dereference_check(p, c) READ_ONCE(p)
#define rcu_dereference_protected(p, c) (p)
#define rcu_access_pointer(p) READ_ONCE(p)
#define rcu_assign_pointer(p, v) smp_store_release(&(p), (typeof(p))(v))
#define RCU_INIT_POINTER(p, v) do { (p) = (v); } while (0)
#define rcu_replace_pointer(rp, p, c) ({ typeof(rp) __lc_old = (rp); rcu_assign_pointer(rp, p); __lc_old; })
#define rcu_pointer_handoff(p) (p)
#define kfree_rcu(ptr, field) do { typeof(ptr) __lc_kp = (ptr); \
    __lc_kp->field.lc_offset = offsetof(typeof(*__lc_kp), field); lc_call_rcu(&__lc_kp->field, lc_kvfree_rcu_cb); } while (0)
#define kvfree_rcu kfree_rcu
#define rcu_read_lock_held() 1

struct llist_node { struct llist_node *next; };
struct llist_head { struct llist_node *first; };
#define LLIST_HEAD_INIT(name) { NULL }
#define LLIST_HEAD(name) struct llist_head name = LLIST_HEAD_INIT(name)
static inline void init_llist_head(struct llist_head *list) { list->first = NULL; }
static inline bool llist_empty(const struct llist_head *head) { return READ_ONCE(head->first) == NULL; }
static inline void init_llist_node(struct llist_node *node) { node->next = node; }
static inline bool llist_on_list(const struct llist_node *node) { return node->next != node; }
static inline bool llist_add(struct llist_node *n, struct llist_head *head)
{
    struct llist_node *first = __atomic_load_n(&head->first, __ATOMIC_RELAXED);
    do
        n->next = first;
    while (!__atomic_compare_exchange_n(&head->first, &first, n, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    return first == NULL;
}
static inline struct llist_node *llist_del_all(struct llist_head *head) { return __atomic_exchange_n(&head->first, NULL, __ATOMIC_ACQUIRE); }
static inline struct llist_node *llist_reverse_order(struct llist_node *head)
{
    struct llist_node *out = NULL;
    while (head)
    {
        struct llist_node *n = head;
        head = head->next;
        n->next = out;
        out = n;
    }
    return out;
}
#define llist_entry(ptr, type, member) container_of(ptr, type, member)
#define llist_for_each(pos, node) for ((pos) = (node); (pos); (pos) = (pos)->next)
#define __lc_member_nonnull(ptr, member) ((uintptr_t)(ptr) + offsetof(typeof(*(ptr)), member) != 0)
#define llist_for_each_entry(pos, node, member) \
    for ((pos) = llist_entry((node), typeof(*(pos)), member); __lc_member_nonnull(pos, member); \
         (pos) = llist_entry((pos)->member.next, typeof(*(pos)), member))
#define llist_for_each_entry_safe(pos, n, node, member) \
    for (pos = llist_entry((node), typeof(*pos), member); __lc_member_nonnull(pos, member) && \
         (n = llist_entry(pos->member.next, typeof(*n), member), true); pos = n)

#define KERN_EMERG "\0010"
#define KERN_ALERT "\0011"
#define KERN_CRIT "\0012"
#define KERN_ERR "\0013"
#define KERN_WARNING "\0014"
#define KERN_NOTICE "\0015"
#define KERN_INFO "\0016"
#define KERN_DEBUG "\0017"
#define KERN_CONT ""
int lc_printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int lc_vprintk(const char *fmt, va_list args);
#define printk(fmt, ...) lc_printk(fmt, ##__VA_ARGS__)
#define pr_fmt(fmt) fmt
#define pr_emerg(fmt, ...) lc_printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) lc_printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) lc_printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) lc_printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) lc_printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...) do { if (0) lc_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define pr_cont(fmt, ...) lc_printk(fmt, ##__VA_ARGS__)
#define pr_err_once pr_err
#define pr_warn_once pr_warn
#define pr_info_once pr_info
#define pr_warn_ratelimited pr_warn
#define pr_err_ratelimited pr_err

void lc_warn_report(const char *file, int line);
void lc_bug_report(const char *file, int line) __attribute__((noreturn));
#define WARN_ON(cond) ({ bool __lc_c = !!(cond); if (unlikely(__lc_c)) lc_warn_report(__FILE__, __LINE__); unlikely(__lc_c); })
#define WARN_ON_ONCE(cond) ({ static bool __lc_w; bool __lc_c = !!(cond); \
    if (unlikely(__lc_c) && !__lc_w) { __lc_w = true; lc_warn_report(__FILE__, __LINE__); } unlikely(__lc_c); })
#define WARN(cond, fmt, ...) ({ bool __lc_c = !!(cond); \
    if (unlikely(__lc_c)) { lc_printk(KERN_WARNING fmt, ##__VA_ARGS__); lc_warn_report(__FILE__, __LINE__); } unlikely(__lc_c); })
#define WARN_ONCE(cond, fmt, ...) ({ static bool __lc_w; bool __lc_c = !!(cond); \
    if (unlikely(__lc_c) && !__lc_w) { __lc_w = true; lc_printk(KERN_WARNING fmt, ##__VA_ARGS__); lc_warn_report(__FILE__, __LINE__); } \
    unlikely(__lc_c); })
#define BUG() lc_bug_report(__FILE__, __LINE__)
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)
#define VM_BUG_ON(cond) do { (void)sizeof(cond); } while (0)
