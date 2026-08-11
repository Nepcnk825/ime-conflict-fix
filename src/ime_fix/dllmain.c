/*
 * ime_fix.dll - IME management for The Binding of Isaac: Repentance+
 *
 * Startup-time IME disabling based on whether the companion Lua mod is
 * enabled. Two phases:
 *
 *   Phase 1 (startup window, ~12s): the game logs "Running Lua Script:
 *   .../ime-conflict-fix/main.lua" when it executes our mod at launch. If
 *   found, IMMEDIATELY call ImmDisableIME(-1) (works at the main menu).
 *
 *   Phase 2 (after window): the mod may have been enabled mid-session
 *   (disabled at launch, toggled on in the Mods menu). Listen for:
 *     - the per-run marker "[IME_RUN_STARTED]" (MC_POST_GAME_STARTED), or
 *     - an in-game mod toggle: the mod marker re-appearing AFTER "Menu Mods
 *       Init", followed by the reload-complete signal "AnmCache: Clear".
 *       Once reload finishes the game is back at the main menu with input
 *       idle, so disabling there is safe (no UI freeze) and happens right
 *       when the user returns from the Mods page - before starting a run.
 *
 * NOTE: the 12s startup-window cap in Phase 1 is important - the game
 * re-loads all mods if the user toggles them in the Mods menu mid-session,
 * which would re-log the "Running Lua Script" marker. Calling ImmDisableIME
 * then, while the game is busy reloading mods, freezes the UI. Restricting
 * Phase-1 detection to the boot window avoids that entirely.
 *
 * NOTE: ImmDisableIME(-1) is IRREVERSIBLE - it disables the IME for the
 * whole process and cannot be re-enabled in-game. That is the accepted
 * trade-off of this "startup disable" mode (online chat loses IME too).
 *
 * Windows API only - no CRT (static /MT link, deps limited to
 * kernel32/user32/imm32).
 */

#include <windows.h>
#include <imm.h>
#include "ime_fix.h"

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "user32.lib")

#define MOD_TAG "ime-conflict-fix/main.lua" /* Lua mod entry; logged only when the mod EXECUTES */
#define RUN_TAG "[IME_RUN_STARTED]"         /* Lua mod per-run marker (MC_POST_GAME_STARTED) */
#define MODS_MENU_TAG "Menu Mods Init"      /* logged when user enters the Mods menu */
#define RELOAD_DONE_TAG "AnmCache: Clear"   /* logged when a mod reload finishes */
#define MAX_SCAN 65536             /* cap for appended log.txt scan window */
#define POLL_WINDOW_S 12           /* only poll during the game's mod-load window */
#define POLL_INTERVAL_MS 2000      /* startup-window poll interval */
#define RUN_POLL_MS 3000           /* per-run marker poll interval (after window) */

/* ====================================================================
 * Minimal string helpers (no CRT: no strlen/strcmp/strstr/strcat)
 * ==================================================================== */

