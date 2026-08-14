/*
 * ime_loader.dll - Loader/updater for IME Conflict Fix
 *
 * Architecture (mirrors the official CN patch's bootstp.dll + inject.dll):
 *
 *   isaac-ng.exe → imports ime_loader.dll!IME_Init (PE patch)
 *   ime_loader.dll (this file, rarely updated)
 *     → on load: compares  <game>\mods\ime-conflict-fix\ime_fix.bin
 *                 vs        <game>\ime_fix.dll
 *       if .bin is newer/different → copy it over ime_fix.dll
 *       (safe: ime_fix.dll is NOT loaded yet - we load it AFTER the copy)
 *     → LoadLibrary("ime_fix.dll") → runs the actual IME logic (latest version)
 *
 * Why two layers: a running DLL cannot overwrite itself (sharing violation,
 * error 32). By keeping the loader tiny and stable, the functional DLL
 * (ime_fix.dll, updated via Steam workshop as ime_fix.bin) can always be
 * replaced at startup before being loaded. The loader itself rarely changes,
 * so it never needs self-updating.
 *
 * Windows API only - no CRT (static /MT link). Deps: user32 (wsprintfW lives
 * in user32, NOT kernel32 - easy to get wrong), kernel32.
 */

#include <windows.h>
#include "ime_loader.h"

/* Our own module handle (set in DllMain). Used to resolve the game dir:
   GetModuleFileNameW(hModule) returns ime_loader.dll's own path, NOT the
   host exe's path - the host might live elsewhere (e.g. rundll32 tests). */
static HMODULE g_hself = NULL;

/* Find the last '\' or '/' in a path, return its index or -1. */
static int path_last_slash(const WCHAR *p)
{
    int last = -1, i;
    for (i = 0; p[i]; i++)
        if (p[i] == L'\\' || p[i] == L'/')
            last = i;
    return last;
}

/* Compare two files by size + last-write time.
   Returns 1 if src differs from dst (needs update), 0 if same, -1 on error. */
static int file_differs(const WCHAR *src, const WCHAR *dst)
{
    WIN32_FILE_ATTRIBUTE_DATA a, b;
    if (!GetFileAttributesExW(src, GetFileExInfoStandard, &a))
        return -1;   /* src missing - nothing to sync */
    if (!GetFileAttributesExW(dst, GetFileExInfoStandard, &b))
        return 1;    /* dst missing - needs copy */

    if (a.nFileSizeHigh != b.nFileSizeHigh || a.nFileSizeLow != b.nFileSizeLow)
        return 1;
    if (CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) != 0)
        return 1;
    return 0;
}

/* Build "<game_dir>\mods\ime-conflict-fix\ime_fix.bin" into out.
   game_dir comes from our own module path (we sit next to isaac-ng.exe). */
static void build_bin_path(WCHAR *out, DWORD out_size)
{
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hself, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { out[0] = 0; return; }

    int last = path_last_slash(self);
    if (last < 0) { out[0] = 0; return; }
    self[last + 1] = 0;  /* keep trailing separator */

    wsprintfW(out, L"%smods\\ime-conflict-fix\\ime_fix.bin", self);
}

/* Sync ime_fix.bin (workshop-updated) over the game dir's ime_fix.dll,
   then load the functional DLL. Called once from DllMain on attach. */
static void loader_run(void)
{
    WCHAR bin[MAX_PATH];
    WCHAR self[MAX_PATH];
    WCHAR dll[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hself, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;

    int last = path_last_slash(self);
    if (last < 0) return;
    self[last + 1] = 0;

    build_bin_path(bin, sizeof(bin));
    wsprintfW(dll, L"%sime_fix.dll", self);

    if (bin[0]) {
        int d = file_differs(bin, dll);
        if (d == 1) {
            /* ime_fix.dll is NOT loaded yet - safe to overwrite */
            if (CopyFileW(bin, dll, FALSE))
                loader_log("sync", "ime_fix.bin -> ime_fix.dll updated");
            else
                loader_log("sync", "copy ime_fix.bin failed");
        } else if (d == 0) {
            loader_log("sync", "ime_fix.dll up to date");
        } else {
            loader_log("sync", "ime_fix.bin not found - no auto-update");
        }
    }

    /* Load the (possibly updated) functional DLL */
    if (LoadLibraryW(dll))
        loader_log("main", "ime_fix.dll loaded");
    else
        loader_log("main", "LoadLibraryW(ime_fix.dll) FAILED");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_hself = h;
        loader_log("main", "ime_loader.dll v" IME_LOADER_STRING " loaded");
        loader_run();
    }
    return TRUE;
}

__declspec(dllexport) void IME_Init(void) {}
