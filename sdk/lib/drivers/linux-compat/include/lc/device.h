/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux device model, platform devices, IRQs, clocks, runtime PM and firmware
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct device;
struct device_link;

struct lc_of_property
{
    const char *name;
    const u32 *values;
    unsigned int count;
    unsigned int cells_per_entry;
};

struct device_node
{
    const char *const *compatible;
    unsigned int compatible_count;
    const char *name;
    const struct lc_of_property *properties;
    unsigned int property_count;
};

struct of_device_id
{
    char name[32];
    char type[32];
    char compatible[128];
    const void *data;
};

struct dev_pm_ops
{
    int (*suspend)(struct device *dev);
    int (*resume)(struct device *dev);
    int (*runtime_suspend)(struct device *dev);
    int (*runtime_resume)(struct device *dev);
    int (*runtime_idle)(struct device *dev);
};
#define RUNTIME_PM_OPS(s, r, i) .runtime_suspend = (s), .runtime_resume = (r), .runtime_idle = (i),
#define SYSTEM_SLEEP_PM_OPS(s, r) .suspend = (s), .resume = (r),
#define pm_ptr(p) (p)

struct device_driver
{
    const char *name;
    const struct dev_pm_ops *pm;
    const struct of_device_id *of_match_table;
};

struct clk;
struct lc_clk_ops
{
    int (*enable)(void *context);
    void (*disable)(void *context);
    unsigned long (*get_rate)(void *context);
};

struct lc_dev_pm
{
    struct mutex lock;
    spinlock_t state_lock;
    int usage;
    bool active;
    bool enabled;
    bool error;
};

struct device
{
    struct device *parent;
    struct device_node *of_node;
    const char *init_name;
    void *driver_data;
    const struct device_driver *driver;
    u64 *dma_mask;
    u64 coherent_dma_mask;
    u64 lc_dma_mask;
    struct list_head lc_devres;
    spinlock_t lc_devres_lock;
    struct list_head lc_clks;
    struct lc_dev_pm lc_pm;
    void *lc_context;
};

#define IORESOURCE_IO 0x00000100
#define IORESOURCE_MEM 0x00000200
#define IORESOURCE_IRQ 0x00000400
struct resource
{
    resource_size_t start;
    resource_size_t end;
    const char *name;
    unsigned long flags;
};
static inline resource_size_t resource_size(const struct resource *res) { return res->end - res->start + 1; }

struct platform_device
{
    const char *name;
    int id;
    struct device dev;
    u32 num_resources;
    struct resource *resource;
};
struct platform_driver
{
    int (*probe)(struct platform_device *pdev);
    void (*remove)(struct platform_device *pdev);
    void (*shutdown)(struct platform_device *pdev);
    struct device_driver driver;
};
#define to_platform_device(d) container_of(d, struct platform_device, dev)
#define module_platform_driver(drv) \
    struct platform_driver *lc_module_platform_driver(void) { return &(drv); }
struct platform_driver *lc_module_platform_driver(void);
static inline void *dev_get_drvdata(const struct device *dev) { return dev->driver_data; }
static inline void dev_set_drvdata(struct device *dev, void *data) { dev->driver_data = data; }
static inline void *platform_get_drvdata(const struct platform_device *pdev) { return pdev->dev.driver_data; }
static inline void platform_set_drvdata(struct platform_device *pdev, void *data) { pdev->dev.driver_data = data; }
static inline const char *dev_name(const struct device *dev) { return dev->init_name ? dev->init_name : "device"; }
struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
int platform_get_irq(struct platform_device *pdev, unsigned int num);
int platform_get_irq_byname(struct platform_device *pdev, const char *name);
void __iomem *devm_platform_get_and_ioremap_resource(struct platform_device *pdev, unsigned int index, struct resource **res);
void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index);
void __iomem *devm_ioremap(struct device *dev, resource_size_t offset, resource_size_t size);
void __iomem *lc_ioremap(phys_addr_t offset, size_t size);
void lc_iounmap(volatile void __iomem *addr);
#define ioremap(o, s) lc_ioremap(o, s)
#define ioremap_wc(o, s) lc_ioremap(o, s)
#define iounmap(a) lc_iounmap(a)

int devm_add_action(struct device *dev, void (*action)(void *), void *data);
int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data);
void devm_remove_action(struct device *dev, void (*action)(void *), void *data);
void devm_release_action(struct device *dev, void (*action)(void *), void *data);
void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp);
void devm_kfree(struct device *dev, const void *p);
#define devm_kzalloc(d, s, g) devm_kmalloc(d, s, (g) | __GFP_ZERO)
#define devm_kcalloc(d, n, s, g) devm_kmalloc(d, size_mul(n, s), (g) | __GFP_ZERO)
void lc_device_initialize(struct device *dev, const char *name);
void lc_device_release_resources(struct device *dev);

