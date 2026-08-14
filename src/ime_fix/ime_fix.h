/*
 * ime_fix.dll — IME management for The Binding of Isaac: Repentance+
 * Version defines and shared logging utility.
 */

#ifndef IME_FIX_H
#define IME_FIX_H

#include <windows.h>

/* Version */
#define IME_FIX_MAJOR 0
#define IME_FIX_MINOR 2
#define IME_FIX_PATCH 1
#define IME_FIX_STRING "0.2.1"

/*
 * Shared debug logging — writes to %APPDATA%\ime-conflict-fix\debug.log
 * Uses only kernel32.dll + user32.dll (wsprintf), no CRT.
 */
static void ime_log(const char *tag, const char *message)
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

    len = wsprintfA(line, "[ime_fix:%s] %s at %S\r\n", tag, message, ts);
    WriteFile(h, line, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

#endif /* IME_FIX_H */