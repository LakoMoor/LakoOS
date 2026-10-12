/*
 * PROJECT:     LakoOS PowerVR user-mode transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Constant helpers used by the DRM user API headers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define __AC(X, Y) (X##Y)
#define _AC(X, Y) __AC(X, Y)
#define _ULL(x) (_AC(x, ULL))
#define _BITULL(x) (_ULL(1) << (x))
