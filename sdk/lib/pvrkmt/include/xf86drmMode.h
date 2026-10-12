/*
 * PROJECT:     LakoOS PowerVR user-mode transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display node queries; the transport exposes render nodes only
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <xf86drm.h>

#ifdef __cplusplus
extern "C" {
#endif

int drmIsKMS(int fd);

#ifdef __cplusplus
}
#endif
