/*
 * PROJECT:     LakoOS command line utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Prints the result of WinINet and WinHTTP requests
 */

#include <windows.h>
#include <stdio.h>

void TestWinInet(LPCWSTR url);
void TestWinHttp(LPCWSTR host, BOOL secure);

int wmain(int argc, WCHAR **argv)
{
    LPCWSTR host = argc > 1 ? argv[1] : L"client-update.steamstatic.com";
    WCHAR url[512];

    wsprintfW(url, L"http://%s/", host);
    TestWinInet(url);
    wsprintfW(url, L"https://%s/", host);
    TestWinInet(url);
    TestWinHttp(host, FALSE);
    TestWinHttp(host, TRUE);
    return 0;
}