typedef enum irqreturn
{
    IRQ_NONE = 0,
    IRQ_HANDLED = 1,
    IRQ_WAKE_THREAD = 2,
} irqreturn_t;
typedef irqreturn_t (*irq_handler_t)(int irq, void *data);
#define IRQF_SHARED 0x00000080UL
#define IRQF_ONESHOT 0x00002000UL
#define IRQF_TRIGGER_HIGH 0x00000004UL
int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
                         unsigned long flags, const char *name, void *dev_id);
static inline int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev_id)
{
    return request_threaded_irq(irq, handler, NULL, flags, name, dev_id);
}
int devm_request_threaded_irq(struct device *dev, unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
                              unsigned long flags, const char *name, void *dev_id);
const void *free_irq(unsigned int irq, void *dev_id);
void disable_irq(unsigned int irq);
void disable_irq_nosync(unsigned int irq);
void enable_irq(unsigned int irq);
void synchronize_irq(unsigned int irq);
bool lc_in_hardirq(void);
#define in_interrupt() lc_in_hardirq()
#define in_irq() lc_in_hardirq()
#define in_task() (!lc_in_hardirq())
#define in_atomic() (!lc_nt_at_passive())
#define irqs_disabled() (!lc_nt_at_passive())

#define LC_IRQ_MAX 8
int lc_irq_register_line(uint32_t vector, uint8_t irql, bool level_sensitive);
bool lc_irq_service(unsigned int irq, bool *queue_dpc);
void lc_irq_dpc(unsigned int irq);

struct clk *lc_device_add_clk(struct device *dev, const char *id, const struct lc_clk_ops *ops, void *context);
struct clk *devm_clk_get(struct device *dev, const char *id);
struct clk *devm_clk_get_optional(struct device *dev, const char *id);
#define clk_get(d, id) devm_clk_get(d, id)
#define clk_put(c) do { (void)(c); } while (0)
int clk_prepare_enable(struct clk *clk);
void clk_disable_unprepare(struct clk *clk);
#define clk_prepare(c) ((void)(c), 0)
#define clk_unprepare(c) do { (void)(c); } while (0)
#define clk_enable(c) clk_prepare_enable(c)
#define clk_disable(c) clk_disable_unprepare(c)
unsigned long clk_get_rate(struct clk *clk);
static inline int clk_set_rate(struct clk *clk, unsigned long rate) { (void)clk; (void)rate; return -EOPNOTSUPP; }

struct reset_control;
static inline struct reset_control *devm_reset_control_get_optional_exclusive(struct device *dev, const char *id) { (void)dev; (void)id; return NULL; }
static inline struct reset_control *devm_reset_control_get_optional(struct device *dev, const char *id) { (void)dev; (void)id; return NULL; }
static inline struct reset_control *devm_reset_control_get_exclusive(struct device *dev, const char *id) { (void)dev; (void)id; return ERR_PTR(-ENOENT); }
static inline int reset_control_assert(struct reset_control *rstc) { (void)rstc; return 0; }
static inline int reset_control_deassert(struct reset_control *rstc) { (void)rstc; return 0; }
static inline int reset_control_reset(struct reset_control *rstc) { (void)rstc; return 0; }

struct pwrseq_desc;
static inline struct pwrseq_desc *devm_pwrseq_get(struct device *dev, const char *target) { (void)dev; (void)target; return ERR_PTR(-ENODEV); }
static inline int pwrseq_power_on(struct pwrseq_desc *d) { (void)d; return -ENODEV; }
static inline int pwrseq_power_off(struct pwrseq_desc *d) { (void)d; return -ENODEV; }
#define pwrseq_enable pwrseq_power_on
#define pwrseq_disable pwrseq_power_off

struct dev_pm_domain_attach_data
{
    const char *const *pd_names;
    u32 num_pd_names;
    u32 pd_flags;
};
struct dev_pm_domain_list
{
    struct device **pd_devs;
    struct device_link **pd_links;
    u32 *opp_tokens;
    u32 num_pds;
};
#define PD_FLAG_NO_DEV_LINK (1U << 0)
#define PD_FLAG_DEV_LINK_ON (1U << 1)
static inline int dev_pm_domain_attach_list(struct device *dev, const struct dev_pm_domain_attach_data *data,
                                            struct dev_pm_domain_list **list)
{
    (void)dev;
    (void)data;
    *list = NULL;
    return 0;
}
static inline void dev_pm_domain_detach_list(struct dev_pm_domain_list *list) { (void)list; }
#define DL_FLAG_STATELESS (1U << 0)
#define DL_FLAG_PM_RUNTIME (1U << 5)
#define DL_FLAG_RPM_ACTIVE (1U << 6)
static inline struct device_link *device_link_add(struct device *consumer, struct device *supplier, u32 flags)
{
    (void)consumer;
    (void)supplier;
    (void)flags;
    return NULL;
}
static inline void device_link_del(struct device_link *link) { (void)link; }

