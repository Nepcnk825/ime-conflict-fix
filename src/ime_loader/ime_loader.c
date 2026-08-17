/*
 * ime_loader.dll - Thin loader for IME Conflict Fix (v0.4 architecture)
 *
 * Architecture:
 *
 *   isaac-ng.exe → imports ime_loader.dll!IME_Init (PE patch)
 *   ime_loader.dll (game root, NEVER updates)
 *     → sets env var IME_FIX_GAME_DIR = <game root> (ime_fix.dll lives in
 *       mods now and needs to find savedatapath.txt next to the exe)
 *     → finds the mod folder (prefix "ime-conflict-fix") and
 *       LoadLibrary("<mods>/<folder>/ime_fix.bin") directly
 *
 * Folder-name note: Steam Workshop downloads mods as "<directory>_<id>",
 * e.g. ime-conflict-fix_3783304248 (all subscribed mods look like this:
 * cn_rep+_3568677664 ...). Local/manual installs may use the bare
 * "ime-conflict-fix" name. The loader therefore ENUMERATES mods\ and picks
 * the first directory whose name starts with "ime-conflict-fix" that
 * contains ime_fix.bin - both layouts work.
 *
 * The functional DLL (ime_fix.bin) lives IN the mods folder, so Steam
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
WCHAR g_logDir[MAX_PATH] = {0};

#define MOD_DIR_PREFIX L"ime-conflict-fix"  /* matches bare name and Steam's <name>_<id> */

/* Find the last '\' or '/' in a path, return its index or -1. */
static int path_last_slash(const WCHAR *p)
{
    int last = -1, i;
    for (i = 0; p[i]; i++)
        if (p[i] == L'\\' || p[i] == L'/')
            last = i;
    return last;
}

/* Match a mods-subdirectory name: starts with "ime-conflict-fix" (Steam
   appends _<workshop id>, manual installs don't). */
static BOOL dir_name_matches(const WCHAR *name)
{
    int i = 0;
    while (MOD_DIR_PREFIX[i] && name[i] == MOD_DIR_PREFIX[i])
        i++;
    if (MOD_DIR_PREFIX[i] != 0)
        return FALSE; /* prefix not fully matched */
    /* next char must be end-of-string or a separator like '_' */
    return name[i] == 0 || name[i] == L'_' || name[i] == L'-';
}

/* Find "<game root>\mods\<matching folder>\ime_fix.bin".
   Enumerates mods\, prefers a folder whose ime_fix.bin exists.
   Returns TRUE and fills out on success. */
static BOOL find_bin_path(const WCHAR *game_root, WCHAR *out, DWORD out_size)
{
    WCHAR pattern[MAX_PATH];
    WCHAR folder[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    WCHAR first_match[MAX_PATH];
    BOOL have_first = FALSE;

    wsprintfW(pattern, L"%smods\\*", game_root);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (fd.cFileName[0] == L'.')
            continue;
        if (!dir_name_matches(fd.cFileName))
            continue;
        wsprintfW(folder, L"%smods\\%s\\ime_fix.bin", game_root, fd.cFileName);
        if (!have_first) {
            /* remember the first name match in case none has the bin */
            wsprintfW(first_match, L"%s", folder);
            have_first = TRUE;
        }
        if (GetFileAttributesW(folder) != INVALID_FILE_ATTRIBUTES) {
            wsprintfW(out, L"%s", folder);
            FindClose(h);
            return TRUE;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (have_first) {
        wsprintfW(out, L"%s", first_match);
        return TRUE;
    }
    return FALSE;
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

    if (find_bin_path(self, bin, sizeof(bin))) {
        int last = path_last_slash(bin);
        int i;
        if (last >= 0) {
            for (i = 0; i <= last; i++)
                g_logDir[i] = bin[i];
            g_logDir[last + 1] = 0;
        }
        if (LoadLibraryW(bin))
            loader_log("main", "ime_fix.bin loaded from mods folder");
        else
            loader_log("main", "LoadLibraryW(ime_fix.bin) FAILED");
    } else {
        loader_log("main", "mods folder not found (no ime-conflict-fix* dir)");
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_hself = h;
        loader_run();
        loader_log("main", "ime_loader.dll v" IME_LOADER_STRING " loaded");
    }
    return TRUE;
}

__declspec(dllexport) void IME_Init(void) {}