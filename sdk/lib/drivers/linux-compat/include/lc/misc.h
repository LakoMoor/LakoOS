/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux tasks, ELF, seq_file, debugfs, averages and tracing stubs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define PF_EXITING 0x00000004
#define SIGKILL 9
#define CAP_SYS_NICE 23
#define CAP_SYS_ADMIN 21
#define TASK_COMM_LEN 16
struct task_struct
{
    struct list_head lc_node;
    void *lc_thread_id;
    lc_nt_event lc_wake;
    struct task_struct *group_leader;
    unsigned int flags;
    int exit_code;
    pid_t pid;
    pid_t tgid;
    char comm[TASK_COMM_LEN];
};
struct task_struct *lc_current_task(void);
#define current lc_current_task()
static inline pid_t task_tgid_nr(struct task_struct *task) { return task->tgid; }
static inline pid_t task_pid_nr(struct task_struct *task) { return task->pid; }
static inline bool capable(int cap) { (void)cap; return true; }
static inline bool fatal_signal_pending(struct task_struct *task) { (void)task; return false; }
static inline bool signal_pending(struct task_struct *task) { (void)task; return false; }
#define preempt_disable() do { } while (0)
#define preempt_enable() do { } while (0)
#define local_irq_save(f) do { (f) = 0; } while (0)
#define local_irq_restore(f) do { (void)(f); } while (0)
#define num_online_cpus() lc_nt_processor_count()
#define num_possible_cpus() lc_nt_processor_count()
static inline u32 get_random_u32(void) { return lc_nt_random(); }

#define PT_LOAD 1
#define EI_NIDENT 16
typedef u32 Elf32_Addr;
typedef u16 Elf32_Half;
typedef u32 Elf32_Off;
typedef u32 Elf32_Word;
struct elf32_hdr
{
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};
struct elf32_phdr
{
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};
_Static_assert(sizeof(struct elf32_hdr) == 52, "ELF32 header size");
_Static_assert(sizeof(struct elf32_phdr) == 32, "ELF32 program header size");

struct dentry;
struct inode { void *i_private; };
struct seq_file
{
    void *private;
};
struct seq_operations
{
    void *(*start)(struct seq_file *m, loff_t *pos);
    void (*stop)(struct seq_file *m, void *v);
    void *(*next)(struct seq_file *m, void *v, loff_t *pos);
    int (*show)(struct seq_file *m, void *v);
};
static inline int seq_open(struct file *file, const struct seq_operations *op) { (void)file; (void)op; return -ENOSYS; }
static inline int seq_release(struct inode *inode, struct file *file) { (void)inode; (void)file; return 0; }
static inline ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    (void)file;
    (void)buf;
    (void)size;
    (void)ppos;
    return -ENOSYS;
}
static inline loff_t seq_lseek(struct file *file, loff_t offset, int whence) { (void)file; (void)offset; (void)whence; return -ENOSYS; }
#define seq_printf(m, fmt, ...) do { (void)(m); } while (0)
#define seq_puts(m, s) do { (void)(m); (void)(s); } while (0)
#define seq_putc(m, c) do { (void)(m); (void)(c); } while (0)
static inline struct dentry *debugfs_create_file(const char *name, umode_t mode, struct dentry *parent, void *data,
                                                 const struct file_operations *fops)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)data;
    (void)fops;
    return NULL;
}
static inline struct dentry *debugfs_create_dir(const char *name, struct dentry *parent) { (void)name; (void)parent; return NULL; }
#define debugfs_remove_recursive(d) do { (void)(d); } while (0)
#define DEFINE_SHOW_ATTRIBUTE(name)
#define DEFINE_DEBUGFS_ATTRIBUTE(name, get, set, fmt) static const struct file_operations name = { 0 }
#define DEFINE_SIMPLE_ATTRIBUTE DEFINE_DEBUGFS_ATTRIBUTE
#define simple_read_from_buffer(to, count, ppos, from, available) ((ssize_t)-ENOSYS)
#define simple_open NULL
#define default_llseek NULL
#define no_llseek NULL

#define DECLARE_EWMA(name, _precision, _weight_rcp)                                                  \
    struct ewma_##name { unsigned long internal; };                                                   \
    static inline void ewma_##name##_init(struct ewma_##name *e) { e->internal = 0; }                 \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e)                             \
    {                                                                                                 \
        return e->internal >> (_precision);                                                           \
    }                                                                                                 \
    static inline void ewma_##name##_add(struct ewma_##name *e, unsigned long val)                    \
    {                                                                                                 \
        unsigned long current_value = READ_ONCE(e->internal);                                         \
        unsigned long sample = val << (_precision);                                                   \
        unsigned long next = current_value ?                                                          \
            current_value - current_value / (_weight_rcp) + sample / (_weight_rcp) : sample;          \
        WRITE_ONCE(e->internal, next);                                                                \
    }

#define TRACE_EVENT(...)
#define DECLARE_EVENT_CLASS(...)
#define DEFINE_EVENT(...)
#define TRACE_SYSTEM

#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) \
    (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))
#define _IO(type, nr) _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, argtype) _IOC(_IOC_READ, (type), (nr), sizeof(argtype))
#define _IOW(type, nr, argtype) _IOC(_IOC_WRITE, (type), (nr), sizeof(argtype))
#define _IOWR(type, nr, argtype) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(argtype))
#define _IOC_DIR(nr) (((nr) >> _IOC_DIRSHIFT) & ((1U << _IOC_DIRBITS) - 1))
#define _IOC_TYPE(nr) (((nr) >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1))
#define _IOC_NR(nr) (((nr) >> _IOC_NRSHIFT) & ((1U << _IOC_NRBITS) - 1))
#define _IOC_SIZE(nr) (((nr) >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1))
#define IOC_IN (_IOC_WRITE << _IOC_DIRSHIFT)
#define IOC_OUT (_IOC_READ << _IOC_DIRSHIFT)
