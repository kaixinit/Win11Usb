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
    return CreateFileA(name, access, sharing, security, creation, flags, template_file);
}
static BOOL WINAPI storage_fixture(HANDLE handle, DWORD code, LPVOID input, DWORD input_size,
                                    LPVOID output, DWORD output_size, LPDWORD returned,
                                    LPOVERLAPPED overlapped) {
    *returned = 0;
    if (code == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) {
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
    if (code == FSCTL_ALLOW_EXTENDED_DASD_IO || code == FSCTL_LOCK_VOLUME ||
        code == FSCTL_DISMOUNT_VOLUME || code == FSCTL_UNLOCK_VOLUME) return TRUE;
    SetLastError(ERROR_INVALID_FUNCTION); return FALSE;
}
#undef CreateFile
#define CreateFile file_only_open
#define DeviceIoControl storage_fixture
#define main unused_production_formatter_entry
#include "../third_party/fat32format.c"
#undef main
#undef DeviceIoControl
#undef CreateFile

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
    require(format_volume(fixture_file, &params, 3, 1048576, fixture_length, sector),
            "production FAT32 formatter succeeds through file-only storage adapter");
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
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--invalid-seek")) {
        seek_to_sect(INVALID_HANDLE_VALUE, 1, 512); return 0;
    }
    if (argc != 2 || strncmp(argv[1], "work\\native-formatter-tests-", 28) ||
        strstr(argv[1], "..") || strchr(argv[1], ':') || strlen(argv[1]) > 160) return 2;
    require(CreateDirectoryA(argv[1], NULL), "unique formatter test directory");
    image_case(argv[1], 8, 512); image_case(argv[1], 64, 512);
    image_case(argv[1], 8, 4096); image_case(argv[1], 64, 4096);
    printf("PASS formatter %u assertions; only ordinary files written; no storage IOCTLs issued\n", checks);
    return 0;
}
