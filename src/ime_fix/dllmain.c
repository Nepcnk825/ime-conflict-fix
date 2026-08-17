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
 *   (disabled at launch, toggled on in the Mods menu). Disabling while the
 *   game reloads mods or initializes the main menu freezes the UI, so we
 *   only listen for two empirically safe triggers:
 *     - the per-run marker "[IME_RUN_STARTED]" (MC_POST_GAME_STARTED), or
 *     - the idle marker "[IME_IDLE]" (no player input for 3 seconds).
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
#pragma comment(lib, "winmm.lib")

/* timeBeginPeriod/timeEndPeriod force the Windows scheduler tick to 1ms.
   Declared manually so this no-CRT DLL does not need mmsystem.h. */
__declspec(dllimport) UINT WINAPI timeBeginPeriod(UINT uPeriod);
__declspec(dllimport) UINT WINAPI timeEndPeriod(UINT uPeriod);

/* Experimental fast path. Log analysis (2026-08-16) showed that
   "AnmCache: Clear" is followed by GameState loading and even game shutdown;
   scheduling ImmDisableIME there freezes the UI despite the main-thread
   timer. Keep the code behind this switch, but leave it OFF by default. */
#define ENABLE_RELOAD_FASTPATH 0

/* Passive diagnostic build: after AnmCache: Clear, do NOT call
   ImmDisableIME. Sample main-thread responsiveness, GUI flags, log growth
   and the F12 marker key for DIAG_DURATION_MS. */
#define ENABLE_DIAGNOSTIC 0

/* Reversible alternative to ImmDisableIME(-1): ask the game window to switch
   its input language to English (US). This is asynchronous via
   WM_INPUTLANGCHANGEREQUEST and DefWindowProc, so it does not synchronize
   with a busy UI thread and can be requested at any time. */
#define IME_FIX_MODE_LAYOUT_SWITCH 1

/* v0.4.2 test proved LoadKeyboardLayout adds layouts to the user profile.
   v0.4.3-test and later no longer load any layout: only existing layouts are
   used. */
#define LAYOUT_TEST_GERMAN_FIRST 0

/* Probe whether Microsoft Pinyin can be switched to English mode via IMM32. */
#define ENABLE_PINYIN_ENGLISH_PROBE 0
#define DIAG_SAMPLE_MS 200
#define DIAG_DURATION_MS 30000
#define DIAG_MARKER_VK VK_F12

/* Heap-backed allocation keeps this DLL free of CRT imports (important for
   the no-runtime-dependency loader architecture and MinGW test builds). */
static void *my_alloc(DWORD bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void my_free(void *ptr)
{
    if (ptr)
        HeapFree(GetProcessHeap(), 0, ptr);
}

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
/* Phase 2 safe-trigger markers (emitted by main.lua via Isaac.DebugString). */
#define RUN_TAG "[IME_RUN_STARTED]"        /* Lua: MC_POST_GAME_STARTED - verified freeze-free */
#define IDLE_TAG "[IME_IDLE]"              /* Lua: player had no input for 3s - quiescent, safe */
#define CLEAR_TAG "AnmCache: Clear"        /* game's mod-reload completion signal */
#define ONLINE_TAG_CREATE "Creating friend lobby"          /* online session started */
#define ONLINE_TAG_JOINED "Successfully joined lobby"      /* joined existing lobby */
#define ONLINE_LEAVE_TAG "Leaving current lobby"           /* lobby left/closed */
#define ONLINE_RUN_TAG "Start Networked"                   /* networked run actually began */
#define ONLINE_CHAT_TAG "[IME_ONLINE_CHAT:"                /* Lua/MCM experimental toggle */
#define ONLINE_CHAT_SENT_TAG "Broadcasting chat message"   /* game actually sent a chat message */
#define MAX_SCAN 262144            /* max bytes read per log.txt poll (256KB) */
#define LOG_OVERLAP 128            /* re-scan tail so markers split across polls aren't lost */
#define POLL_WINDOW_S 12           /* only poll during the game's mod-load window */
#define POLL_INTERVAL_MS 1         /* Phase 1 marker polling: 1ms (user-requested) */
#define RUN_POLL_MS 1              /* Phase 2 marker polling: 1ms (user-requested) */
#define WORKER_START_DELAY_MS 2000 /* wait for the game to reset log.txt; baseline safety */
#define MONITOR_FAST_MS 1          /* foreground/Enter/layout poll: 1ms */
#define MONITOR_SLOW_MS 50         /* log-scan / state machine / chat-close poll */
#define LAYOUT_REPOST_MS 50        /* don't spam WM_INPUTLANGCHANGEREQUEST faster than this */
#define CHAT_CLOSE_DELAY_MS 150    /* after chat broadcast, let the game close its input box */

/* Experimental fast path: after a mid-session mod reload is complete, ask the
   game's MAIN UI thread to execute ImmDisableIME from a WM_TIMER callback.
   The previous freezes happened when the worker thread called ImmDisableIME
   while the main thread was busy; executing at the main thread's own message
   boundary avoids that cross-thread synchronization. */
#define IME_TIMER_ID 0x1F1C
#define RELOAD_TIMER_DELAY_MS 50

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

/* Return the byte index of the LAST occurrence of needle, or -1.
   Used for state transitions that can appear close together in one log
   increment (leave lobby -> create new lobby). */
static int last_index_of(const char *haystack, const char *needle)
{
    int hl = my_strlen(haystack);
    int nl = my_strlen(needle);
    int i, j;
    int last = -1;

    if (nl == 0 || nl > hl)
        return -1;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++)
            if (haystack[i + j] != needle[j])
                break;
        if (j == nl)
            last = i;
    }
    return last;
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
   Returns heap buffer (caller frees with my_free) or NULL on any failure.
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

    buf = (char*)my_alloc(len + 1);
    if (!buf) { CloseHandle(h); return NULL; }

    SetFilePointer(h, from_offset, NULL, FILE_BEGIN);
    if (!ReadFile(h, buf, len, &rd, NULL)) {
        CloseHandle(h);
        my_free(buf);
        return NULL;
    }
    CloseHandle(h);
    buf[rd] = 0;
    if (out_len) *out_len = rd;
    return buf;
}

/* Phase 2 rolling reader. *io_offset tracks where the previous poll ended.
   On success *io_offset advances to (current size - LOG_OVERLAP), so the
   tail is re-scanned on the next poll and a marker split across two polls
   cannot be lost. If the log grew more than MAX_SCAN since the last poll,
   only the most recent MAX_SCAN bytes are inspected (long sessions keep
   working instead of permanently missing markers after the first 256KB). */
