#
# PROJECT:     LakoOS SpacemiT K1 thermal driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     SpacemiT K1 thermal driver build rules
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(k1xthermal MODULE k1xthermal.c k1xthermal.rc)
set_module_type(k1xthermal kernelmodedriver)
add_importlibs(k1xthermal ntoskrnl hal)
add_cd_file(TARGET k1xthermal DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(k1xthermal k1xthermal.inf)
