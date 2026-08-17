// ime_patcher.c - IME Conflict Fix PE import table patcher
// GUI subsystem (no console window), asInvoker manifest (no UAC).
// Wide-char (UTF-16) strings for correct Chinese display on any code page.
// Compile with /utf-8 (source is UTF-8) - see build.bat.

#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")

#define SECTION_NAME ".imefix"
#define DLL_NAME "ime_loader.dll"   /* loader/updater - exe imports this */
#define FUNC_NAME "IME_Init"
#define ALIGN_UP(v, a) (((v) + (a) - 1) & ~((a) - 1))

/* forward decls - defined below, used by do_patch/do_restore */
static int patch_cn_check(const char *exe_path);
static int restore_cn_check(const char *exe_path);
static int file_differs_from_buf(const char *path, const BYTE *buf, DWORD sz);

/* Show a MessageBox with a UTF-8 string; convert to UTF-16 internally.
   Source is UTF-8 (compiled with /utf-8), so Chinese literals work
   directly and display correctly on any Windows code page. */
static int msg_box(const char *utf8_text, const char *utf8_title, UINT type)
{
    wchar_t wtext[1024];
    wchar_t wtitle[256];
    MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, wtext, 1024);
    MultiByteToWideChar(CP_UTF8, 0, utf8_title, -1, wtitle, 256);
    return MessageBoxW(NULL, wtext, wtitle, type);
}

/* Find .imefix section index, or -1 if not present */
static int find_imefix_section(PIMAGE_NT_HEADERS32 nt)
{
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, SECTION_NAME, 8) == 0)
            return i;
    }
    return -1;
}

/* Check whether a file is a patched PE carrying our .imefix section.
   Returns 1 = patched, 0 = not patched, -1 = not a valid/unreadable PE file.
   (A locked file - e.g. the game is running - also yields -1; the caller's
   message should not claim "not a PE" for that case.) */
static int is_patched(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, rd = 0;
    BYTE *buf;
    int r = -1;

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return -1; }
    buf = malloc(sz);
    if (!buf) { CloseHandle(h); return -1; }
    if (ReadFile(h, buf, sz, &rd, NULL) && rd == sz) {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)buf;
        PIMAGE_NT_HEADERS32 nt = (PIMAGE_NT_HEADERS32)(buf + dos->e_lfanew);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE)
            r = (find_imefix_section(nt) >= 0) ? 1 : 0;
    }
    free(buf);
    CloseHandle(h);
    return r;
}

/* Recursively delete a directory tree. Used only for the two legacy
   folders created by old ime_fix versions:
     - %%APPDATA%%\ime-conflict-fix
     - <game dir>\ime-conflict-fix
   Never called with arbitrary user input. */
