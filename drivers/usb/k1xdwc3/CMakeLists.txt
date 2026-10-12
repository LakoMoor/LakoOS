#
# PROJECT:     LakoOS SpacemiT K1 USB3 Glue Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 USB3 glue driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

list(APPEND SOURCE
    k1xdwc3.c
    k1xdwc3.h)

add_library(k1xdwc3 MODULE ${SOURCE} k1xdwc3.rc)
set_module_type(k1xdwc3 kernelmodedriver)
add_importlibs(k1xdwc3 ntoskrnl hal)
add_cd_file(TARGET k1xdwc3 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xdwc3 usbk1xdwc3.inf)
