
PROJECT(mmc)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/atl)
add_definitions(-D_ATL_NO_EXCEPTIONS)

list(APPEND CPP_SOURCE
    CAboutSnapinDialog.cpp
    CAddAdvancedDialog.cpp
    CAddDialog.cpp
    CConsoleWnd.cpp
    CMainWnd.cpp
    COptionsDialog.cpp
    CRecentFileEntry.cpp
    CSnapin.cpp
    CSnapinCacheEntry.cpp
    CSnapinAlias.cpp
    misc.cpp
    mscfile.cpp
    mmc.cpp
    # This is here so that they show up in visual studio:
    CAddDialog.h
    CCustomizeDialog.h
    COptionsDialog.h
    CConsoleWnd.h
    CMainWnd.h
    CSnapin.h
    CImageList.h
    precomp.h
    resource.h)

add_rc_deps(mmc.rc ${CMAKE_CURRENT_SOURCE_DIR}/resources/mmc.ico)
add_executable(mmc ${CPP_SOURCE} mmc.rc)
set_module_type(mmc win32gui UNICODE)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mmc PRIVATE -Wno-write-strings)
endif()
target_link_libraries(mmc uuid cpprt mmc_uuids)
add_importlibs(mmc oleaut32 ole32 shlwapi shell32 advapi32 comdlg32 comctl32 user32 gdi32 msvcrt kernel32 ntdll)
add_pch(mmc precomp.h CPP_SOURCE)
add_dependencies(mmc psdk)
add_cd_file(TARGET mmc DESTINATION reactos/system32 FOR all)