static void delete_tree(const char *dir)
{
    char pattern[MAX_PATH];
    char child[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    sprintf(pattern, "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryA(dir);
        return;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        sprintf(child, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            delete_tree(child);
            RemoveDirectoryA(child);
        } else {
            SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(child))
                printf("Removed %s\n", child);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    RemoveDirectoryA(dir);
}

/* Remove legacy log/config folders from very old versions. The current
   version writes debug.log next to ime_fix.bin in the mods folder and only
   creates settings.ini there when MCM online options are enabled, so these
   old folders are pure leftovers. */
static void cleanup_legacy_folders(const char *exe_path)
{
    char dir[MAX_PATH];
    char game_root[MAX_PATH];
    DWORD n;
    char *slash;

    n = GetEnvironmentVariableA("APPDATA", dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        sprintf(dir + n, "\\ime-conflict-fix");
        if (GetFileAttributesA(dir) != INVALID_FILE_ATTRIBUTES) {
            printf("Removing legacy folder %s\n", dir);
            delete_tree(dir);
            printf("Legacy AppData folder cleaned\n");
        }
    }

    strncpy(game_root, exe_path, sizeof(game_root) - 1);
    game_root[sizeof(game_root) - 1] = 0;
    slash = strrchr(game_root, '\\');
    if (slash) {
        strcpy(slash + 1, "ime-conflict-fix");
        if (GetFileAttributesA(game_root) != INVALID_FILE_ATTRIBUTES) {
            printf("Removing legacy game-root folder %s\n", game_root);
            delete_tree(game_root);
            printf("Legacy game-root folder cleaned\n");
        }
    }
}

/* Restore mode: verify exe carries our patch, then restore the
   pre-patch backup (.imefix.bak). If the exe has no .imefix section
   (e.g. Steam updated/verified the game), do nothing - the backup
   would be stale.
   When clean is non-zero, the .imefix.bak backup is removed after a
   successful restore, leaving NO trace of this mod in the game dir. */
static int do_restore(const char *path, int clean)
{
    /* Normalize to a FULL path first: with a bare relative name (CLI mode)
       the sibling-file logic below (backup name, DLL deletion) would derive
       the exe path itself and DeleteFileA would delete the exe. */
    char full[MAX_PATH];
    if (GetFullPathNameA(path, sizeof(full), full, NULL) == 0 || !full[0])
        return 1;
    path = full;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: cannot open %s\n", path); return 1; }
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); printf("ERROR: bad file size\n"); return 1; }
    BYTE *buf = malloc(sz);
    DWORD rd = 0;
    if (!buf || !ReadFile(h, buf, sz, &rd, NULL) || rd != sz) {
        CloseHandle(h);
        printf("ERROR: cannot read %s\n", path);
        if (buf) free(buf);
        return 1;
    }
    CloseHandle(h);

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)buf;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { printf("ERROR: not MZ\n"); free(buf); return 1; }
    PIMAGE_NT_HEADERS32 nt = (PIMAGE_NT_HEADERS32)(buf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { printf("ERROR: not PE\n"); free(buf); return 1; }

    if (find_imefix_section(nt) < 0) {
        printf("No .imefix section found - nothing to restore (exe already clean).\n");
        free(buf);
        cleanup_legacy_folders(path);
        return 0;
    }
    free(buf);

    char bak[MAX_PATH];
    sprintf(bak, "%s.imefix.bak", path);
    if (GetFileAttributesA(bak) == INVALID_FILE_ATTRIBUTES) {
        printf("ERROR: backup %s not found. Cannot restore automatically.\n", bak);
        printf("       Restore manually: Steam -> verify game files, or copy your own backup over isaac-ng.exe.\n");
        return 1;
    }

    if (!CopyFileA(bak, path, FALSE)) {
        printf("ERROR: restore failed (is the game running?)\n");
        return 1;
    }
    printf("RESTORED! %s restored from %s\n", path, bak);

    /* Clean up: remove ime_loader.dll + ime_fix.dll next to the exe */
    {
        char dll[MAX_PATH];
        strncpy(dll, path, sizeof(dll) - 1);
        dll[sizeof(dll) - 1] = 0;
        char *slash = strrchr(dll, '\\');
        if (slash) strcpy(slash + 1, "ime_loader.dll");
        if (GetFileAttributesA(dll) != INVALID_FILE_ATTRIBUTES)
            if (DeleteFileA(dll)) printf("Removed %s\n", dll);

        strncpy(dll, path, sizeof(dll) - 1);
        dll[sizeof(dll) - 1] = 0;
        slash = strrchr(dll, '\\');
        if (slash) strcpy(slash + 1, "ime_fix.dll");
        if (GetFileAttributesA(dll) != INVALID_FILE_ATTRIBUTES)
            if (DeleteFileA(dll)) printf("Removed %s\n", dll);
    }
    /* Restore the CN patch config.ini check value if we changed it */
    restore_cn_check(path);

    /* Remove pre-v0.4.9 leftovers on C: and in the game root. */
    cleanup_legacy_folders(path);

    /* Clean mode: remove the backup too - zero residue of this mod */
    if (clean) {
        if (GetFileAttributesA(bak) != INVALID_FILE_ATTRIBUTES) {
            if (DeleteFileA(bak))
                printf("Removed backup %s\n", bak);
        }
    }
    return 0;
}

