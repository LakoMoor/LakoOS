project(control11)

list(APPEND SOURCE
    catalog.cpp
    pages.cpp
    main.cpp
    control11.h)

file(GLOB control11_rc_deps res/*.*)
add_rc_deps(control11.rc ${control11_rc_deps})
add_executable(control ${SOURCE} control11.rc)
set_module_type(control win32gui UNICODE)
target_link_libraries(control uuid cpprt)
add_importlibs(control advapi32 user32 gdi32 shell32 shlwapi ole32 oleaut32 comctl32 powrprof iphlpapi netapi32 uxtheme msvcrt kernel32 ntdll)
add_pch(control control11.h SOURCE)
add_cd_file(TARGET control DESTINATION reactos/system32 FOR all)
