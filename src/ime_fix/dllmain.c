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

/* Lua mod execution signal. The game logs
   "Running Lua Script: .../mods/<folder>/main.lua" ONLY when the mod actually
   executes. The folder may be the manual name "ime-conflict-fix" or the
   Steam Workshop name "ime-conflict-fix_<workshop id>", so we match the
   folder prefix ("ime-conflict-fix") FOLLOWED (within a small window) by
   "/main.lua". A plain prefix match would false-positive on the "LOADED MOD"
   enumeration line (printed even when the mod is disabled). */
#define MOD_DIR_TAG "ime-conflict-fix"  /* folder prefix, both namings */
#define MOD_FILE_TAG "/main.lua"        /* proves execution (not just enumeration) */
#define MOD_GAP_MAX 64                  /* max chars between the two tags */
/* Reload-active signature: the game prints "AnmCache: cannot remove
   reference to ..." lines WHILE reloading mods (verified in real logs) and
   stops when done. Used as the reload-complete signal - waiting for the
   whole log to go quiet is wrong (menu-init logs keep flowing) and too
   short a quiet window freezes the UI mid-reload. */
#define RELOAD_ACTIVE_TAG "AnmCache: cannot remove reference"
#define RUN_TAG "[IME_RUN_STARTED]"         /* Lua mod per-run marker (MC_POST_GAME_STARTED) */
#define MAX_SCAN 262144            /* cap for appended log.txt scan window (256KB) */
#define POLL_WINDOW_S 12           /* only poll during the game's mod-load window */
#define POLL_INTERVAL_MS 2000      /* startup-window poll interval */
#define RUN_POLL_MS 500            /* Phase 2 poll interval: fast marker discovery (~1s to disable after toggle) */

/* NOTE: this DLL is loaded from the mods folder by ime_loader.dll. It never
   resolves its own path for configuration - the loader sets the
   IME_FIX_GAME_DIR environment variable (game root, where savedatapath.txt
   and log.txt live) before loading us. */
#define GAME_DIR_ENV "IME_FIX_GAME_DIR"

/* ====================================================================
 * Minimal string helpers (no CRT: no strlen/strcmp/strstr/strcat)
 * ==================================================================== */

static int my_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
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

/* Paired substring search: TRUE if needle1 occurs AND needle2 occurs within
   max_gap characters AFTER that occurrence. Used for the mod-execution
   signal (folder prefix + "/main.lua") to avoid false positives on the
   "LOADED MOD" enumeration line which contains the folder name but not
   "/main.lua". ALL occurrences of needle1 are tried (the enumeration line
   may precede the real execution line in the buffer). */