static int do_patch(const char *path)
{
    printf("IME Patcher v1.5\nTarget: %s\n", path);

    /* Normalize to a FULL path first - sibling paths (backup, DLL copy)
       are derived from it and a bare relative name would corrupt them. */
    char full[MAX_PATH];
    if (GetFullPathNameA(path, sizeof(full), full, NULL) == 0 || !full[0])
        return 1;
    path = full;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: cannot open %s\n", path); return 1; }
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); printf("ERROR: bad file size\n"); return 1; }
    BYTE *buf = malloc(sz);
    DWORD rd = 0;
    if (!buf || !ReadFile(h, buf, sz, &rd, NULL) || rd != sz) {
        CloseHandle(h);
        printf("ERROR: cannot read %s\n", path);
        if (buf) free(buf);
        return 1;
    }
    CloseHandle(h);
    printf("Read %u bytes\n", sz);

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)buf;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { printf("ERROR: not MZ\n"); free(buf); return 1; }
    PIMAGE_NT_HEADERS32 nt = (PIMAGE_NT_HEADERS32)(buf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { printf("ERROR: not PE\n"); free(buf); return 1; }

    /* Refuse 64-bit PEs - Isaac Repentance+ is PE32 (x86). Patching a PE32+
       file with 32-bit import descriptors would corrupt it. */
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        printf("ERROR: not a 32-bit PE (PE32+, machine 0x%X) - Isaac Repentance+ is 32-bit.\n", nt->FileHeader.Machine);
        free(buf);
        return 1;
    }

    /* Already patched? Bail out instead of double-patching. */
    if (find_imefix_section(nt) >= 0) {
        printf("Already patched (.imefix section present). Nothing to do.\n");
        free(buf);
        return 0;
    }

    DWORD fa = nt->OptionalHeader.FileAlignment;
    DWORD sa = nt->OptionalHeader.SectionAlignment;
    printf("PE OK, sections=%u FileAlign=0x%X SectionAlign=0x%X\n", nt->FileHeader.NumberOfSections, fa, sa);

    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    DWORD import_rva = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    printf("Import RVA=0x%X\n", import_rva);

    PIMAGE_IMPORT_DESCRIPTOR imps = NULL;
    for (int i = 0; i < nt->FileHeader.NumberOfSections && !imps; i++) {
        if (import_rva >= sec[i].VirtualAddress && import_rva < sec[i].VirtualAddress + sec[i].Misc.VirtualSize)
            imps = (PIMAGE_IMPORT_DESCRIPTOR)(buf + import_rva - sec[i].VirtualAddress + sec[i].PointerToRawData);
    }
    if (!imps) { printf("ERROR: no imports\n"); free(buf); return 1; }

    int count = 0;
    while (imps[count].Name || imps[count].FirstThunk) count++;
    printf("Existing imports: %d\n", count);

    /* Backup (dedicated name - never clobbers an existing .bak from the CN patch).
       A STALE backup (game updated on Steam since the last patch, then re-patched)
       is refreshed so a later restore rolls back to the CURRENT pre-patch exe
       instead of an ancient game version. */
    char bak[MAX_PATH];
    sprintf(bak, "%s.imefix.bak", path);
    if (GetFileAttributesA(bak) == INVALID_FILE_ATTRIBUTES) {
        CopyFileA(path, bak, TRUE);
        printf("Backup: %s\n", bak);
    } else if (file_differs_from_buf(bak, buf, sz)) {
        CopyFileA(path, bak, FALSE);
        printf("Backup was stale (game updated?) - refreshed: %s\n", bak);
    } else {
        printf("Backup already exists, keeping it: %s\n", bak);
    }

    /* Layout new section */
    DWORD new_va = ALIGN_UP(nt->OptionalHeader.SizeOfImage, sa);
    DWORD new_raw = ALIGN_UP(sz, fa);
    printf("New section: VA=0x%X raw=0x%X\n", new_va, new_raw);

    BYTE sdata[1024] = {0};
    DWORD off = 0;

    /* The new section data is laid out in the fixed 1KB buffer:
       descriptors + ILT + hint/name + DLL name must fit. */
    {
        DWORD need = (DWORD)(count + 2) * sizeof(IMAGE_IMPORT_DESCRIPTOR)
                   + 8 + sizeof(IMAGE_IMPORT_BY_NAME) + sizeof(FUNC_NAME) - 1 + sizeof(DLL_NAME);
        if (need > sizeof(sdata)) {
            printf("ERROR: too many imports (%d) for the patch buffer - not patching.\n", count);
            free(buf);
            return 1;
        }
    }

    /* Combined import descriptors (old + new + null) */
    DWORD desc_n = count + 2;
    memcpy(sdata, imps, count * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    off = desc_n * sizeof(IMAGE_IMPORT_DESCRIPTOR);

    /* ILT (2 DWORDs for x86) */
    DWORD rva_ilt = new_va + off;
    DWORD rva_hint = rva_ilt + 8;
    DWORD rva_dll = rva_hint + sizeof(IMAGE_IMPORT_BY_NAME) + sizeof(FUNC_NAME) - 1;

    *(DWORD*)(sdata + off) = rva_hint;
    off += 8;

    /* Hint/Name */
    PIMAGE_IMPORT_BY_NAME hn = (PIMAGE_IMPORT_BY_NAME)(sdata + off);
    hn->Hint = 0;
    memcpy(hn->Name, FUNC_NAME, sizeof(FUNC_NAME));
    off += sizeof(IMAGE_IMPORT_BY_NAME) + sizeof(FUNC_NAME) - 1;

    /* DLL name */
    memcpy(sdata + off, DLL_NAME, sizeof(DLL_NAME));
    off += sizeof(DLL_NAME);

    /* New import descriptor */
    PIMAGE_IMPORT_DESCRIPTOR nd = (PIMAGE_IMPORT_DESCRIPTOR)(sdata + count * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    nd->Characteristics = rva_ilt;
    nd->FirstThunk = rva_ilt;
    nd->Name = rva_dll;
    nd->TimeDateStamp = 0;
    nd->ForwarderChain = 0;

    /* New section header */
    PIMAGE_SECTION_HEADER ns = &sec[nt->FileHeader.NumberOfSections];
    memset(ns, 0, sizeof(*ns));
    memcpy(ns->Name, SECTION_NAME, 8);
    ns->Misc.VirtualSize = ALIGN_UP(off, sa);
    ns->VirtualAddress = new_va;
    ns->SizeOfRawData = ALIGN_UP(off, fa);
    ns->PointerToRawData = new_raw;
    ns->Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA;

    nt->FileHeader.NumberOfSections++;
    nt->OptionalHeader.SizeOfImage = new_va + ns->Misc.VirtualSize;
    nt->OptionalHeader.DataDirectory[1].VirtualAddress = new_va;
    nt->OptionalHeader.DataDirectory[1].Size = desc_n * sizeof(IMAGE_IMPORT_DESCRIPTOR);

    printf("Section: raw_sz=0x%X virt_sz=0x%X off=%d\n", ns->SizeOfRawData, ns->Misc.VirtualSize, off);

    /* Write: seek to new_raw before writing section data.
       NOTE: CREATE_ALWAYS truncates first - if any write below fails the
       exe is damaged, but the backup (.imefix.bak) was already created
       above, so recovery is one restore away. */
    HANDLE out = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (out == INVALID_HANDLE_VALUE) { printf("ERROR: write\n"); free(buf); return 1; }
    DWORD wr = 0;
    BOOL ok = WriteFile(out, buf, sz, &wr, NULL) && wr == sz;
    if (ok) {
        ok = SetFilePointer(out, new_raw, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
        BYTE *pad = calloc(1, ns->SizeOfRawData);
        if (pad) {
            memcpy(pad, sdata, off);
            wr = 0;
            ok = ok && WriteFile(out, pad, ns->SizeOfRawData, &wr, NULL) && wr == ns->SizeOfRawData;
            free(pad);
        } else {
            ok = FALSE;
        }
    }
    if (ok) {
        ok = SetFilePointer(out, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER && SetEndOfFile(out);
    }
    CloseHandle(out);
    if (!ok) {
        printf("ERROR: write to %s failed - the file may be damaged.\n", path);
        printf("       Restore it with: ime_patcher.exe --restore %s\n", path);
        free(buf);
        return 1;
    }
    free(buf);

    printf("PATCHED! %s!%s added.\n", DLL_NAME, FUNC_NAME);
    /* Suppress the CN patch hash-check popup if the CN patch is installed */
    patch_cn_check(path);
    return 0;
}

/* Compare a file's content with a memory buffer. Returns 1 when the file
   is missing, unreadable, or differs in any byte - i.e. a backup file that
   does not match the current exe is STALE (typically after a Steam update
   replaced the exe and the user re-patched). */
static int file_differs_from_buf(const char *path, const BYTE *buf, DWORD sz)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD fsz, rd = 0;
    BYTE *fb;
    int diff;

    if (h == INVALID_HANDLE_VALUE)
        return 1;
    fsz = GetFileSize(h, NULL);
    if (fsz == INVALID_FILE_SIZE || fsz != sz) {
        CloseHandle(h);
        return 1;
    }
    fb = (BYTE*)malloc(sz);
    if (!fb) { CloseHandle(h); return 1; }
    if (!ReadFile(h, fb, sz, &rd, NULL) || rd != sz) {
        free(fb);
        CloseHandle(h);
        return 1;
    }
    CloseHandle(h);
    diff = (memcmp(buf, fb, sz) != 0);
    free(fb);
    return diff;
}

/* Copy ime_loader.dll from our own directory to the game exe's directory.
   Returns 1 on success, 0 if the DLL is not next to us or copy failed.
   The exe path is resolved to a FULL path first: with a bare relative name
   (e.g. CLI mode: ime_patcher.exe isaac-ng.exe) the "game directory" is the
   CWD, and deriving the destination from the raw argument would yield
   "isaac-ng.exe" itself - CopyFileA would then overwrite the freshly
   patched exe with ime_loader.dll. */
static int copy_dll_next_to_game(const char *game_exe)
{
    char exe_full[MAX_PATH] = {0};
    char self_dir[MAX_PATH] = {0};
    char dll_src[MAX_PATH];
    char dll_dst[MAX_PATH];
    char *slash;

    if (GetFullPathNameA(game_exe, sizeof(exe_full), exe_full, NULL) == 0)
        return 0;
    slash = strrchr(exe_full, '\\');
    if (!slash)
        return 0;
    *slash = 0; /* exe_full = full game directory */

    GetModuleFileNameA(NULL, self_dir, MAX_PATH);
    slash = strrchr(self_dir, '\\');
    if (slash)
        *slash = 0;

    _snprintf(dll_src, sizeof(dll_src), "%s\\%s", self_dir, DLL_NAME);
    if (GetFileAttributesA(dll_src) == INVALID_FILE_ATTRIBUTES)
        return 0;
    _snprintf(dll_dst, sizeof(dll_dst), "%s\\%s", exe_full, DLL_NAME);
    return CopyFileA(dll_src, dll_dst, FALSE) ? 1 : 0;
}

/* ====================================================================
 * Official CN patch config.ini handling.
 *
 * The CN patch (cn_rep+) checks a hash of isaac-ng.exe on every launch
 * and shows a MessageBox popup on mismatch. Our patch changes the exe,
 * so we must set its config.ini "check=" value to -1 (official "silent"
 * switch, per the comment in the file) to suppress the popup.
 *
 * config.ini is GBK-encoded, but the check line is pure ASCII, so we
 * process it line-by-line with plain text (no encoding conversion).
 * The original check value is recorded next to the exe backup so the
 * uninstall path can restore it.
 * ==================================================================== */

#define CN_CFG_REL "\\mods\\cn_rep+_3568677664\\res\\config.ini"
#define STATE_REL "\\isaac-ng.exe.imefix.state"

/* Build "<exe_dir>\mods\cn_rep+_3568677664\res\config.ini" from the exe path */
static void cn_cfg_path(const char *exe_path, char *out, DWORD out_sz)
{
    strncpy(out, exe_path, out_sz - 1);
    out[out_sz - 1] = 0;
    char *slash = strrchr(out, '\\');
    if (slash) *slash = 0;
    strncat(out, CN_CFG_REL, out_sz - strlen(out) - 1);
}

/* Build "<exe_dir>\isaac-ng.exe.imefix.state" */
static void cn_state_path(const char *exe_path, char *out, DWORD out_sz)
{
    strncpy(out, exe_path, out_sz - 1);
    out[out_sz - 1] = 0;
    char *slash = strrchr(out, '\\');
    if (slash) *slash = 0;
    strncat(out, STATE_REL, out_sz - strlen(out) - 1);
}

/* Read a text file fully. Returns malloc'd NUL-terminated buffer or NULL. */
static char *read_text_file(const char *path, DWORD *out_size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return NULL; }
    char *buf = (char*)malloc(sz + 1);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, buf, sz, &rd, NULL);
    CloseHandle(h);
    if (!ok) { free(buf); return NULL; }
    buf[rd] = 0;
    if (out_size) *out_size = rd;
    return buf;
}