static char *read_log_increment(const char *savePath, DWORD *io_offset,
                                  DWORD *out_len, DWORD *out_start)
{
    DWORD from_offset = *io_offset;
    DWORD size = 0;
    DWORD start = from_offset;
    DWORD next = 0;
    DWORD rd = 0;
    char *buf;
    BOOL skipped = FALSE;

    if (!get_log_size(savePath, &size)) {
        /* log.txt is not readable yet (early boot, or transient failure). */
        return NULL;
    }

    if (size <= from_offset) {
        *io_offset = size;
        return NULL;
    }

    /* If the log grew more than MAX_SCAN between polls, inspect the most
       recent MAX_SCAN bytes instead of the stale leading chunk. */
    if (size - from_offset > MAX_SCAN) {
        start = size - MAX_SCAN;
        skipped = TRUE;
    }

    buf = read_log_range(savePath, start, &rd);
    if (!buf) {
        *io_offset = size;
        return NULL;
    }

    /* Keep a small overlap so a marker written across a poll boundary is
       found by the next poll. When old bytes were skipped, overlap the
       SKIP boundary (not just the file tail) for the same reason. */
    if (skipped)
        next = (start > LOG_OVERLAP) ? start - LOG_OVERLAP : 0;
    else if (size > LOG_OVERLAP)
        next = size - LOG_OVERLAP;
    else
        next = 0;

    *io_offset = next;
    if (out_len) *out_len = rd;
    if (out_start) *out_start = start;
    return buf;
}

static volatile LONG g_imeDisabled = 0;
static HINSTANCE g_hself = NULL;
HMODULE g_imefixModule = NULL;
static volatile LONG g_monitorStop = 0;
static BOOL g_lockLayout = FALSE;          /* effective: should the monitor keep English */
static BOOL g_lockLayoutBase = FALSE;       /* user's lock_layout setting (INI/MCM) */
static BOOL g_online = FALSE;
static BOOL g_onlineForce = FALSE;
static BOOL g_onlineChatToggle = FALSE;
static BOOL g_chatChineseMode = FALSE;
static BOOL g_onlineLockApplied = FALSE;    /* online-force override currently active */
static BOOL g_onlineRunSeen = FALSE;        /* "Start Networked" already processed */
static DWORD g_lastOnlineRunPos = 0;        /* absolute log position of that marker */
static BOOL g_lobbySeen = FALSE;            /* lobby marker already processed */
static DWORD g_lastLobbyPos = 0;            /* absolute log position of that marker */
static DWORD g_chatRequestTick = 0;         /* when Enter requested the Chinese layout */
static BOOL g_chatClosePending = FALSE;      /* chat broadcast seen, waiting to restore EN */
static DWORD g_chatCloseTick = 0;            /* tick when the close delay started */
static DWORD g_lastChatSentPos = 0;          /* absolute log position of last broadcast handled */
static HKL g_chineseLayout = NULL;
static BOOL g_chineseLayoutTried = FALSE;

/* Read an integer INI value. Supports UTF-16LE (BOM) and ANSI/UTF-8. */
static int read_ini_int(const WCHAR *ini, const WCHAR *sectionW,
                        const WCHAR *keyW)
{
    HANDLE h;
    DWORD rd = 0;
    BYTE bom[2] = {0, 0};
    BOOL utf16 = FALSE;

    h = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(h, bom, 2, &rd, NULL);
    CloseHandle(h);
    if (rd == 2 && bom[0] == 0xFF && bom[1] == 0xFE)
        utf16 = TRUE;

    if (utf16)
        return GetPrivateProfileIntW(sectionW, keyW, 0, ini);

    {
        char iniA[MAX_PATH];
        char sectionA[64];
        char keyA[64];
        if (WideCharToMultiByte(CP_ACP, 0, ini, -1, iniA, MAX_PATH,
                                NULL, NULL) > 0 &&
            WideCharToMultiByte(CP_ACP, 0, sectionW, -1, sectionA,
                                sizeof(sectionA), NULL, NULL) > 0 &&
            WideCharToMultiByte(CP_ACP, 0, keyW, -1, keyA,
                                sizeof(keyA), NULL, NULL) > 0)
            return GetPrivateProfileIntA(sectionA, keyA, 0, iniA);
    }
    return 0;
}

static int read_lock_layout_setting(const WCHAR *ini)
{
    return read_ini_int(ini, L"input", L"lock_layout");
}

static int read_online_force_setting(const WCHAR *ini)
{
    return read_ini_int(ini, L"online", L"enabled");
}

static int read_online_chat_setting(const WCHAR *ini)
{
    return read_ini_int(ini, L"online", L"chat_toggle");
}

/* Build the settings.ini path next to ime_fix.bin. */
static BOOL build_mod_settings_path(WCHAR *out, DWORD outSize)
{
    static const WCHAR suffix[] = L"settings.ini";
    DWORD n;
    DWORD len;
    int i, last;

    if (!g_imefixModule)
        return FALSE;
    n = GetModuleFileNameW(g_imefixModule, out, outSize);
    if (n == 0 || n >= outSize)
        return FALSE;
    last = -1;
    for (i = 0; out[i]; i++)
        if (out[i] == L'\\' || out[i] == L'/')
            last = i;
    if (last < 0)
        return FALSE;
    out[last + 1] = 0;
    len = (DWORD)(last + 1);
    for (i = 0; suffix[i] && len + 1 < outSize; i++, len++)
        out[len] = suffix[i];
    out[len] = 0;
    return TRUE;
}

/* Persist online settings to mod-folder settings.ini so the DLL can still
   read them when the Lua mod is disabled in online multiplayer. */
static void persist_online_settings(void)
{
    WCHAR ini[MAX_PATH];
    BOOL exists;

    if (!build_mod_settings_path(ini, MAX_PATH))
        return;

    exists = (GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES);

    /* Keep the mod-folder structure clean: do not create settings.ini just
       to store two zeroes. The file is only materialized after MCM actually
       enables an online option. If it already exists (created earlier), keep
       it updated so turning an option back off is also persisted. */
    if (!g_onlineForce && !g_onlineChatToggle && !exists)
        return;

    WritePrivateProfileStringW(L"online", L"enabled",
                               g_onlineForce ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"online", L"chat_toggle",
                               g_onlineChatToggle ? L"1" : L"0", ini);
}

