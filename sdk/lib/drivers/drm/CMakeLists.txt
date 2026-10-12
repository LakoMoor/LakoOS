# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

set(DRM_CORE_HEADERS
    drm/drm_auth.h
    drm/drm_device.h
    drm/drm_drv.h
    drm/drm_file.h
    drm/drm_gem.h
    drm/drm_gem_shmem_helper.h
    drm/drm_ioctl.h
    drm/drm_managed.h
    drm/drm_mm.h
    drm/drm_prime.h
    drm/drm_print.h
    drm/drm_utils.h
    drm/drm_vma_manager.h
    drm_internal.h
    gpu_scheduler_trace.h)

set(DRM_CORE_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
foreach(_header ${DRM_CORE_HEADERS})
    set(_target ${DRM_CORE_GENERATED_INCLUDE}/${_header})
    if(NOT EXISTS ${_target})
        file(WRITE ${_target} "#include <drm_compat.h>\n")
    endif()
endforeach()

list(APPEND SOURCE
    drm_core.c
    drm_exec.c
    drm_gem.c
    drm_gpuvm.c
    drm_syncobj.c
    scheduler/sched_entity.c
    scheduler/sched_fence.c
    scheduler/sched_main.c
    scheduler/sched_rq.c)

add_library(drm_core ${SOURCE})
add_dependencies(drm_core bugcodes xdk)
target_compile_definitions(drm_core PRIVATE __KERNEL__)
target_link_libraries(drm_core PUBLIC linux_compat)
target_include_directories(drm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/uapi
    ${DRM_CORE_GENERATED_INCLUDE})
if(NOT MSVC)
    target_compile_options(drm_core PRIVATE -Wno-unused-function)
endif()