/* Byte-stream scan for "check=" in the config.ini (GBK-safe: never tokenizes
   on CR/LF, which would corrupt multi-byte Chinese characters).
   loc: receives pointer to the first digit of the check value (or NULL).
   Returns the number of digits in the value. */
static int find_check_value(char *buf, DWORD sz, char **loc)
{
    static const char pat[] = "check=";
    DWORD i;
    for (i = 0; i + 6 <= sz; i++) {
        if (memcmp(buf + i, pat, 6) == 0) {
            char *p = buf + i + 6;
            /* loc points at the START of the value (including any '-') */
            char *val_start = p;
            if (*p == '-') p++;
            if (*p >= '0' && *p <= '9') {
                DWORD n = 0;
                while (p[n] >= '0' && p[n] <= '9') n++;
                *loc = val_start;
                /* total value length = optional '-' + digits */
                return (int)n + (val_start[0] == '-' ? 1 : 0);
            }
        }
    }
    *loc = NULL;
    return 0;
}

/* Rewrite config.ini replacing the check value with newVal.
   Only the value bytes change; everything else is copied verbatim,
   so the GBK-encoded Chinese text is preserved untouched.
   Returns 1 on success, 0 on failure. */
static int rewrite_check_value(const char *cfg, const char *newVal)
{
    DWORD sz = 0;
    char *buf = read_text_file(cfg, &sz);
    if (!buf) return 0;
    char *loc = NULL;
    int nd = find_check_value(buf, sz, &loc);
    if (!loc) { free(buf); return 0; }

    size_t old_len = (size_t)nd;
    size_t new_len = strlen(newVal);
    size_t tail_len = (size_t)(sz - (DWORD)(loc + nd - buf));
    DWORD out_sz = sz - (DWORD)old_len + (DWORD)new_len;
    char *out = (char*)malloc(out_sz + 1);
    if (!out) { free(buf); return 0; }

    /* [0, loc) + newVal + [loc+old_len, sz) */
    size_t pre = (size_t)(loc - buf);
    memcpy(out, buf, pre);
    memcpy(out + pre, newVal, new_len);
    memcpy(out + pre + new_len, loc + nd, tail_len);

    HANDLE h = CreateFileA(cfg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(out); free(buf); return 0; }
    DWORD wr;
    WriteFile(h, out, out_sz, &wr, NULL);
    CloseHandle(h);
    free(out);
    free(buf);
    return 1;
}

