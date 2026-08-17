/*
 * ime_fix.dll — IME management for The Binding of Isaac: Repentance+
 * Version defines and shared logging utility.
 */

#ifndef IME_FIX_H
#define IME_FIX_H

#include <windows.h>

/* Version */
#define IME_FIX_MAJOR 0
#define IME_FIX_MINOR 4
#define IME_FIX_PATCH 15
#define IME_FIX_STRING "0.4.15-layout"

/*
 * Shared debug logging.
 * Writes debug.log next to ime_fix.bin in the mod folder. No C-drive or
 * game-root files. Uses only kernel32.dll + user32.dll (wsprintf), no CRT.
 */
extern HMODULE g_imefixModule;
static void ime_log(const char *tag, const char *message)
{
    WCHAR base[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR ts[64];
    SYSTEMTIME st;
    HANDLE h;
    DWORD w;
    DWORD n;
    char line[512];
    int len;
    int i, last;

    n = GetModuleFileNameW(g_imefixModule, base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return; /* cannot find the mod folder -> no log */

    last = -1;
    for (i = 0; base[i]; i++)
        if (base[i] == L'\\' || base[i] == L'/')
            last = i;
    if (last < 0)
        return;
    base[last + 1] = 0; /* keep the mod folder path */

    wsprintfW(file, L"%sdebug.log", base);

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