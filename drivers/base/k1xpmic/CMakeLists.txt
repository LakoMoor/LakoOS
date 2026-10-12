#
# PROJECT:     LakoOS SpacemiT K1 PMIC Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 PMIC driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xpmic MODULE k1xpmic.c k1xpmic.rc)
set_module_type(k1xpmic kernelmodedriver)
add_importlibs(k1xpmic ntoskrnl hal)
add_cd_file(TARGET k1xpmic DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xpmic k1xpmic.inf)