/* Patch the CN patch config.ini: set check=-1, record original to state file.
   Returns 1 on success, 0 if config.ini not found or no check line. */
static int patch_cn_check(const char *exe_path)
{
    char cfg[MAX_PATH];
    cn_cfg_path(exe_path, cfg, sizeof(cfg));
    if (GetFileAttributesA(cfg) == INVALID_FILE_ATTRIBUTES)
        return 0; /* CN patch not installed */

    DWORD sz = 0;
    char *buf = read_text_file(cfg, &sz);
    if (!buf) return 0;
    char *loc = NULL;
    int nd = find_check_value(buf, sz, &loc);
    if (!loc) { free(buf); return 0; }

    /* extract the original value */
    char *orig = (char*)malloc((size_t)nd + 1);
    memcpy(orig, loc, (size_t)nd);
    orig[nd] = 0;
    free(buf);

    if (!rewrite_check_value(cfg, "-1")) { free(orig); return 0; }

    /* record original value next to the exe */
    char state[MAX_PATH];
    cn_state_path(exe_path, state, sizeof(state));
    HANDLE hs = CreateFileA(state, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hs != INVALID_HANDLE_VALUE) {
        DWORD wr;
        WriteFile(hs, orig, (DWORD)strlen(orig), &wr, NULL);
        CloseHandle(hs);
    }
    printf("CN patch config.ini: check=%s -> check=-1 (popup suppressed)\n", orig);
    free(orig);
    return 1;
}

