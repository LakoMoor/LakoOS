/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux kernel logging, warnings and fatal assertions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <linux_compat.h>

static int lc_vlog(const char *prefix, const char *fmt, va_list args)
{
    char buffer[512];
    int level = 6, offset = 0, len;

    while (fmt[0] == '\001' && fmt[1] >= '0' && fmt[1] <= '7')
    {
        level = fmt[1] - '0';
        fmt += 2;
    }
    if (level > 6)
        return 0;
    if (prefix)
        offset = lc_scnprintf(buffer, sizeof(buffer), "%s", prefix);
    len = lc_vscnprintf(buffer + offset, sizeof(buffer) - (size_t)offset, fmt, args);
    lc_nt_log(level, buffer);
    return offset + len;
}

int lc_vprintk(const char *fmt, va_list args)
{
    return lc_vlog(NULL, fmt, args);
}

int lc_printk(const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = lc_vlog(NULL, fmt, args);
    va_end(args);
    return r;
}

int lc_dev_printk(const char *level, const struct device *dev, const char *fmt, ...)
{
    char format[320];
    va_list args;
    int r;

    lc_scnprintf(format, sizeof(format), "%s%s: %s", level, dev ? dev_name(dev) : "(NULL device)", fmt);
    va_start(args, fmt);
    r = lc_vlog(NULL, format, args);
    va_end(args);
    return r;
}

void lc_warn_report(const char *file, int line)
{
    lc_printk(KERN_WARNING "WARNING: at %s:%d\n", file, line);
}

void lc_bug_report(const char *file, int line)
{
    lc_printk(KERN_EMERG "BUG: at %s:%d\n", file, line);
    lc_nt_bug(file, line);
}
