add_executable(nettest nettest.c inet.c whttp.c)
set_module_type(nettest win32cui UNICODE)
add_importlibs(nettest wininet winhttp user32 msvcrt kernel32)
add_cd_file(TARGET nettest DESTINATION reactos/system32 FOR all)