/* Restore the CN patch config.ini check value from the state file.
   Returns 1 on success, 0 if no state/backup exists. */
static int restore_cn_check(const char *exe_path)
{
    char state[MAX_PATH];
    cn_state_path(exe_path, state, sizeof(state));
    DWORD sz = 0;
    char *orig = read_text_file(state, &sz);
    if (!orig) return 0;
    orig[sz] = 0;

    char cfg[MAX_PATH];
    cn_cfg_path(exe_path, cfg, sizeof(cfg));
    if (GetFileAttributesA(cfg) == INVALID_FILE_ATTRIBUTES) {
        free(orig);
        DeleteFileA(state);
        return 0;
    }

    int ok = rewrite_check_value(cfg, orig);
    if (ok) {
        DeleteFileA(state);
        printf("CN patch config.ini: check restored to %s\n", orig);
    }
    free(orig);
    return ok;
}
/* Try to auto-locate the game exe, like the official CN patch does:
   read SteamPath from registry and append the Isaac relative path.
   Returns 1 and fills out when found, 0 otherwise. */
static int find_game_exe(char *out, DWORD out_sz)
{
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        char steam_path[MAX_PATH] = {0};
        DWORD sz = sizeof(steam_path);
        DWORD type = 0;
        if (RegQueryValueExA(key, "SteamPath", NULL, &type, (LPBYTE)steam_path, &sz) == ERROR_SUCCESS) {
            RegCloseKey(key);
            char cand[MAX_PATH];
            _snprintf(cand, sizeof(cand), "%s\\steamapps\\common\\The Binding of Isaac Rebirth\\isaac-ng.exe", steam_path);
            if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
                strncpy(out, cand, out_sz - 1);
                out[out_sz - 1] = 0;
                return 1;
            }
        } else {
            RegCloseKey(key);
        }
    }
    return 0;
}

