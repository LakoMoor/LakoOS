/*
 * PROJECT:     LakoOS command line utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Prints the result of WinINet and WinHTTP requests
 */

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

void TestWinHttp(LPCWSTR host, BOOL secure)
{
    HINTERNET hSession, hConnect, hRequest;
    DWORD status = 0, len = sizeof(status);

    printf("winhttp  %s://%ls/\n", secure ? "https" : "http", host);
    hSession = WinHttpOpen(L"LakoOS nettest", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession)
    {
        printf("  WinHttpOpen failed %lu\n", GetLastError());
        return;
    }
    hConnect = WinHttpConnect(hSession, host, secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", L"/", NULL, NULL, NULL,
                                             secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (!hRequest)
    {
        printf("  open request failed %lu\n", GetLastError());
    }
    else if (!WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(hRequest, NULL))
    {
        printf("  request failed %lu\n", GetLastError());
    }
    else
    {
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, &status, &len, NULL);
        printf("  status %lu\n", status);
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

