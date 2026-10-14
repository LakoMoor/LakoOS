/*
 * PROJECT:     LakoOS command line utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Prints the result of WinINet and WinHTTP requests
 */

#include <windows.h>
#include <wininet.h>
#include <stdio.h>

void TestWinInet(LPCWSTR url)
{
    HINTERNET hNet, hUrl;
    DWORD flags = 0, size = 0, status = 0, len = sizeof(status), read;
    char buf[256];

    printf("wininet  %ls\n", url);
    printf("  connected state: %d (flags 0x%lx)\n", InternetGetConnectedState(&flags, 0), flags);
    hNet = InternetOpenW(L"LakoOS nettest", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet)
    {
        printf("  InternetOpen failed %lu\n", GetLastError());
        return;
    }
    hUrl = InternetOpenUrlW(hNet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl)
    {
        printf("  InternetOpenUrl failed %lu\n", GetLastError());
        InternetCloseHandle(hNet);
        return;
    }
    HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, NULL);
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read)
        size += read;
    printf("  status %lu, %lu bytes\n", status, size);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
}

