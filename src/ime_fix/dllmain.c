/*
 * ime_fix.dll - IME management for The Binding of Isaac: Repentance+
 *
 * Startup-time IME disabling based on whether the companion Lua mod is
 * enabled. When the mod is enabled, the game executes its main.lua at
 * startup and logs "Running Lua Script: .../ime-conflict-fix/main.lua"
 * in log.txt. NOTE: "LOADED MOD .../ime-conflict-fix/..." is NOT a
 * reliable signal - the game enumerates mod folders even when mods are
 * disabled (EnableMods=0), but only logs "Running Lua Script" for mods
 * that actually execute. So this DLL:
 *
 *   1. records the log.txt size at process start (baseline),
 *   2. waits for the game to load its mods,
 *   3. scans the NEWLY-APPENDED log.txt content for "ime-conflict-fix/main.lua";
 *      if found, the Lua mod executed this launch -> IMMEDIATELY call
 *      ImmDisableIME(-1) (works at the main menu, not only in a run).
 *   4. if not found within the ~12s startup window (mod disabled or
 *      EnableMods=0), do nothing.
 *
 * The 12s cap is important: the game re-loads all mods if the user toggles
 * them in the Mods menu mid-session, which would re-log our marker. Calling
 * ImmDisableIME then, while the game is busy reloading mods, freezes the UI.
 * Restricting detection to the boot window avoids that entirely.
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
#define MAX_SCAN 65536             /* cap for appended log.txt scan window */
#define POLL_WINDOW_S 12           /* only poll during the game's mod-load window */
#define POLL_INTERVAL_MS 2000      /* seconds between scans */

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

/* Scan log.txt bytes in [from_offset, end) for our Lua mod's load marker.
   Returns TRUE when found. Only the freshly-appended part is checked, so
   previous launches' logs never cause a false positive. */
static BOOL mod_loaded_since(const char *savePath, DWORD from_offset)
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

    found = contains(buf, MOD_TAG);
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

    /* Poll ONLY during the startup mod-load window (~12s). The game loads
       its mods within this time; disabling IME here is safe because the
       input system is idle at boot. Deliberately NOT polling for 30s: if
       the user toggles mods in-game (Mods menu), the game re-loads all mods
       and our marker appears again - calling ImmDisableIME then, while the
       game is busy reloading and processing input, freezes the UI. */
    int max_attempts = POLL_WINDOW_S * 1000 / POLL_INTERVAL_MS;
    for (attempts = 0; attempts < max_attempts; attempts++) {
        if (mod_loaded_since(savePath, baseline)) {
            ImmDisableIME(-1);
            ime_log("main", "mod enabled - IME disabled at startup");
            return 0;
        }
        Sleep(POLL_INTERVAL_MS);
    }

    ime_log("main", "mod not detected in startup window - IME stays enabled");
    return 0;
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
