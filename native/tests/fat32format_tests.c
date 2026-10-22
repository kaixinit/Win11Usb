/* SPDX-License-Identifier: GPL-2.0-or-later
   File-only harness for the separate GPL formatter. No physical-device writes.
   Storage IOCTLs are test doubles; production formatting/seek/write code is reused. */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char fixture_file[MAX_PATH];
static DWORD fixture_sector;
static ULONGLONG fixture_length;
static unsigned checks;
static ULONGLONG fixture_tick;
static DWORD forced_open_error, forced_lock_error;
static unsigned open_failures, lock_failures, open_calls, lock_calls, extent_checks, format_writes;
static int fixture_locked, fixture_formatting;
static ULONGLONG fixture_clock(void) { return fixture_tick; }
static void fixture_wait(DWORD ms) { fixture_tick += ms; }
static void reset_acquisition(void) {
    fixture_tick = 0; forced_open_error = forced_lock_error = 0;
    open_failures = lock_failures = open_calls = lock_calls = extent_checks = format_writes = 0;
    fixture_locked = fixture_formatting = 0;
}
static void require(int value, const char *message) {
    ++checks;
    if (!value) { fprintf(stderr, "FAIL formatter: %s (%lu)\n", message, GetLastError()); exit(1); }
}
static HANDLE WINAPI file_only_open(LPCSTR name, DWORD access, DWORD sharing,
                                    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags,
                                    HANDLE template_file) {
    if (strcmp(name, fixture_file) || strncmp(name, "work\\native-formatter-tests-", 28)) {
        SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
    }
    ++open_calls;
    if (sharing != (FILE_SHARE_READ | FILE_SHARE_WRITE))
        require(0, "production volume open uses read/write sharing");
    if (forced_open_error || open_failures) {
        DWORD error = forced_open_error ? forced_open_error : ERROR_SHARING_VIOLATION;
        if (open_failures) --open_failures;
        SetLastError(error); return INVALID_HANDLE_VALUE;
    }
    fixture_locked = 0;
    return CreateFileA(name, access, sharing, security, creation, flags, template_file);
}
static BOOL WINAPI checked_write(HANDLE file, LPCVOID bytes, DWORD count, LPDWORD written,
                                 LPOVERLAPPED overlapped) {
    if (fixture_formatting) {
        if (!fixture_locked || extent_checks < 2)
            require(0, "format writes only after lock and repeated extent checks");
        ++format_writes;
    }
    return WriteFile(file, bytes, count, written, overlapped);
}
static BOOL WINAPI storage_fixture(HANDLE handle, DWORD code, LPVOID input, DWORD input_size,
                                    LPVOID output, DWORD output_size, LPDWORD returned,
                                    LPOVERLAPPED overlapped) {
    *returned = 0;
    if (code == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) {
        ++extent_checks;
        VOLUME_DISK_EXTENTS *v = output;
        if (output_size < sizeof(*v)) return FALSE;
        memset(v, 0, sizeof(*v));
        v->NumberOfDiskExtents = 1; v->Extents[0].DiskNumber = 3;
        v->Extents[0].StartingOffset.QuadPart = 1048576;
        v->Extents[0].ExtentLength.QuadPart = fixture_length;
        *returned = sizeof(*v); return TRUE;
    }
    if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY) {
        DISK_GEOMETRY *g = output;
        if (output_size < sizeof(*g)) return FALSE;
        memset(g, 0, sizeof(*g));
        g->BytesPerSector = fixture_sector; g->SectorsPerTrack = 63; g->TracksPerCylinder = 255;
        *returned = sizeof(*g); return TRUE;
    }
    if (code == IOCTL_DISK_GET_PARTITION_INFO_EX) {
        PARTITION_INFORMATION_EX *p = output;
        if (output_size < sizeof(*p)) return FALSE;
        memset(p, 0, sizeof(*p)); p->PartitionStyle = PARTITION_STYLE_MBR;
        p->StartingOffset.QuadPart = 1048576; p->PartitionLength.QuadPart = fixture_length;
        p->Mbr.PartitionType = 0x0c; p->Mbr.HiddenSectors = 1048576 / fixture_sector;
        *returned = sizeof(*p); return TRUE;
    }
    if (code == FSCTL_LOCK_VOLUME) {
        ++lock_calls;
        if (forced_lock_error || lock_failures) {
            DWORD error = forced_lock_error ? forced_lock_error : ERROR_ACCESS_DENIED;
            if (lock_failures) --lock_failures;
            SetLastError(error); return FALSE;
        }
        fixture_locked = 1;
        return TRUE;
    }
    if (code == FSCTL_UNLOCK_VOLUME) fixture_locked = 0;
    if (code == FSCTL_ALLOW_EXTENDED_DASD_IO ||
        code == FSCTL_DISMOUNT_VOLUME || code == FSCTL_UNLOCK_VOLUME) return TRUE;
    SetLastError(ERROR_INVALID_FUNCTION); return FALSE;
}
#undef CreateFile
#define CreateFile file_only_open
#define DeviceIoControl storage_fixture
#define WriteFile checked_write
#define GetTickCount64 fixture_clock
#define Sleep fixture_wait
#define main unused_production_formatter_entry
#include "../third_party/fat32format.c"
#undef main
#undef DeviceIoControl
#undef CreateFile
#undef WriteFile
#undef GetTickCount64
#undef Sleep

