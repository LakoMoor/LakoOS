#
# PROJECT:     LakoOS SpacemiT K1 watchdog driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 watchdog driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xwdt MODULE k1xwdt.c k1xwdt.rc)
set_module_type(k1xwdt kernelmodedriver)
add_importlibs(k1xwdt ntoskrnl hal)
add_cd_file(TARGET k1xwdt DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xwdt k1xwdt.inf)