/* Load settings from settings.ini next to ime_fix.bin only. No APPDATA
   fallback: settings are meant to live in the mod folder. */
static void load_settings(void)
{
    WCHAR ini[MAX_PATH];
    char msg[160];
    const char *source = "none";

    if (build_mod_settings_path(ini, MAX_PATH) &&
        GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES) {
        g_lockLayoutBase = (read_lock_layout_setting(ini) != 0);
        g_lockLayout = g_lockLayoutBase;
        g_onlineForce = (read_online_force_setting(ini) != 0);
        g_onlineChatToggle = (read_online_chat_setting(ini) != 0);
        source = "mod";

        /* User-requested: when online_force is enabled, English must be
           active from the main menu even if mods are disabled at launch.
           The resident monitor applies this as soon as the game window is
           foreground. A later Lua lock_layout=0 signal may release it. */
        if (g_onlineForce) {
            g_lockLayout = TRUE;
            g_onlineLockApplied = TRUE;
        }
    }

    wsprintfA(msg, "settings source=%s lock=%d online_force=%d chat_toggle=%d",
              source,
              g_lockLayout ? 1 : 0,
              g_onlineForce ? 1 : 0,
              g_onlineChatToggle ? 1 : 0);
    ime_log("layout", msg);
    if (g_onlineForce)
        ime_log("online", "settings online_force=1 - English will be kept from startup");
}

typedef struct {
    DWORD pid;
    HWND best;
    LONG bestArea;
} MAIN_WINDOW_SEARCH;

static BOOL CALLBACK find_main_window_proc(HWND hwnd, LPARAM lParam)
{
    MAIN_WINDOW_SEARCH *search = (MAIN_WINDOW_SEARCH*)lParam;
    DWORD pid = 0;
    RECT rc;
    LONG area;

    if (!IsWindowVisible(hwnd))
        return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid)
        return TRUE;
    if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
        return TRUE;

    if (!GetWindowRect(hwnd, &rc))
        return TRUE;
    area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (!search->best || area > search->bestArea) {
        search->best = hwnd;
        search->bestArea = area;
    }
    return TRUE;
}

/* Return TRUE when hwnd is a visible, non-tool top-level window owned by
   the current process. */
static BOOL is_our_main_window(HWND hwnd)
{
    DWORD pid = 0;

    if (!hwnd || !IsWindowVisible(hwnd))
        return FALSE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId())
        return FALSE;
    return (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
}

/* Pick the game window that should receive WM_INPUTLANGCHANGEREQUEST.
   Prefer the actual foreground window when it belongs to the game process
   (online lobby/chat may use a different top-level than the largest one).
   Fall back to the largest visible top-level window owned by this process. */
static HWND find_main_window(void)
{
    HWND fg = GetForegroundWindow();
    MAIN_WINDOW_SEARCH search;

    if (is_our_main_window(fg))
        return fg;

    search.pid = GetCurrentProcessId();
    search.best = NULL;
    search.bestArea = 0;
    EnumWindows(find_main_window_proc, (LPARAM)&search);
    return search.best;
}

#if IME_FIX_MODE_LAYOUT_SWITCH
static HKL g_englishLayout = NULL;
static BOOL g_englishLayoutTried = FALSE;

/* Try common English keyboard layout IDs first. kbdus.dll ships with
   Windows and 00000409 normally loads without downloading a language pack.
   If it fails, fall back to other English variants and then to any already
   loaded English layout reported by GetKeyboardLayoutList. */