static void read_at(HANDLE file, ULONGLONG offset, void *bytes, DWORD count) {
    LARGE_INTEGER at; DWORD got = 0;
    at.QuadPart = offset;
    require(SetFilePointerEx(file, at, NULL, FILE_BEGIN), "seek ordinary fixture for inspection");
    require(ReadFile(file, bytes, count, &got, NULL) && got == count, "read ordinary fixture bytes");
}
static void image_case(const char *root, ULONGLONG gib, DWORD sector) {
    HANDLE file; LARGE_INTEGER length; DWORD got = 0;
    BYTE boot[4096], backup[4096], info[4096], fat[4096], zero[4096] = {0};
    FAT_BOOTSECTOR32 *b = (FAT_BOOTSECTOR32 *)boot;
    FAT_FSINFO *fsi = (FAT_FSINFO *)info;
    format_params params = {0};
    fixture_sector = sector;
    fixture_length = gib * 1024 * 1024 * 1024 - 2 * 1048576;
    snprintf(fixture_file, sizeof(fixture_file), "%s\\fat-%lluGiB-%lu.img", root, gib, sector);
    file = CreateFileA(fixture_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "create new ordinary sparse fixture");
    require(DeviceIoControl(file, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &got, NULL), "mark fixture sparse");
    length.QuadPart = fixture_length;
    require(SetFilePointerEx(file, length, NULL, FILE_BEGIN) && SetEndOfFile(file), "size sparse file");
    CloseHandle(file);
    reset_acquisition(); fixture_formatting = 1;
    /* Real Windows file sharing, retained across the production formatter call.
       An exclusive share=0 open would fail with 32 while this query handle exists. */
    file = CreateFileA(fixture_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "retain a shared query handle during formatter acquisition");
    require(format_volume(fixture_file, &params, 3, 1048576, fixture_length, sector),
            "production FAT32 formatter succeeds through file-only storage adapter");
    require(format_writes > 0 && lock_calls == 1, "one successful lock gates all formatter writes");
    CloseHandle(file); fixture_formatting = 0;
    file = CreateFileA(fixture_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "open resulting file for inspection");
    read_at(file, 0, boot, sector);
    require(b->wBytsPerSec == sector && b->dTotSec32 == fixture_length / sector &&
            b->dHiddSec == 1048576 / sector, "full capacity and physical offset preserved in BPB");
    require(boot[510] == 0x55 && boot[511] == 0xaa && boot[sector-2] == 0x55 &&
            boot[sector-1] == 0xaa, "FAT32 boot signatures for 512 and 4096 byte sectors");
    require(!memcmp(b->sBS_FilSysType, "FAT32   ", 8) && b->dRootClus == 2 && b->bNumFATs == 2,
            "FAT32 root and two FATs initialized");
    read_at(file, (ULONGLONG)b->wBkBootSec * sector, backup, sector);
    require(!memcmp(boot, backup, sector), "backup boot sector matches");
    read_at(file, sector, info, sector);
    require(fsi->dLeadSig == 0x41615252 && fsi->dStrucSig == 0x61417272 &&
            fsi->dTrailSig == 0xaa550000, "FSInfo signatures initialized");
    read_at(file, (ULONGLONG)b->wRsvdSecCnt * sector, fat, sector);
    require(((DWORD *)fat)[0] == 0x0ffffff8 && ((DWORD *)fat)[1] == 0x0fffffff &&
            ((DWORD *)fat)[2] == 0x0fffffff, "reserved FAT entries and root end-of-chain initialized");
    read_at(file, (ULONGLONG)(b->wRsvdSecCnt + b->dFATSz32) * sector, backup, sector);
    require(!memcmp(fat, backup, sector), "second FAT starts with identical entries");
    read_at(file, (ULONGLONG)(b->wRsvdSecCnt + 2 * b->dFATSz32) * sector, backup, sector);
    require(!memcmp(zero, backup, sector), "root directory is empty");
    memset(backup, 0x5a, sector);
    write_sect(file, (DWORD)((5ULL * 1024 * 1024 * 1024) / sector + 3), sector, backup, 1);
    read_at(file, 5ULL * 1024 * 1024 * 1024 + 3 * sector, fat, sector);
    require(!memcmp(fat, backup, sector), "production sector seek/write preserves offsets above 4 GiB");
    read_at(file, 1ULL * 1024 * 1024 * 1024 + 3 * sector, fat, sector);
    require(!memcmp(fat, zero, sector), "32-bit wrapped offset is untouched");
    CloseHandle(file);
    require(DeleteFileA(fixture_file), "remove only this test's ordinary sparse image");
    printf("PASS file-backed FAT32 %llu GiB / %lu-byte sectors (not physical disk format)\n", gib, sector);
}
static void acquisition_cases(const char *root) {
    HANDLE file, query, acquired, legacy;
    const char *stage;
    fixture_sector = 512; fixture_length = 8ULL * 1024 * 1024 * 1024 - 2 * 1048576;
    snprintf(fixture_file, sizeof(fixture_file), "%s\\acquisition.img", root);
    file = CreateFileA(fixture_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "create own acquisition fixture"); CloseHandle(file);
    query = CreateFileA(fixture_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    require(query != INVALID_HANDLE_VALUE, "create ordinary shared reader");
    legacy = CreateFileA(fixture_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    require(legacy == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION,
            "actual Windows sharing reproduces old share-zero error 32");
    reset_acquisition();
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired != INVALID_HANDLE_VALUE && fixture_locked && !format_writes,
            "production shared open succeeds beside real shared reader, lock remains required");
    CloseHandle(acquired); CloseHandle(query);
    reset_acquisition(); open_failures = 2;
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired != INVALID_HANDLE_VALUE && open_calls == 3 && fixture_tick == 400,
            "transient open error 32 retries before any formatting"); CloseHandle(acquired);
    reset_acquisition(); lock_failures = 3;
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired != INVALID_HANDLE_VALUE && open_calls == 4 && lock_calls == 4 && fixture_tick == 600,
            "temporary access-denied lock retries with a newly opened confirmed handle"); CloseHandle(acquired);
    reset_acquisition(); forced_open_error = ERROR_ACCESS_DENIED;
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED &&
            open_calls == 1 && fixture_tick == 0 && !format_writes,
            "open permission denial aborts immediately with original error");
    reset_acquisition();
    acquired = acquire_confirmed_volume(fixture_file, 4, 1048576, fixture_length, &stage);
    require(acquired == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_DATA &&
            !lock_calls && !format_writes, "wrong device extent is neither locked nor written");
    reset_acquisition(); forced_open_error = ERROR_SHARING_VIOLATION;
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION &&
            fixture_tick == 30000 && !lock_calls && !format_writes, "persistent sharing conflict times out before write");
    reset_acquisition(); forced_lock_error = ERROR_SHARING_VIOLATION;
    acquired = acquire_confirmed_volume(fixture_file, 3, 1048576, fixture_length, &stage);
    require(acquired == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION &&
            fixture_tick == 30000 && lock_calls == 151 && !format_writes,
            "persistent lock conflict is bounded, never bypassed");
    require(DeleteFileA(fixture_file), "delete only own acquisition fixture");
    printf("PASS real file sharing and production acquisition retries; volume lock IOCTL is a test double\n");
}
int main(int argc, char **argv) {
    if (argc == 3 && (!strcmp(argv[1], "--lock-timeout") || !strcmp(argv[1], "--verify-unwritten"))) {
        BYTE marker[512], actual[512];
        HANDLE file; DWORD n = 0;
        format_params params = {0};
        if (strncmp(argv[2], "work\\native-formatter-tests-", 28) || strstr(argv[2], "..") ||
            strchr(argv[2], ':') || strlen(argv[2]) > 160) return 2;
        snprintf(fixture_file, sizeof(fixture_file), "%s\\blocked-format.img", argv[2]);
        memset(marker, 0xa5, sizeof(marker));
        if (!strcmp(argv[1], "--verify-unwritten")) {
            file = CreateFileA(fixture_file, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
            require(file != INVALID_HANDLE_VALUE, "open failed-format marker for readback");
            read_at(file, 0, actual, sizeof(actual));
            require(!memcmp(marker, actual, sizeof(marker)), "persistent lock failure left ordinary file unchanged");
            CloseHandle(file);
            require(DeleteFileA(fixture_file), "remove only this test's failed-format marker");
            printf("PASS full formatter lock timeout performs no FAT32 writes; marker unchanged\n");
            return 0;
        }
        file = CreateFileA(fixture_file, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        require(file != INVALID_HANDLE_VALUE && WriteFile(file, marker, sizeof(marker), &n, NULL) &&
                n == sizeof(marker), "create own marker before full formatter failure test");
        CloseHandle(file);
        reset_acquisition(); fixture_formatting = 1;
        fixture_sector = 512; fixture_length = 8ULL * 1024 * 1024 * 1024 - 2 * 1048576;
        forced_lock_error = ERROR_SHARING_VIOLATION;
        format_volume(fixture_file, &params, 3, 1048576, fixture_length, fixture_sector);
        return 99; /* Expected to exit 32 in the production failure branch. */
    }
    if (argc == 2 && !strcmp(argv[1], "--invalid-seek")) {
        seek_to_sect(INVALID_HANDLE_VALUE, 1, 512); return 0;
    }
    if (argc != 2 || strncmp(argv[1], "work\\native-formatter-tests-", 28) ||
        strstr(argv[1], "..") || strchr(argv[1], ':') || strlen(argv[1]) > 160) return 2;
    require(CreateDirectoryA(argv[1], NULL), "unique formatter test directory");
    acquisition_cases(argv[1]);
    image_case(argv[1], 8, 512); image_case(argv[1], 64, 512);
    image_case(argv[1], 8, 4096); image_case(argv[1], 64, 4096);
    printf("PASS formatter %u assertions; only ordinary files written; no storage IOCTLs issued\n", checks);
    return 0;
}
