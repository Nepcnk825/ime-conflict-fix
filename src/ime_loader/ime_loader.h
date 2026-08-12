/*
 * ime_loader.h - Loader for IME Conflict Fix
 * Version defines and shared logging utility (same pattern as ime_fix.h).
 */

#ifndef IME_LOADER_H
#define IME_LOADER_H

#include <windows.h>

/* Version */
#define IME_LOADER_MAJOR 0
#define IME_LOADER_MINOR 1
#define IME_LOADER_PATCH 0
#define IME_LOADER_STRING "0.1.0"

/* Shared debug logging - writes to %APPDATA%\ime-conflict-fix\debug.log */
static void loader_log(const char *tag, const char *message)
{
    WCHAR appdata[MAX_PATH];
    WCHAR dir[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR ts[64];
    SYSTEMTIME st;
    HANDLE h;
    DWORD w;
    char line[512];
    int len;

    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0)
        return;

    wsprintfW(dir, L"%s\\ime-conflict-fix", appdata);
    CreateDirectoryW(dir, NULL);
    wsprintfW(file, L"%s\\debug.log", dir);

    GetLocalTime(&st);
    wsprintfW(ts, L"%04d-%02d-%02d %02d:%02d:%02d",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);

    h = CreateFileW(file,
                    FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);

    if (h == INVALID_HANDLE_VALUE)
        return;

    len = wsprintfA(line, "[ime_loader:%s] %s at %S\r\n", tag, message, ts);
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

#endif /* IME_LOADER_H */