static HKL load_english_layout(void)
{
    int n;
    int got = 0;
    int i;
    char msg[256];

    if (g_englishLayoutTried)
        return g_englishLayout;

    /* Prefer layouts that already exist. Only when the user has no English
       layout at all do we load English (US) as a last-resort fallback. */
    n = GetKeyboardLayoutList(0, NULL);
    wsprintfA(msg, "GetKeyboardLayoutList count=%d", n);
    ime_log("layout", msg);

    if (n > 0) {
        HKL *list = (HKL*)my_alloc((DWORD)n * sizeof(HKL));
        if (list) {
            got = GetKeyboardLayoutList(n, list);
            for (i = 0; i < got; i++) {
                wsprintfA(msg, "existing layout[%d]=0x%X lang=0x%X",
                          i, (unsigned)(ULONG_PTR)list[i],
                          (unsigned)LOWORD(list[i]));
                ime_log("layout", msg);
                if (PRIMARYLANGID(LOWORD(list[i])) == LANG_ENGLISH) {
                    g_englishLayout = list[i];
                    wsprintfA(msg, "selected existing English layout[%d] hkl=0x%X",
                              i, (unsigned)(ULONG_PTR)g_englishLayout);
                    ime_log("layout", msg);
                    break;
                }
            }
            my_free(list);
        }
    }

    if (!g_englishLayout) {
        HKL hkl = LoadKeyboardLayoutW(L"00000409",
                                      KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
        if (hkl) {
            g_englishLayout = hkl;
            wsprintfA(msg, "no existing English layout; loaded 00000409 hkl=0x%X",
                      (unsigned)(ULONG_PTR)hkl);
            ime_log("layout", msg);
        } else {
            ime_log("layout", "FATAL: no English layout and LoadKeyboardLayout(00000409) failed");
        }
    }

    g_englishLayoutTried = TRUE;
    return g_englishLayout;
}

static HKL load_chinese_layout(void)
{
    int n;
    int got = 0;
    int i;

    if (g_chineseLayoutTried)
        return g_chineseLayout;

    n = GetKeyboardLayoutList(0, NULL);
    if (n > 0) {
        HKL *list = (HKL*)my_alloc((DWORD)n * sizeof(HKL));
        if (list) {
            got = GetKeyboardLayoutList(n, list);
            for (i = 0; i < got; i++) {
                if (PRIMARYLANGID(LOWORD(list[i])) == LANG_CHINESE) {
                    g_chineseLayout = list[i];
                    break;
                }
            }
            my_free(list);
        }
    }

    g_chineseLayoutTried = TRUE;
    if (!g_chineseLayout)
        ime_log("online", "no existing Chinese layout found for chat toggle");
    return g_chineseLayout;
}

#if ENABLE_PINYIN_ENGLISH_PROBE
/* Test Microsoft Pinyin's own Chinese/English mode by sending one synthetic
   Shift key press. This is only a probe: it logs conversion status before and
   after and does NOT drive the normal fix path yet. */
static void probe_shift_toggle_english_mode(void)
{
    HWND hwnd = find_main_window();
    HIMC himc;
    DWORD convBefore = 0, sentBefore = 0;
    DWORD convAfter = 0, sentAfter = 0;
    char msg[256];

    if (!hwnd) {
        ime_log("shift-probe", "window not found, probe skipped");
        return;
    }
    himc = ImmGetContext(hwnd);
    if (!himc) {
        ime_log("shift-probe", "ImmGetContext failed");
        return;
    }

    ImmGetConversionStatus(himc, &convBefore, &sentBefore);
    wsprintfA(msg, "before Shift conv=0x%X sent=0x%X", convBefore, sentBefore);
    ime_log("shift-probe", msg);

    keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    Sleep(120);

    ImmGetConversionStatus(himc, &convAfter, &sentAfter);
    wsprintfA(msg, "after Shift conv=0x%X sent=0x%X", convAfter, sentAfter);
    ime_log("shift-probe", msg);

    if ((convBefore & IME_CMODE_NATIVE) && !(convAfter & IME_CMODE_NATIVE))
        ime_log("shift-probe", "RESULT: Shift toggled Pinyin to English mode");
    else if (!(convBefore & IME_CMODE_NATIVE) && (convAfter & IME_CMODE_NATIVE))
        ime_log("shift-probe", "RESULT: Shift toggled Pinyin to Chinese mode");
    else
        ime_log("shift-probe", "RESULT: Shift did NOT toggle Pinyin mode (or state unchanged)");

    ImmReleaseContext(hwnd, himc);
}

static void probe_pinyin_english_mode(void)
{
    HWND hwnd = find_main_window();
    HIMC himc;
    DWORD convBefore = 0, sentBefore = 0;
    DWORD convAfter = 0, sentAfter = 0;
    BOOL ok;
    char msg[256];

    if (!hwnd) {
        ime_log("pinyin-probe", "window not found, probe skipped");
        return;
    }
    himc = ImmGetContext(hwnd);
    if (!himc) {
        ime_log("pinyin-probe", "ImmGetContext failed (no IME context for game window)");
        return;
    }

    ImmGetConversionStatus(himc, &convBefore, &sentBefore);
    wsprintfA(msg, "before ImmSetConversionStatus conv=0x%X sent=0x%X",
              convBefore, sentBefore);
    ime_log("pinyin-probe", msg);

    SetLastError(0);
    ok = ImmSetConversionStatus(himc, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
    ImmGetConversionStatus(himc, &convAfter, &sentAfter);
    wsprintfA(msg, "ImmSetConversionStatus ok=%d err=%lu after conv=0x%X sent=0x%X",
              ok ? 1 : 0, (unsigned long)GetLastError(), convAfter, sentAfter);
    ime_log("pinyin-probe", msg);

    if (convAfter & IME_CMODE_ALPHANUMERIC)
        ime_log("pinyin-probe", "RESULT: IME accepted English/alphanumeric mode");
    else
        ime_log("pinyin-probe", "RESULT: IME did NOT accept English/alphanumeric mode (TSF)");

    ImmReleaseContext(hwnd, himc);
}
#endif /* ENABLE_PINYIN_ENGLISH_PROBE */

/* Post a WM_INPUTLANGCHANGEREQUEST to the game window. This is the
   standard asynchronous layout switch; it cannot block the game UI thread.
   Repetitive lock-monitor calls pass logSuccess=FALSE so the debug log does
   not grow by two lines every 500ms. */
static BOOL post_layout_request_ex(HWND hwnd, HKL hkl, const char *tag,
                                   BOOL logSuccess)
{
    char msg[256];

    if (!PostMessageW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)hkl)) {
        wsprintfA(msg, "PostMessage(WM_INPUTLANGCHANGEREQUEST) failed hwnd=0x%X hkl=0x%X",
                  (unsigned)hwnd, (unsigned)(ULONG_PTR)hkl);
        ime_log(tag, msg);
        return FALSE;
    }

    if (logSuccess) {
        wsprintfA(msg, "layout request posted hwnd=0x%X hkl=0x%X",
                  (unsigned)hwnd, (unsigned)(ULONG_PTR)hkl);
        ime_log(tag, msg);
    }
    return TRUE;
}

static BOOL post_layout_request(HWND hwnd, HKL hkl, const char *tag)
{
    return post_layout_request_ex(hwnd, hkl, tag, TRUE);
}

static BOOL request_english_layout_for_game(const char *reason)
{
    HWND hwnd;
    HKL hkl = load_english_layout();

    if (!hkl) {
#if ENABLE_PINYIN_ENGLISH_PROBE
        /* No existing English layout: test the Pinyin Shift toggle. */
        probe_shift_toggle_english_mode();
#endif
        return FALSE;
    }

    hwnd = find_main_window();
    if (!hwnd)
        return FALSE; /* window may not exist yet; caller should retry */

    if (!post_layout_request(hwnd, hkl, "layout"))
        return FALSE;

    ime_log("layout", reason);
    return TRUE;
}

/* Find the LAST "[IME_ONLINE_FORCE:<0/1>]" marker emitted by main.lua. */
static int last_online_force_signal(const char *buf)
{
    static const char tag[] = "[IME_ONLINE_FORCE:";
    int tagLen = (int)sizeof(tag) - 1;
    int i;
    int value = -1;

    for (i = 0; buf[i]; i++) {
        if (buf[i] != '[')
            continue;
        if (my_strncmp(buf + i, tag, tagLen) == 0) {
            const char *p = buf + i + tagLen;
            if ((p[0] == '0' || p[0] == '1') && p[1] == ']')
                value = p[0] - '0';
        }
    }
    return value;
}

/* Find the LAST "[IME_ONLINE_CHAT:<0/1>]" marker emitted by main.lua. */
static int last_online_chat_signal(const char *buf)
{
    static const char tag[] = "[IME_ONLINE_CHAT:";
    int tagLen = (int)sizeof(tag) - 1;
    int i;
    int value = -1;

    for (i = 0; buf[i]; i++) {
        if (buf[i] != '[')
            continue;
        if (my_strncmp(buf + i, tag, tagLen) == 0) {
            const char *p = buf + i + tagLen;
            if ((p[0] == '0' || p[0] == '1') && p[1] == ']')
                value = p[0] - '0';
        }
    }
    return value;
}

