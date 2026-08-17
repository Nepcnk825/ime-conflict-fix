/*
 * ime_loader.h - Loader for IME Conflict Fix
 * Version defines and shared logging utility (same pattern as ime_fix.h).
 */

#ifndef IME_LOADER_H
#define IME_LOADER_H

#include <windows.h>

/* Version */
#define IME_LOADER_MAJOR 0
#define IME_LOADER_MINOR 2
#define IME_LOADER_PATCH 4
#define IME_LOADER_STRING "0.2.4"

/*
 * Shared debug logging.
 * Writes debug.log into the detected mod folder (the folder containing
 * ime_fix.bin). If no mod folder is found yet, fall back to host exe dir.
 * Never falls back to %APPDATA%.
 */
extern WCHAR g_logDir[MAX_PATH];

static void loader_log(const char *tag, const char *message)
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

    if (g_logDir[0]) {
        wsprintfW(file, L"%sdebug.log", g_logDir);
    } else {
        n = GetModuleFileNameW(NULL, base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return; /* no path -> no log */
        last = -1;
        for (i = 0; base[i]; i++)
            if (base[i] == L'\\' || base[i] == L'/')
                last = i;
        if (last < 0)
            return;
        base[last + 1] = 0;
        wsprintfW(file, L"%sdebug.log", base);
    }

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