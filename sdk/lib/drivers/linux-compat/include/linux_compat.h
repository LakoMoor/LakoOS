/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux kernel interfaces for code imported into LakoOS drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include "lc/nt.h"
#include "lc/base.h"
#include "lc/sync.h"
#include "lc/rcu.h"
#include "lc/time.h"
#include "lc/mem.h"
#include "lc/tree.h"
#include "lc/device.h"
#include "lc/fence.h"
#include "lc/misc.h"
