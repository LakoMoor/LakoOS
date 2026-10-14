add_executable(lakoupdate lakoupdate.c)
set_module_type(lakoupdate win32cui UNICODE)
add_importlibs(lakoupdate winhttp bcrypt newdev advapi32 user32 msvcrt kernel32)
add_cd_file(TARGET lakoupdate DESTINATION reactos/system32 FOR all)