/* Find the LAST "[IME_LOCK_LAYOUT:<0/1>]" marker emitted by main.lua. */
static int last_lock_layout_signal(const char *buf)
{
    static const char tag[] = "[IME_LOCK_LAYOUT:";
    int tagLen = (int)sizeof(tag) - 1;
    int i;
    int value = -1;

    for (i = 0; buf[i]; i++) {
        if (buf[i] != '[')
            continue;
        if (my_strncmp(buf + i, tag, tagLen) == 0) {
            const char *p = buf + i + tagLen;
            if ((p[0] == '0' || p[0] == '1') && p[1] == ']')
                value = p[0] - '0';
        }
    }
    return value;
}

/* Online-force override: entered an online lobby/run while the user has
   enabled "apply while mods are disabled online". */
static void online_apply_force(void)
{
    if (!g_onlineForce || g_onlineLockApplied)
        return;
    g_onlineLockApplied = TRUE;
    if (!g_lockLayout)
        g_lockLayout = TRUE;
    ime_log("online", "online force enabled: keeping English while mods are disabled");
}

static void online_enter_state(DWORD absPos)
{
    if (g_lobbySeen && absPos == g_lastLobbyPos)
        return; /* rolling-window overlap re-read: not a new lobby */

    g_lobbySeen = TRUE;
    g_lastLobbyPos = absPos;
    if (!g_online) {
        g_online = TRUE;
        ime_log("online", "online lobby detected");
    }
    if (g_onlineRunSeen) {
        /* A new lobby after a networked run: allow the run marker to be
           processed again if the host starts another round. */
        g_onlineRunSeen = FALSE;
        g_lastOnlineRunPos = 0;
    }
    if (g_chatChineseMode) {
        g_chatChineseMode = FALSE;
        g_chatRequestTick = 0;
        g_chatClosePending = FALSE;
        ime_log("online", "new lobby detected - chat mode ended");
    }
    online_apply_force();
}

static void online_leave_state(void)
{
    if (!g_online && !g_onlineLockApplied && !g_chatChineseMode)
        return; /* already processed (rolling-window overlap re-read) */

    if (g_chatChineseMode) {
        g_chatChineseMode = FALSE;
        g_chatRequestTick = 0;
        g_chatClosePending = FALSE;
        ime_log("online", "left lobby - chat mode ended");
    }
    if (g_onlineLockApplied) {
        g_onlineLockApplied = FALSE;
        g_lockLayout = g_lockLayoutBase;
        ime_log("online", "left lobby - online force released");
    }
    g_online = FALSE;
    g_onlineRunSeen = FALSE;
    g_lastOnlineRunPos = 0;
    g_lobbySeen = FALSE;
    g_lastLobbyPos = 0;
}

static void online_run_start_state(DWORD absPos)
{
    if (g_onlineRunSeen && absPos == g_lastOnlineRunPos)
        return; /* rolling-window overlap re-read */
    g_onlineRunSeen = TRUE;
    g_lastOnlineRunPos = absPos;
    ime_log("online", "networked run start observed");
    if (g_chatChineseMode) {
        g_chatChineseMode = FALSE;
        g_chatRequestTick = 0;
        g_chatClosePending = FALSE;
        ime_log("online", "networked run started - chat mode ended");
    }
    online_apply_force();
}