/* Show the file picker (wide-char, so Chinese titles and paths work).
   Constrains the choice to the Isaac main executable (isaac-ng.exe),
   like the official CN patch.
   init_dir, when non-NULL, is the folder the dialog opens in.
   Returns 1 on success (fills out with UTF-8 path),
           -1 if the picked file is not isaac-ng.exe (caller should retry),
           0 on cancel. */
static int pick_exe(char *out, DWORD out_sz, const char *init_dir)
{
    wchar_t wfile[MAX_PATH] = {0};
    wcscpy(wfile, L"isaac-ng.exe"); /* preset the target filename */
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Isaac Executable (isaac-ng.exe)\0isaac-ng.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = wfile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"请选择 isaac-ng.exe 打补丁";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    wchar_t winit[MAX_PATH] = {0};
    if (init_dir)
        MultiByteToWideChar(CP_UTF8, 0, init_dir, -1, winit, MAX_PATH);
    if (winit[0])
        ofn.lpstrInitialDir = winit;
    if (!GetOpenFileNameW(&ofn))
        return 0;

    /* Enforce that the picked file is the Isaac main executable */
    wchar_t *name = wcsrchr(wfile, L'\\');
    name = name ? name + 1 : wfile;
    if (_wcsicmp(name, L"isaac-ng.exe") != 0)
        return -1;

    WideCharToMultiByte(CP_UTF8, 0, wfile, -1, out, out_sz, NULL, NULL);
    return 1;
}

/* Show the file picker with retry-on-wrong-file loop.
   Returns 1 and fills out (UTF-8 path) on success, 0 on cancel. */
static int pick_exe_retry(char *out, DWORD out_sz, const char *init_dir)
{
    for (;;) {
        int r = pick_exe(out, out_sz, init_dir);
        if (r == 1)
            return 1;
        if (r == 0)
            return 0; /* cancelled */
        /* r == -1: wrong file - inform and retry */
        int again = msg_box(
            "请选择以撒的主程序 isaac-ng.exe。\n"
            "刚才选择的文件不是 isaac-ng.exe。",
            "文件不正确", MB_RETRYCANCEL | MB_ICONWARNING);
        if (again != IDRETRY)
            return 0;
    }
}

/* GUI mode: auto-locate the game exe (registry SteamPath, like the
   official CN patch), fall back to a file picker. If the chosen exe is
   already patched, ask whether to restore it. */
