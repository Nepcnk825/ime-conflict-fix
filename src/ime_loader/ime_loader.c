/*
 * ime_loader.dll - Thin loader for IME Conflict Fix (v0.4 architecture)
 *
 * Architecture:
 *
 *   isaac-ng.exe → imports ime_loader.dll!IME_Init (PE patch)
 *   ime_loader.dll (game root, NEVER updates)
 *     → sets env var IME_FIX_GAME_DIR = <game root> (ime_fix.dll lives in
 *       mods now and needs to find savedatapath.txt next to the exe)
 *     → LoadLibrary("mods\ime-conflict-fix\ime_fix.bin") directly
 *
 * The functional DLL (ime_fix.bin) now lives IN the mods folder, so Steam
 * workshop updates replace it in place (no file is locked: the loader only
 * holds a reference to the loaded module, and Steam pushes updates while
 * the game is closed). The game root keeps exactly ONE file (this loader),
 * which never changes - no re-patching ever needed for mod updates.
 *
 * Why a loader at all: the exe's PE import must reference a DLL that exists
 * in the game root (Windows import search: exe dir → system dirs → PATH;
 * the mods folder is not searched). A running DLL cannot be replaced, but
 * the loader itself is never replaced.
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

/* Called once from DllMain on attach. */
static void loader_run(void)
{
    WCHAR bin[MAX_PATH];
    WCHAR self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hself, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;

    int last = path_last_slash(self);
    if (last < 0)
        return;
    self[last + 1] = 0; /* keep trailing separator = game root */

    /* Tell ime_fix.dll where the game root is: it lives in mods now and
       must resolve savedatapath.txt (next to the exe) to find log.txt.
       Set BEFORE loading - its worker reads it 2s after attach. */
    SetEnvironmentVariableW(L"IME_FIX_GAME_DIR", self);

    wsprintfW(bin, L"%smods\\ime-conflict-fix\\ime_fix.bin", self);
    if (LoadLibraryW(bin))
        loader_log("main", "ime_fix.bin loaded from mods folder");
    else
        loader_log("main", "LoadLibraryW(mods\\ime-conflict-fix\\ime_fix.bin) FAILED");
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