static DWORD WINAPI layout_monitor_thread(LPVOID param)
{
    DWORD lastLockLog = 0;
    DWORD lastLayoutPost = 0;
    DWORD lastSlow = 0;
    char savePath[MAX_PATH] = {0};
    DWORD scanFrom = 0;
    SHORT enterWasDown = 0;

    (void)param;
    while (!g_monitorStop) {
        DWORD now;
        HWND hwnd;
        HWND fg;
        DWORD tid;
        HKL current;
        HKL english;
        char *newLog;
        DWORD logStart = 0;
        BOOL enterDown;
        BOOL onlineChatActive;

        Sleep(MONITOR_FAST_MS); /* 1ms fast path (timer resolution set to 1ms) */
        now = GetTickCount();
        fg = GetForegroundWindow();

        /* ---- 1ms fast path: foreground, Enter edge, layout lock ---- */
        if (is_our_main_window(fg)) {
            hwnd = fg;
            tid = GetWindowThreadProcessId(hwnd, NULL);
            current = GetKeyboardLayout(tid);
            onlineChatActive = (g_online && g_onlineChatToggle);

            enterDown = ((GetAsyncKeyState(VK_RETURN) & 0x8000) != 0);
            if (onlineChatActive && enterDown && !enterWasDown && !g_chatChineseMode) {
                HKL chinese = load_chinese_layout();
                if (chinese) {
                    post_layout_request(hwnd, chinese, "online");
                    g_chatChineseMode = TRUE;
                    g_chatRequestTick = now;
                    g_chatClosePending = FALSE;
                    ime_log("online", "chat heuristic: Enter -> Chinese for chat (EXPERIMENTAL)");
                } else {
                    ime_log("online", "chat heuristic: no Chinese layout available");
                }
            }
            enterWasDown = enterDown;

            /* While chat mode is active, Enter is deliberately ignored:
               pressing Enter inside the still-open input box must NOT toggle
               back to English. The close decision is made from the game log
               ("Broadcasting chat message") in the slow path below. */
            if (!g_chatChineseMode && g_lockLayout) {
                english = load_english_layout();
                if (english) {
                    if (PRIMARYLANGID(LOWORD(current)) != LANG_ENGLISH) {
                        /* First reversion of an episode is posted immediately
                           (1ms fast path); repeats are throttled to avoid
                           flooding the game window with requests. */
                        if (lastLayoutPost == 0 ||
                            now - lastLayoutPost >= LAYOUT_REPOST_MS) {
                            post_layout_request_ex(hwnd, english, "layout", FALSE);
                            lastLayoutPost = now;
                        }
                        if (now - lastLockLog > 3000) {
                            char msg[160];
                            wsprintfA(msg,
                                      "lock_layout=1: non-English layout 0x%X detected, reverting to English",
                                      (unsigned)(LOWORD(current)));
                            ime_log("layout", msg);
                            lastLockLog = now;
                        }
                    } else {
                        lastLayoutPost = 0; /* episode ended; next one posts at once */
                    }
                }
            }
        } else {
            /* Game is not foreground; forget the previous edge state. */
            enterWasDown = 0;
        }

        /* ---- slow path every MONITOR_SLOW_MS: log scan + state machine ---- */
        if (now - lastSlow < MONITOR_SLOW_MS)
            continue;
        lastSlow = now;

        if (!savePath[0]) {
            resolve_save_path(savePath, sizeof(savePath));
            if (savePath[0])
                get_log_size(savePath, &scanFrom);
        }
        if (savePath[0]) {
            newLog = read_log_increment(savePath, &scanFrom, NULL, &logStart);
            if (newLog) {
                int signalValue = last_lock_layout_signal(newLog);
                int chatValue = last_online_chat_signal(newLog);
                int forceValue = last_online_force_signal(newLog);
                int createPos = last_index_of(newLog, ONLINE_TAG_CREATE);
                int joinedPos = last_index_of(newLog, ONLINE_TAG_JOINED);
                int lobbyPos = (createPos > joinedPos) ? createPos : joinedPos;
                int leavePos = last_index_of(newLog, ONLINE_LEAVE_TAG);
                int runPos = last_index_of(newLog, ONLINE_RUN_TAG);
                int chatSentPos = last_index_of(newLog, ONLINE_CHAT_SENT_TAG);

                if (signalValue == 1 && !g_lockLayoutBase) {
                    g_lockLayoutBase = TRUE;
                    if (!g_onlineLockApplied && !g_lockLayout) {
                        g_lockLayout = TRUE;
                        lastLockLog = 0;
                    }
                    ime_log("layout", "Lua/MCM signal: lock_layout=1");
                } else if (signalValue == 0 && g_lockLayoutBase) {
                    g_lockLayoutBase = FALSE;
                    if (g_lockLayout)
                        g_lockLayout = FALSE;
                    if (g_onlineLockApplied) {
                        g_onlineLockApplied = FALSE;
                        ime_log("online", "MCM lock_layout=0 released startup/online force");
                    }
                    ime_log("layout", "Lua/MCM signal: lock_layout=0");
                }

                if (chatValue == 1 && !g_onlineChatToggle) {
                    g_onlineChatToggle = TRUE;
                    persist_online_settings();
                    ime_log("online", "Lua/MCM signal: online chat toggle=1 (EXPERIMENTAL)");
                } else if (chatValue == 0 && g_onlineChatToggle) {
                    g_onlineChatToggle = FALSE;
                    persist_online_settings();
                    ime_log("online", "Lua/MCM signal: online chat toggle=0");
                }

                if (forceValue == 1 && !g_onlineForce) {
                    g_onlineForce = TRUE;
                    persist_online_settings();
                    ime_log("online", "Lua/MCM signal: online force=1");
                    if (g_online)
                        online_apply_force();
                } else if (forceValue == 0 && g_onlineForce) {
                    g_onlineForce = FALSE;
                    persist_online_settings();
                    ime_log("online", "Lua/MCM signal: online force=0");
                    if (g_onlineLockApplied) {
                        g_onlineLockApplied = FALSE;
                        g_lockLayout = g_lockLayoutBase;
                        ime_log("online", "online force disabled by MCM - lock restored");
                    }
                }

                /* A chat message actually left the game. Only this closes
                   chat mode; additional Enter presses inside the input box
                   no longer toggle. */
                if (g_chatChineseMode && chatSentPos >= 0) {
                    DWORD absPos = (DWORD)(logStart + chatSentPos);
                    if (absPos != g_lastChatSentPos) {
                        g_lastChatSentPos = absPos;
                        g_chatClosePending = TRUE;
                        g_chatCloseTick = now;
                        ime_log("online", "chat message broadcast - preparing to restore English");
                    }
                }

                /* State transitions can share one log increment. Apply the
                   LATEST event so leave -> create -> run resolves correctly. */
                if (leavePos >= 0 && leavePos >= lobbyPos && leavePos >= runPos) {
                    online_leave_state();
                } else if (lobbyPos >= 0 && lobbyPos > leavePos && lobbyPos >= runPos) {
                    online_enter_state((DWORD)(logStart + lobbyPos));
                } else if (runPos >= 0 && runPos > leavePos && runPos > lobbyPos) {
                    online_run_start_state((DWORD)(logStart + runPos));
                }
                my_free(newLog);
            }
        }

        if (!is_our_main_window(fg))
            continue; /* do not touch other applications */

        hwnd = fg;
        tid = GetWindowThreadProcessId(hwnd, NULL);
        current = GetKeyboardLayout(tid);
        onlineChatActive = (g_online && g_onlineChatToggle);

        /* If the user turned the experimental chat toggle off while chat
           mode was active, close chat mode and restore English. */
        if (!onlineChatActive && g_chatChineseMode) {
            english = load_english_layout();
            if (english)
                post_layout_request(hwnd, english, "online");
            g_chatChineseMode = FALSE;
            g_chatRequestTick = 0;
            g_chatClosePending = FALSE;
            ime_log("online", "chat toggle disabled - switched back to English");
        }

        /* After the game broadcasts the chat message, wait briefly for its
           input box to close, then restore English. */
        if (g_chatChineseMode && g_chatClosePending &&
            now - g_chatCloseTick >= CHAT_CLOSE_DELAY_MS) {
            english = load_english_layout();
            if (english)
                post_layout_request(hwnd, english, "online");
            g_chatChineseMode = FALSE;
            g_chatRequestTick = 0;
            g_chatClosePending = FALSE;
            ime_log("online", "chat message sent - switched back to English");
        }

        if (g_chatChineseMode) {
            /* Keep Chinese while chat mode is active; no timeout. If the
               requested layout never took effect (or the game changed it),
               re-request after a short grace period. */
            if (g_chatRequestTick &&
                now - g_chatRequestTick > 1000 &&
                PRIMARYLANGID(LOWORD(current)) != LANG_CHINESE) {
                HKL chinese = load_chinese_layout();
                if (chinese) {
                    if (now - lastLockLog > 3000) {
                        char msg[160];
                        wsprintfA(msg,
                                  "chat mode active: current layout 0x%X is not Chinese, re-requesting",
                                  (unsigned)(LOWORD(current)));
                        ime_log("online", msg);
                        lastLockLog = now;
                    }
                    post_layout_request_ex(hwnd, chinese, "online", FALSE);
                }
            }
            continue;
        }

        /* Layout lock enforcement already ran on the 1ms fast path above. */
    }
    return 0;
}

static void start_layout_monitor(void)
{
#if IME_FIX_MODE_LAYOUT_SWITCH
    CloseHandle(CreateThread(NULL, 0, layout_monitor_thread, NULL, 0, NULL));
#else
    (void)layout_monitor_thread;
#endif
}

static BOOL apply_ime_fix(const char *reason)
{
    return request_english_layout_for_game(reason);
}
#else
static BOOL apply_ime_fix(const char *reason)
{
    (void)reason;
    ImmDisableIME(-1);
    return TRUE;
}
#endif

