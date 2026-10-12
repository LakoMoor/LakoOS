# PROJECT:     LakoOS GPT Support Library
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     Identifiers and default names of new GPT partitions
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_library(gptsup gptsup.c)
target_link_libraries(gptsup csprng)
add_dependencies(gptsup psdk xdk)