static int my_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int my_strcmp(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int my_strncmp(const char *a, const char *b, int n)
{
    int i = 0;
    while (i < n && a[i] && b[i] && a[i] == b[i]) i++;
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void my_strncpy(char *dst, const char *src, int maxLen)
{
    int i = 0;
    while (src[i] && i < maxLen - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void my_strcat_bounded(char *dst, const char *src, DWORD maxLen)
{
    DWORD i = 0;
    while (dst[i] && i < maxLen - 1) i++;
    DWORD j = 0;
    while (src[j] && i + j < maxLen - 1) { dst[i + j] = src[j]; j++; }
    dst[i + j] = 0;
}

/* Build "<dir><suffix>" into out (bounded). dir may end in '/' or '\'. */
static void build_path(char *out, DWORD outSize, const char *dir,
                       const char *suffix)
{
    my_strncpy(out, dir, (int)outSize);
    my_strcat_bounded(out, suffix, outSize);
}

/* Substring search - returns TRUE if needle occurs in haystack. */
static BOOL contains(const char *haystack, const char *needle)
{
    int hl = my_strlen(haystack);
    int nl = my_strlen(needle);
    int i, j;
    if (nl == 0 || nl > hl) return FALSE;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++)
            if (haystack[i + j] != needle[j]) break;
        if (j == nl) return TRUE;
    }
    return FALSE;
}

/* ====================================================================
 * Save path resolution
 * ==================================================================== */

/* Read whole file into buf (NUL-terminated). Returns TRUE on success. */
static BOOL read_file_to_buf(const char *path, char *buf, DWORD bufSize)
{
    HANDLE h = CreateFileA(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD read = 0;
    BOOL ok;
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = ReadFile(h, buf, bufSize - 1, &read, NULL);
    CloseHandle(h);
    if (!ok)
        return FALSE;
    buf[read] = 0;
    return TRUE;
}

/* Parse the "Save Data Path: <value>" line out of savedatapath.txt. */
static BOOL parse_save_data_path(const char *cfg, char *out, DWORD outSize)
{
    static const char marker[] = "Save Data Path:";
    int mlen = my_strlen(marker);
    int i = 0;
    while (cfg[i]) {
        if (my_strncmp(cfg + i, marker, mlen) == 0) {
            const char *p = cfg + i + mlen;
            DWORD j = 0;
            while (*p == ' ') p++;
            while (*p && *p != '\r' && *p != '\n' && j < outSize - 1)
                out[j++] = *p++;
            out[j] = 0;
            while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\t'))
                out[--j] = 0;
            return j > 0;
        }
        i++;
    }
    return FALSE;
}

/* Fallback path: %USERPROFILE%\Documents\My Games\Binding of Isaac Repentance+ */
static void build_fallback_path(char *out, DWORD outSize)
{
    char profile[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH) == 0) {
        out[0] = 0;
        return;
    }
    build_path(out, outSize, profile, "\\Documents\\My Games\\Binding of Isaac Repentance+");
}

/*
 * Resolve the save data path. Reads savedatapath.txt next to this DLL
 * (found via GetModuleFileNameA) and parses the "Save Data Path:" line.
 * Falls back to the default My Games location on any failure.
 */
static void resolve_save_path(char *out, DWORD outSize)
{
    char dllPath[MAX_PATH];
    char cfgPath[MAX_PATH];
    char cfg[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    int last = -1, i;

    if (n == 0 || n >= MAX_PATH)
        goto fallback;

    for (i = 0; dllPath[i]; i++)
        if (dllPath[i] == '\\' || dllPath[i] == '/')
            last = i;
    if (last < 0)
        goto fallback;
    dllPath[last + 1] = 0; /* keep trailing separator */

    build_path(cfgPath, sizeof(cfgPath), dllPath, "savedatapath.txt");
    if (read_file_to_buf(cfgPath, cfg, sizeof(cfg)) &&
        parse_save_data_path(cfg, out, outSize) && out[0])
        return;

fallback:
    build_fallback_path(out, outSize);
}

/* ====================================================================
 * log.txt size tracking + mod-enabled detection
 * ==================================================================== */

static BOOL get_log_size(const char *savePath, DWORD *outSize)
{
    char logPath[MAX_PATH];
    HANDLE h;
    DWORD size;

    build_path(logPath, sizeof(logPath), savePath, "\\log.txt");
    h = CreateFileA(logPath, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    size = GetFileSize(h, NULL);
    CloseHandle(h);
    if (size == INVALID_FILE_SIZE)
        return FALSE;
    *outSize = size;
    return TRUE;
}

/* Scan log.txt bytes in [from_offset, end) for a marker.
   Returns TRUE when found. Only the freshly-appended part is checked, so
   previous launches' logs never cause a false positive. */
static BOOL marker_since(const char *savePath, DWORD from_offset, const char *tag)
{
    char logPath[MAX_PATH];
    char *buf;
    HANDLE h;
    DWORD size, rd = 0, len;
    BOOL ok, found;

    build_path(logPath, sizeof(logPath), savePath, "\\log.txt");
    h = CreateFileA(logPath, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size <= from_offset) {
        CloseHandle(h);
        return FALSE;
    }
    len = size - from_offset;
    if (len > MAX_SCAN) len = MAX_SCAN;

    buf = (char*)malloc(len + 1);
    if (!buf) { CloseHandle(h); return FALSE; }

    SetFilePointer(h, from_offset, NULL, FILE_BEGIN);
    ok = ReadFile(h, buf, len, &rd, NULL);
    CloseHandle(h);
    if (!ok) { free(buf); return FALSE; }
    buf[rd] = 0;

    found = contains(buf, tag);
    free(buf);
    return found;
}

static BOOL mod_loaded_since(const char *savePath, DWORD from_offset)
{
    return marker_since(savePath, from_offset, MOD_TAG);
}

static BOOL run_started_since(const char *savePath, DWORD from_offset)
{
    return marker_since(savePath, from_offset, RUN_TAG);
}

/* Find the byte offset of the FIRST occurrence of tag in log.txt at/after
   from_offset. Returns TRUE and fills *out_pos when found. */
static BOOL find_marker_pos(const char *savePath, DWORD from_offset,
                            const char *tag, DWORD *out_pos)
{
    char logPath[MAX_PATH];
    char *buf;
    HANDLE h;
    DWORD size, rd = 0, len;
    BOOL ok, found = FALSE;
    int i, j;

    build_path(logPath, sizeof(logPath), savePath, "\\log.txt");
    h = CreateFileA(logPath, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size <= from_offset) {
        CloseHandle(h);
        return FALSE;
    }
    len = size - from_offset;
    if (len > MAX_SCAN) len = MAX_SCAN;

    buf = (char*)malloc(len + 1);
    if (!buf) { CloseHandle(h); return FALSE; }

    SetFilePointer(h, from_offset, NULL, FILE_BEGIN);
    ok = ReadFile(h, buf, len, &rd, NULL);
    CloseHandle(h);
    if (!ok) { free(buf); return FALSE; }
    buf[rd] = 0;

    int nl = my_strlen(tag);
    if (nl > 0) {
        for (i = 0; i + nl <= (int)rd; i++) {
            for (j = 0; j < nl; j++)
                if (buf[i + j] != tag[j]) break;
            if (j == nl) {
                *out_pos = from_offset + (DWORD)i;
                found = TRUE;
                break;
            }
        }
    }
    free(buf);
    return found;
}

/* ====================================================================
 * Worker thread
 * ==================================================================== */

static DWORD WINAPI worker(LPVOID param)
{
    char savePath[MAX_PATH];
    char buf[1024];
    DWORD baseline = 0;
    int attempts;

    (void)param;
    Sleep(2000); /* let the game start writing log.txt */

    resolve_save_path(savePath, sizeof(savePath));
    if (savePath[0] == 0) {
        ime_log("main", "save path resolution failed - IME stays enabled");
        return 0;
    }
    wsprintfA(buf, "save path: %s", savePath);
    ime_log("main", buf);

    /* Baseline = log.txt size right now (game's own startup writes may have
       already begun, but our Lua mod is not loaded yet, so any later
       "ime-conflict-fix" mention in the appended region means it is). */
    get_log_size(savePath, &baseline);
    wsprintfA(buf, "log.txt baseline size: %d", (int)baseline);
    ime_log("main", buf);

    /* Phase 1: poll ONLY during the startup mod-load window (~12s). The
       game loads its mods within this time; disabling IME here is safe
       because the input system is idle at boot. Deliberately short: if the
       user toggles mods in-game (Mods menu), the game re-loads all mods and
       our marker appears again - calling ImmDisableIME then, while the game
       is busy reloading and processing input, freezes the UI. */
    int max_attempts = POLL_WINDOW_S * 1000 / POLL_INTERVAL_MS;
    for (attempts = 0; attempts < max_attempts; attempts++) {
        if (mod_loaded_since(savePath, baseline)) {
            ImmDisableIME(-1);
            ime_log("main", "mod enabled - IME disabled at startup");
            return 0;
        }
        Sleep(POLL_INTERVAL_MS);
    }

    /* Phase 2: mod was NOT enabled at launch (e.g. user enabled it in-game
       after the startup window). The user still needs the IME disabled once
       they actually play. Wait for one of two safe triggers:
         a) "[IME_RUN_STARTED]" - Lua mod fires it on MC_POST_GAME_STARTED,
            i.e. after the game has fully loaded a run (input idle) -> safe.
         b) An in-game mod toggle: "ime-conflict-fix/main.lua" re-appears in
            the log AFTER "Menu Mods Init" (user toggled mods in the Mods
            menu). Disabling immediately would freeze the UI mid-reload, so
            wait for "AnmCache: Clear" (reload done) plus the log to stop
            growing for ~3s (game back at main menu, input idle) -> safe. */
    ime_log("main", "mod not enabled at launch - monitoring for safe disable trigger");
    for (;;) {
        /* Trigger a): a single-player run started. */
        if (run_started_since(savePath, baseline)) {
            ImmDisableIME(-1);
            ime_log("main", "per-run marker - IME disabled at run start");
            return 0;
        }

        /* Trigger b): user toggled mods in-game (marker after Mods menu).
           Disabling IMMEDIATELY while the game is mid-reload freezes the UI,
           so wait for the reload-complete signal "AnmCache: Clear" that the
           game logs right after all mods finish loading (before any user
           action like starting a run). At that point the game is back at the
           main menu with the input system idle - safe to disable, and fast
           enough that starting a run right away still gets IME off first. */
        DWORD menu_pos = 0;
        BOOL in_menu = find_marker_pos(savePath, baseline, MODS_MENU_TAG, &menu_pos);
        if (in_menu) {
            DWORD mod_pos = 0;
            if (find_marker_pos(savePath, menu_pos, MOD_TAG, &mod_pos)) {
                ime_log("main", "mod toggled in-game - waiting for reload to finish");
                for (;;) {
                    Sleep(500);
                    if (run_started_since(savePath, baseline)) {
                        ImmDisableIME(-1);
                        ime_log("main", "per-run marker - IME disabled at run start");
                        return 0;
                    }
                    if (marker_since(savePath, mod_pos, RELOAD_DONE_TAG)) {
                        ImmDisableIME(-1);
                        ime_log("main", "mod toggled in-game - IME disabled after reload finished");
                        return 0;
                    }
                }
            }
        }
        Sleep(RUN_POLL_MS);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x)
{
    (void)x;
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        ime_log("main", "ime_fix.dll v" IME_FIX_STRING " loaded (startup-disable mode)");
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    return TRUE;
}

__declspec(dllexport) void IME_Init(void) {}