#if ENABLE_RELOAD_FASTPATH
/* ====================================================================
 * Main-thread WM_TIMER fast path
 * ==================================================================== */

static volatile LONG g_timerScheduled = 0;

/* The fast-path timer runs on the game main thread. If the user was spamming
   keys when the timer fired, queued input messages can be dispatched
   re-entrantly while ImmDisableIME is working. Remove the queued keyboard,
   mouse and raw-input messages first; other messages are left untouched. */
static BOOL is_input_message(UINT message)
{
    if (message >= WM_KEYFIRST && message <= WM_KEYLAST)
        return TRUE;
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
        return TRUE;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        return TRUE;
    if (message == WM_INPUT)
        return TRUE;
    return FALSE;
}

static void drain_queued_input_messages(void)
{
    MSG msg;
    int guard = 0;

    for (;;) {
        if (!PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE))
            break;
        if (!is_input_message(msg.message))
            break; /* leave non-input messages in their original order */
        PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);
        if (++guard > 256)
            break; /* never spin here if input is being generated continuously */
    }
}

static void CALLBACK main_thread_disable_timer(HWND hwnd, UINT uMsg,
                                               UINT_PTR idEvent, DWORD dwTime)
{
    (void)uMsg;
    (void)dwTime;
    if (idEvent != IME_TIMER_ID)
        return;

    KillTimer(hwnd, IME_TIMER_ID);
    InterlockedExchange(&g_timerScheduled, 0);
    if (InterlockedCompareExchange(&g_imeDisabled, 1, 0) != 0)
        return; /* another trigger already disabled IME */
    ime_log("main", "fast path: draining queued input before ImmDisableIME");
    drain_queued_input_messages();
    ime_log("main", "fast path: WM_TIMER fired on main thread, calling ImmDisableIME");
    ImmDisableIME(-1);
    ime_log("main", "fast path: ImmDisableIME returned (IME disabled on main thread)");
}

/* Schedule the one-shot main-thread disable. Returns FALSE if no game window
   is available and the caller should use a direct/fallback path. */
static BOOL disable_ime_on_main_thread(void)
{
    HWND hwnd;

    if (g_imeDisabled)
        return TRUE;
    if (InterlockedCompareExchange(&g_timerScheduled, 1, 0) != 0)
        return TRUE; /* already scheduled */

    hwnd = find_main_window();
    if (!hwnd || !SetTimer(hwnd, IME_TIMER_ID, RELOAD_TIMER_DELAY_MS,
                           main_thread_disable_timer)) {
        InterlockedExchange(&g_timerScheduled, 0);
        return FALSE;
    }
    return TRUE;
}

#endif /* ENABLE_RELOAD_FASTPATH */

#if ENABLE_DIAGNOSTIC
/* ====================================================================
 * Passive diagnostic sampler
 * ==================================================================== */

static HHOOK g_diagGetMsgHook = NULL;
static volatile LONG g_diagMsgCount = 0;
static volatile LONG g_diagInputCount = 0;
static volatile LONG g_diagTimerCount = 0;

static BOOL diag_is_input_message(UINT message)
{
    if (message >= WM_KEYFIRST && message <= WM_KEYLAST)
        return TRUE;
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
        return TRUE;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        return TRUE;
    if (message == WM_INPUT)
        return TRUE;
    return FALSE;
}

static LRESULT CALLBACK diag_getmsg_proc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && lParam) {
        const MSG *msg = (const MSG*)lParam;
        InterlockedIncrement(&g_diagMsgCount);
        if (diag_is_input_message(msg->message))
            InterlockedIncrement(&g_diagInputCount);
        if (msg->message == WM_TIMER)
            InterlockedIncrement(&g_diagTimerCount);
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

static void diag_sample(const char *savePath, DWORD startTick,
                        DWORD *io_lastLogSize, SHORT *io_f12WasDown)
{
    char buf[512];
    DWORD now = GetTickCount();
    DWORD elapsed = now - startTick;
    DWORD logSize = 0;
    LONG delta = 0;
    DWORD result = 0;
    DWORD t0, latency = 0;
    GUITHREADINFO gui;
    DWORD guiFlags = 0;
    SHORT f12 = GetAsyncKeyState(DIAG_MARKER_VK);
    HWND hwnd = find_main_window();
    DWORD mainTid = 0;

    if (hwnd)
        mainTid = GetWindowThreadProcessId(hwnd, NULL);

    get_log_size(savePath, &logSize);
    if (*io_lastLogSize)
        delta = (LONG)(logSize - *io_lastLogSize);
    *io_lastLogSize = logSize;

    t0 = GetTickCount();
    if (hwnd) {
        SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result);
        latency = GetTickCount() - t0;
    }

    gui.cbSize = sizeof(gui);
    guiFlags = 0;
    if (mainTid && GetGUIThreadInfo(mainTid, &gui))
        guiFlags = gui.flags;

    {
        LONG msgs = InterlockedExchange(&g_diagMsgCount, 0);
        LONG input = InterlockedExchange(&g_diagInputCount, 0);
        LONG timers = InterlockedExchange(&g_diagTimerCount, 0);
        wsprintfA(buf,
                  "t=+%lu hwnd=0x%X log=%lu d=%ld resp=%s lat=%lu gui=0x%X fg=%d f12=%d msgs=%d inp=%d tim=%d",
                  elapsed, (unsigned)hwnd, logSize, delta,
                  (hwnd && result) ? "OK" : "FAIL",
                  latency, guiFlags,
                  (hwnd && GetForegroundWindow() == hwnd) ? 1 : 0,
                  (f12 & 0x8000) ? 1 : 0,
                  (int)msgs, (int)input, (int)timers);
    }
    ime_log("diag", buf);

    if ((f12 & 0x8000) && !(*io_f12WasDown & 0x8000)) {
        wsprintfA(buf, "MARKER F12 pressed at t=+%lu", elapsed);
        ime_log("diag", buf);
    }
    *io_f12WasDown = f12;
}

