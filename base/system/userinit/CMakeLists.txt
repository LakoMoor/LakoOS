
list(APPEND SOURCE
    livecd.c
    userinit.c
    userinit.h)

add_rc_deps(userinit.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/rosbitmap.bmp ${CMAKE_CURRENT_SOURCE_DIR}/res/rosbitmap_mask.bmp)
add_executable(userinit ${SOURCE} userinit.rc)
target_link_libraries(userinit wine)
set_module_type(userinit win32gui UNICODE)
add_importlibs(userinit advapi32 userenv gdi32 user32 powrprof msvcrt kernel32 ntdll)
if(ENABLE_EXPERIMENTAL_EARLY_SPLASH)
    target_compile_definitions(userinit PRIVATE ENABLE_EXPERIMENTAL_EARLY_SPLASH)
endif()
add_pch(userinit userinit.h SOURCE)
add_cd_file(TARGET userinit DESTINATION reactos/system32 FOR all)