static int gui_mode(void)
{
    char file[MAX_PATH] = {0};
    char steam_root[MAX_PATH] = {0};
    int found = find_game_exe(file, sizeof(file));

    if (!found) {
        /* Try to point the picker at the Steam games folder as a hint */
        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
            DWORD sz = sizeof(steam_root);
            DWORD type = 0;
            if (RegQueryValueExA(key, "SteamPath", NULL, &type, (LPBYTE)steam_root, &sz) != ERROR_SUCCESS)
                steam_root[0] = 0;
            RegCloseKey(key);
        }
        if (!pick_exe_retry(file, sizeof(file), steam_root[0] ? steam_root : NULL))
            return 0; /* user cancelled */
    } else {
        /* Auto-located - confirm before touching the exe */
        char msg[1024];
        _snprintf(msg, sizeof(msg),
            "已找到游戏：\n%s\n\n"
            "点击\"是\"打补丁（添加 ime_loader.dll 加载，自动更新输入法管理）。\n"
            "点击\"否\"手动选择其他文件。",
            file);
        int r = msg_box(msg, "已找到游戏 - IME 修复", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL)
            return 0;
        if (r == IDNO) {
            /* User wants a different file - open the picker */
            char picked[MAX_PATH] = {0};
            if (!pick_exe_retry(picked, sizeof(picked), NULL))
                return 0; /* user cancelled the picker */
            strncpy(file, picked, sizeof(file) - 1);
            file[sizeof(file) - 1] = 0; /* ensure termination */
        }
    }

    int st = is_patched(file);
    if (st < 0) {
        msg_box("无法读取所选文件，或它不是有效的 Windows 可执行文件（PE）。\n\n"
                "常见原因：游戏正在运行（请先退出游戏），或选择的不是 isaac-ng.exe。",
                "请选择正确的文件", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (st == 1) {
        int r = msg_box(
            "该 exe 已经被 IME 修复打补丁。\n\n"
            "点击\"是\"：还原并完全清理（删除备份，零残留）。\n"
            "点击\"否\"：仅还原（保留备份文件 .imefix.bak）。\n"
            "点击\"取消\"：不操作。",
            "已打补丁 - IME 修复", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL)
            return 0;
        int rc = do_restore(file, (r == IDYES) ? 1 : 0);
        if (rc == 0)
            msg_box("还原成功。\n\nexe 已恢复到补丁前状态，\nime_loader.dll 和 ime_fix.dll 已从游戏目录删除。\n\n"
                    "(若选择完全清理，备份文件也已删除)",
                    "还原成功", MB_OK | MB_ICONINFORMATION);
        else
            msg_box("还原失败。\n\n请确认游戏未在运行，且 exe 旁边存在\nisaac-ng.exe.imefix.bak 备份文件。",
                    "还原失败", MB_OK | MB_ICONERROR);
        return rc;
    }

    int rc = do_patch(file);
    if (rc == 0) {
        char msg[512];
        if (copy_dll_next_to_game(file)) {
            _snprintf(msg, sizeof(msg),
                "打补丁成功！\n\n"
                "ime_loader.dll 已自动复制到游戏目录。\n"
                "现在可以直接启动游戏了（输入法管理会随创意工坊自动更新）。");
        } else {
            _snprintf(msg, sizeof(msg),
                "打补丁成功！\n\n"
                "但未找到 ime_loader.dll（应与本程序在同一目录）。\n"
                "请手动将 ime_loader.dll 复制到游戏目录后启动游戏。");
        }
        msg_box(msg, "打补丁成功", MB_OK | MB_ICONINFORMATION);
    } else
        msg_box("打补丁失败。\n\n请确认游戏未在运行，且\nisaac-ng.exe 是有效的可执行文件。",
                "打补丁失败", MB_OK | MB_ICONERROR);
    return rc;
}

/* GUI subsystem exe: no console window on double-click (like the official
   CN patch). When run from a console (install.bat / command line), attach
   to the parent console so printf output is visible. */
static void attach_console(void)
{
    if (GetConsoleWindow() == NULL) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2)
        attach_console();

    if (argc < 2)
        return gui_mode();

    const char *path;
    int restore = 0;
    int clean = 0;
    if (strcmp(argv[1], "--restore") == 0) {
        if (argc < 3) { printf("Usage: ime_patcher.exe --restore [--clean] <isaac-ng.exe>\n"); return 1; }
        restore = 1;
        if (strcmp(argv[2], "--clean") == 0) {
            if (argc < 4) { printf("Usage: ime_patcher.exe --restore [--clean] <isaac-ng.exe>\n"); return 1; }
            clean = 1;
            path = argv[3];
        } else {
            path = argv[2];
        }
    } else {
        path = argv[1];
    }

    if (restore)
        return do_restore(path, clean);

    int rc = do_patch(path);
    if (rc == 0) {
        /* The exe now imports ime_loader.dll - it MUST be in the game dir or
           Windows will fail to start the game. Copy it (if available). */
        if (copy_dll_next_to_game(path))
            printf("%s copied next to the game.\n", DLL_NAME);
        else
            printf("WARNING: %s not found next to ime_patcher.exe - copy it to the game directory manually!\n", DLL_NAME);
    }
    return rc;
}