static void diag_run(const char *savePath, DWORD startTick)
{
    HWND hwnd = find_main_window();
    DWORD mainTid = 0;
    DWORD lastLogSize = 0;
    SHORT f12WasDown = 0;
    char buf[256];

    if (hwnd)
        mainTid = GetWindowThreadProcessId(hwnd, NULL);
    get_log_size(savePath, &lastLogSize);

    if (mainTid && g_hself)
        g_diagGetMsgHook = SetWindowsHookExW(WH_GETMESSAGE, diag_getmsg_proc,
                                             g_hself, mainTid);

    wsprintfA(buf, "start clear=+0 initial_hwnd=0x%X tid=%lu hook=%d",
              (unsigned)hwnd, mainTid, g_diagGetMsgHook ? 1 : 0);
    ime_log("diag", buf);

    while (GetTickCount() - startTick < DIAG_DURATION_MS) {
        diag_sample(savePath, startTick, &lastLogSize, &f12WasDown);
        Sleep(DIAG_SAMPLE_MS);
    }
    if (g_diagGetMsgHook) {
        UnhookWindowsHookEx(g_diagGetMsgHook);
        g_diagGetMsgHook = NULL;
    }
    ime_log("diag", "observation window finished - no IME call was made");
}

#endif /* ENABLE_DIAGNOSTIC */

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
    Sleep(WORKER_START_DELAY_MS); /* let the game reset log.txt before baselining */

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
    {
        DWORD scanFrom = baseline;
        for (attempts = 0; attempts < max_attempts; attempts++) {
            char *newLog = read_log_increment(savePath, &scanFrom, NULL, NULL);
            BOOL loaded;
            if (newLog) {
                loaded = contains_pair(newLog, MOD_DIR_TAG, MOD_FILE_TAG, MOD_GAP_MAX);
                my_free(newLog);
                if (loaded) {
#if ENABLE_PINYIN_ENGLISH_PROBE
                    probe_pinyin_english_mode();
#endif
                    if (apply_ime_fix("mod enabled - English layout requested at startup")) {
                        InterlockedExchange(&g_imeDisabled, 1);
                        return 0;
                    }
                    /* PostMessage may fail while the game window is still
                       being created; keep polling during the startup window. */
                }
            }
            Sleep(POLL_INTERVAL_MS);
        }
    }

    /* Phase 2: mod was NOT enabled at launch (e.g. user enabled it in-game
       after the startup window). The user still needs the IME disabled once
       they actually play. Wait for one of two empirically safe triggers:
         a) "[IME_RUN_STARTED]" - Lua mod fires it on MC_POST_GAME_STARTED,
            i.e. after the game has fully loaded a run -> safe.
         b) "[IME_IDLE]" - player has had no input for 3 seconds, so the
            game is quiescent (no UI transitions) -> safe.
       No trigger is based on AnmCache/Menu Mods Init: calling ImmDisableIME
       during mod reload or main-menu initialization freezes the UI. */
    {
        DWORD scanFrom = baseline;
        BOOL reloadSeen = FALSE;
#if ENABLE_RELOAD_FASTPATH
        BOOL reloadTimerTried = FALSE;
#endif

        ime_log("main", "mod not enabled at launch - waiting for a safe disable moment");
        for (;;) {
            char *newLog = read_log_increment(savePath, &scanFrom, NULL, NULL);
            BOOL runSeen;
            BOOL idleSeen;
            BOOL modMarkerSeen;
#if ENABLE_RELOAD_FASTPATH || ENABLE_DIAGNOSTIC
            BOOL clearSeen;
#endif

            if (g_imeDisabled) {
                ime_log("main", "IME already disabled - worker exiting");
                return 0;
            }
            if (!newLog) {
                Sleep(RUN_POLL_MS);
                continue;
            }

            runSeen = contains(newLog, RUN_TAG);
            idleSeen = contains(newLog, IDLE_TAG);
            modMarkerSeen = contains_pair(newLog, MOD_DIR_TAG, MOD_FILE_TAG, MOD_GAP_MAX);
#if ENABLE_RELOAD_FASTPATH || ENABLE_DIAGNOSTIC
            clearSeen = contains(newLog, CLEAR_TAG);
#endif

            /* Trigger b): mid-session mod execution was detected. Log evidence
               shows the game is still reloading/shutting down after
               AnmCache: Clear, so an immediate ImmDisableIME call is unsafe.
               Let the game finish its mod-state restart; the next launch
               will disable IME at startup. */
            if (modMarkerSeen && !reloadSeen) {
                reloadSeen = TRUE;
                ime_log("main", "mid-session mod execution detected - requesting English layout");
#if ENABLE_PINYIN_ENGLISH_PROBE
                probe_pinyin_english_mode();
#endif
            }
#if IME_FIX_MODE_LAYOUT_SWITCH
            if (reloadSeen && !g_imeDisabled) {
                /* PostMessage is asynchronous and cannot freeze the game UI,
                   so there is no need to wait for AnmCache/idle/run. Retry
                   until the game window accepts the request. */
                if (apply_ime_fix("mid-session mod execution - English layout requested")) {
                    InterlockedExchange(&g_imeDisabled, 1);
                    return 0;
                }
            }
#endif
#if ENABLE_RELOAD_FASTPATH
            if (reloadSeen && clearSeen && !reloadTimerTried) {
                reloadTimerTried = TRUE;
                if (disable_ime_on_main_thread()) {
                    ime_log("main", "reload complete - fast path scheduled on main thread");
                } else {
                    ime_log("main", "fast path unavailable - using run/idle fallback");
                }
            }
#endif

#if ENABLE_DIAGNOSTIC
            if (reloadSeen && clearSeen) {
                ime_log("diag", "AnmCache: Clear observed - starting passive observation (NO ImmDisableIME call)");
                my_free(newLog);
                diag_run(savePath, GetTickCount());
                return 0;
            }
#endif

            /* Trigger a): a single-player run started (verified freeze-free). */
            if (runSeen) {
                if (apply_ime_fix("per-run marker - English layout requested at run start")) {
                    InterlockedExchange(&g_imeDisabled, 1);
                    return 0;
                }
            }

            /* Trigger c): the Lua mod reports the player has been idle for 3
               seconds ("[IME_IDLE]") - the game is quiescent (no input, no
               popups, no page switches). */
            if (idleSeen) {
                if (apply_ime_fix("player idle - English layout requested after 3s of no input")) {
                    InterlockedExchange(&g_imeDisabled, 1);
                    return 0;
                }
            }
            my_free(newLog);
            Sleep(RUN_POLL_MS);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x)
{
    (void)x;
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_hself = h;
        g_imefixModule = h;
        timeBeginPeriod(1); /* make Sleep(1) actually resolve to ~1ms */
        ime_log("main", "ime_fix.dll v" IME_FIX_STRING " loaded (startup-disable mode)");
        load_settings();
        start_layout_monitor();
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    if (r == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_monitorStop, 1);
        timeEndPeriod(1);
    }
    return TRUE;
}

__declspec(dllexport) void IME_Init(void) {}