int pm_runtime_resume_and_get(struct device *dev);
int pm_runtime_get_sync(struct device *dev);
int pm_runtime_get_if_in_use(struct device *dev);
int pm_runtime_get_if_active(struct device *dev);
int pm_runtime_put(struct device *dev);
int pm_runtime_put_sync(struct device *dev);
int pm_runtime_put_sync_suspend(struct device *dev);
int pm_runtime_put_autosuspend(struct device *dev);
void pm_runtime_put_noidle(struct device *dev);
int pm_runtime_suspend(struct device *dev);
int pm_runtime_resume(struct device *dev);
int pm_runtime_set_active(struct device *dev);
int pm_runtime_set_suspended(struct device *dev);
void pm_runtime_enable(struct device *dev);
void pm_runtime_disable(struct device *dev);
int devm_pm_runtime_enable(struct device *dev);
bool pm_runtime_status_suspended(struct device *dev);
bool pm_runtime_active(struct device *dev);
#define pm_runtime_mark_last_busy(d) do { (void)(d); } while (0)
#define pm_runtime_set_autosuspend_delay(d, ms) do { (void)(d); (void)(ms); } while (0)
#define pm_runtime_use_autosuspend(d) do { (void)(d); } while (0)
#define pm_runtime_dont_use_autosuspend(d) do { (void)(d); } while (0)
#define pm_runtime_get_noresume(d) do { (void)(d); } while (0)

const void *of_device_get_match_data(const struct device *dev);
const struct lc_of_property *lc_of_find_property(const struct device_node *np, const char *name);
static inline int of_count_phandle_with_args(const struct device_node *np, const char *list_name, const char *cells_name)
{
    const struct lc_of_property *prop = lc_of_find_property(np, list_name);

    (void)cells_name;
    if (!prop)
        return -ENOENT;
    return (int)(prop->count / (prop->cells_per_entry ? prop->cells_per_entry : 1));
}
static inline bool of_property_present(const struct device_node *np, const char *name) { return lc_of_find_property(np, name) != NULL; }
static inline int of_property_read_u32(const struct device_node *np, const char *name, u32 *value)
{
    const struct lc_of_property *prop = lc_of_find_property(np, name);

    if (!prop)
        return -EINVAL;
    if (!prop->count)
        return -ENODATA;
    *value = prop->values[0];
    return 0;
}
bool of_device_is_compatible(const struct device_node *np, const char *compatible);
static inline int device_property_read_u32(struct device *dev, const char *name, u32 *value)
{
    (void)dev;
    (void)name;
    (void)value;
    return -EINVAL;
}
static inline bool device_property_present(struct device *dev, const char *name) { (void)dev; (void)name; return false; }
enum dev_dma_attr
{
    DEV_DMA_NOT_SUPPORTED,
    DEV_DMA_NON_COHERENT,
    DEV_DMA_COHERENT,
};
static inline enum dev_dma_attr device_get_dma_attr(struct device *dev) { (void)dev; return DEV_DMA_NON_COHERENT; }
static inline bool dev_is_dma_coherent(struct device *dev) { (void)dev; return false; }

struct firmware
{
    size_t size;
    const u8 *data;
    void *priv;
};
int request_firmware(const struct firmware **fw, const char *name, struct device *dev);
int firmware_request_nowarn(const struct firmware **fw, const char *name, struct device *dev);
void release_firmware(const struct firmware *fw);

int lc_dev_printk(const char *level, const struct device *dev, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define dev_emerg(d, fmt, ...) lc_dev_printk(KERN_EMERG, d, fmt, ##__VA_ARGS__)
#define dev_err(d, fmt, ...) lc_dev_printk(KERN_ERR, d, fmt, ##__VA_ARGS__)
#define dev_warn(d, fmt, ...) lc_dev_printk(KERN_WARNING, d, fmt, ##__VA_ARGS__)
#define dev_notice(d, fmt, ...) lc_dev_printk(KERN_NOTICE, d, fmt, ##__VA_ARGS__)
#define dev_info(d, fmt, ...) lc_dev_printk(KERN_INFO, d, fmt, ##__VA_ARGS__)
#define dev_dbg(d, fmt, ...) do { if (0) lc_dev_printk(KERN_DEBUG, d, fmt, ##__VA_ARGS__); } while (0)
#define dev_err_once dev_err
#define dev_warn_once dev_warn
#define dev_info_once dev_info
#define dev_err_ratelimited dev_err
#define dev_warn_ratelimited dev_warn
#define dev_err_probe(d, err, fmt, ...) ({ int __lc_e = (err); \
    if (__lc_e != -EPROBE_DEFER) lc_dev_printk(KERN_ERR, d, fmt, ##__VA_ARGS__); __lc_e; })
#define dev_WARN(d, fmt, ...) WARN(1, fmt, ##__VA_ARGS__)
#define dev_WARN_ONCE(d, cond, fmt, ...) WARN_ONCE(cond, fmt, ##__VA_ARGS__)