static BOOL contains_pair(const char *haystack, const char *needle1,
                          const char *needle2, int max_gap)
{
    int hl = my_strlen(haystack);
    int l1 = my_strlen(needle1);
    int l2 = my_strlen(needle2);
    int i, j;
    for (i = 0; i + l1 <= hl; i++) {
        for (j = 0; j < l1; j++)
            if (haystack[i + j] != needle1[j]) break;
        if (j == l1) {
            /* found needle1: look for needle2 in the next max_gap chars */
            int end = i + l1 + max_gap;
            int k;
            if (end > hl) end = hl;
            for (k = i + l1; k + l2 <= end; k++) {
                for (j = 0; j < l2; j++)
                    if (haystack[k + j] != needle2[j]) break;
                if (j == l2)
                    return TRUE;
            }
            /* no needle2 after THIS occurrence - keep scanning for the next */
        }
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
 * Resolve the save data path. Preferred source: the IME_FIX_GAME_DIR
 * environment variable set by ime_loader.dll (the game root - this DLL
 * lives in the mods folder and cannot see savedatapath.txt next to
 * itself). Reads and parses the "Save Data Path:" line from
 * savedatapath.txt, falls back to the default My Games location.
 */
static void resolve_save_path(char *out, DWORD outSize)
{
    char gameDir[MAX_PATH];
    char cfgPath[MAX_PATH];
    char cfg[MAX_PATH * 2];

    if (GetEnvironmentVariableA(GAME_DIR_ENV, gameDir, MAX_PATH) > 0) {
        build_path(cfgPath, sizeof(cfgPath), gameDir, "\\savedatapath.txt");
        if (read_file_to_buf(cfgPath, cfg, sizeof(cfg)) &&
            parse_save_data_path(cfg, out, outSize) && out[0])
            return;
    }
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

/* Read the log.txt range [from_offset, end) into a NUL-terminated buffer.
   Returns malloc'd buffer (caller frees) or NULL on any failure.
   The game appends to log.txt while we read, so use FILE_SHARE_WRITE. */
static char *read_log_range(const char *savePath, DWORD from_offset, DWORD *out_len)
{
    char logPath[MAX_PATH];
    char *buf;
    HANDLE h;
    DWORD size, rd = 0, len;

    build_path(logPath, sizeof(logPath), savePath, "\\log.txt");
    h = CreateFileA(logPath, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size <= from_offset) {
        CloseHandle(h);
        return NULL;
    }
    len = size - from_offset;
    if (len > MAX_SCAN) len = MAX_SCAN;

    buf = (char*)malloc(len + 1);
    if (!buf) { CloseHandle(h); return NULL; }

    SetFilePointer(h, from_offset, NULL, FILE_BEGIN);
    if (!ReadFile(h, buf, len, &rd, NULL)) {
        CloseHandle(h);
        free(buf);
        return NULL;
    }
    CloseHandle(h);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

/* Scan log.txt bytes in [from_offset, end) for a marker.
   Returns TRUE when found. Only the freshly-appended part is checked, so
   previous launches' logs never cause a false positive. */
static BOOL marker_since(const char *savePath, DWORD from_offset, const char *tag)
{
    DWORD rd = 0;
    char *buf = read_log_range(savePath, from_offset, &rd);
    BOOL found;
    if (!buf)
        return FALSE;
    found = contains(buf, tag);
    free(buf);
    return found;
}

static BOOL mod_loaded_since(const char *savePath, DWORD from_offset)
{
    DWORD rd = 0;
    char *buf = read_log_range(savePath, from_offset, &rd);
    BOOL found;
    if (!buf)
        return FALSE;
    found = contains_pair(buf, MOD_DIR_TAG, MOD_FILE_TAG, MOD_GAP_MAX);
    free(buf);
    return found;
}

static BOOL run_started_since(const char *savePath, DWORD from_offset)
{
    return marker_since(savePath, from_offset, RUN_TAG);
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
    BOOL waiting_reload = FALSE; /* a mod-execution marker was seen; waiting for reload to settle */
    for (;;) {
        /* Trigger a): a single-player run started. */
        if (run_started_since(savePath, baseline)) {
            ImmDisableIME(-1);
            ime_log("main", "per-run marker - IME disabled at run start");
            return 0;
        }

        /* Trigger b): the mod executed AFTER the startup window - the user
           must have toggled it on in the Mods menu (the game reloads all
           mods, re-running main.lua and re-logging "Running Lua Script").
           Disabling IMMEDIATELY while the game is mid-reload freezes the UI,
           so wait for the log to stop growing (reload finished, game idle at
           the main menu) - no text signal for "reload done" exists in the
           real game log, hence the stability proxy. If a run starts during
           the wait, trigger a) fires instead (equally safe). */
        if (!waiting_reload && mod_loaded_since(savePath, baseline)) {
            ime_log("main", "mod toggled in-game - waiting for reload to finish");
            waiting_reload = TRUE;
        }
        if (waiting_reload) {
            /* Reload-done detection via the reload-active signature lines.
               The game prints "AnmCache: cannot remove reference ..." while
               reloading mods and stops when done. We disable as soon as no
               new signature line appears for one 500ms window (after a 1s
               grace period that lets the signature start). A run starting
               during the wait fires trigger a) instead. 30s hard cap. */
            DWORD scan_from = 0;
            DWORD size = 0;
            DWORD t0 = GetTickCount();
            if (!get_log_size(savePath, &scan_from))
                scan_from = baseline;
            for (;;) {
                Sleep(500);
                if (run_started_since(savePath, baseline)) {
                    ImmDisableIME(-1);
                    ime_log("main", "per-run marker - IME disabled at run start");
                    return 0;
                }
                if (!get_log_size(savePath, &size))
                    break;
                if (marker_since(savePath, scan_from, RELOAD_ACTIVE_TAG)) {
                    /* reload still in progress */
                    scan_from = size;
                    if (GetTickCount() - t0 > 30000)
                        break; /* 30s cap - give up this attempt */
                    continue;
                }
                if (GetTickCount() - t0 < 1000)
                    continue; /* 1s grace before concluding reload is done */
                ImmDisableIME(-1);
                ime_log("main", "mod toggled in-game - IME disabled after reload finished");
                return 0;
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