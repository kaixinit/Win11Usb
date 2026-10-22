#include "platform.hpp"
#include <algorithm>
#include <cfgmgr32.h>
#include <shlobj.h>
#include <sstream>
#include <virtdisk.h>

namespace wu {
namespace {
std::wstring Bare(std::wstring p) {
    if (!p.empty() && p.back() == L'\\')
        p.pop_back();
    return p;
}
void Io(HANDLE h, DWORD code, void *in = nullptr, DWORD size = 0) {
    DWORD n = 0;
    if (!DeviceIoControl(h, code, in, size, nullptr, 0, &n, nullptr))
        throw Error(L"磁盘操作未完成（" + std::to_wstring(code) + L"）：" + WinError());
}
void EnableVolumePrivilege() {
    Handle token;
    HANDLE raw;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw))
        throw Error(WinError());
    token.reset(raw);
    TOKEN_PRIVILEGES p{};
    p.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_MANAGE_VOLUME_NAME, &p.Privileges[0].Luid))
        throw Error(WinError());
    p.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(0);
    if (!AdjustTokenPrivileges(token, FALSE, &p, sizeof p, nullptr, nullptr) ||
        GetLastError() == ERROR_NOT_ALL_ASSIGNED)
        throw Error(L"当前账户缺少磁盘管理权限。尚未清盘。");
}
std::wstring WorkBase() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
        throw Error(L"无法获取本机临时目录。");
    std::wstring base = raw;
    CoTaskMemFree(raw);
    return base;
}
uint64_t FreeBytes(const std::wstring &path) {
    ULARGE_INTEGER free{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &free, nullptr, nullptr))
        throw Error(L"无法确认可用容量：" + WinError());
    return free.QuadPart;
}
struct IsoMount {
    Module library{L"virtdisk.dll"};
    Handle disk;
    bool owned = false;
    decltype(&OpenVirtualDisk) open = library.Proc<decltype(&OpenVirtualDisk)>("OpenVirtualDisk");
    decltype(&AttachVirtualDisk) attach = library.Proc<decltype(&AttachVirtualDisk)>("AttachVirtualDisk");
    decltype(&DetachVirtualDisk) detach = library.Proc<decltype(&DetachVirtualDisk)>("DetachVirtualDisk");
    decltype(&GetVirtualDiskPhysicalPath) path =
        library.Proc<decltype(&GetVirtualDiskPhysicalPath)>("GetVirtualDiskPhysicalPath");
    std::wstring device;
    std::wstring Physical() {
        wchar_t p[1024];
        ULONG bytes = sizeof p;
        DWORD e = path(disk, &bytes, p);
        if (e)
            return L"";
        return p;
    }
    std::wstring Mount(const std::wstring &iso, const Hooks &h, std::atomic<bool> &stop) {
        CheckCancel(stop);
        VIRTUAL_STORAGE_TYPE t{};
        t.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_ISO;
        t.VendorId = {0xec984aec, 0xa0f9, 0x47e9, {0x90, 0x1f, 0x71, 0x41, 0x5a, 0x66, 0x34, 0x5b}};
        OPEN_VIRTUAL_DISK_PARAMETERS p{};
        p.Version = OPEN_VIRTUAL_DISK_VERSION_1;
        HANDLE raw;
        DWORD e = open(&t, iso.c_str(),
                       (VIRTUAL_DISK_ACCESS_MASK)(VIRTUAL_DISK_ACCESS_ATTACH_RO |
                                                  VIRTUAL_DISK_ACCESS_GET_INFO | VIRTUAL_DISK_ACCESS_DETACH),
                       OPEN_VIRTUAL_DISK_FLAG_NONE, &p, &raw);
        if (e)
            throw Error(L"ISO 打开失败：" + WinError(e));
        disk.reset(raw);
        device = Physical();
        if (device.empty()) {
            if (h.log)
                h.log(L"正在通过系统原生接口只读挂载 ISO…");
            ATTACH_VIRTUAL_DISK_PARAMETERS a{};
            a.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
            e = attach(disk, nullptr,
                       (ATTACH_VIRTUAL_DISK_FLAG)(ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY |
                                                  ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME),
                       0, &a, nullptr);
            if (e)
                throw Error(L"ISO 挂载失败：" + WinError(e));
            owned = true;
            device = Physical();
        } else if (h.log)
            h.log(L"使用已挂载的 ISO。取消任务时将保留原有挂载。");
        uint64_t start = GetTickCount64();
        while (GetTickCount64() - start < 30000) {
            CheckCancel(stop);
            if (device.empty())
                device = Physical();
            auto slash = device.find_last_of(L'\\');
            auto expected = Lower(device.substr(slash == device.npos ? 0 : slash + 1));
            if (!expected.empty())
                for (wchar_t letter = L'A'; letter <= L'Z'; letter++) {
                    std::wstring root{letter, L':', L'\\'};
                    if (GetDriveTypeW(root.c_str()) != DRIVE_CDROM)
                        continue;
                    wchar_t target[4096];
                    std::wstring dos{letter, L':'};
                    if (QueryDosDeviceW(dos.c_str(), target, 4096)) {
                        auto t = Lower(target);
                        auto i = t.find_last_of(L'\\');
                        if (t.substr(i == t.npos ? 0 : i + 1) == expected) {
                            wchar_t guid[MAX_PATH];
                            if (!GetVolumeNameForVolumeMountPointW(root.c_str(), guid, MAX_PATH))
                                throw Error(L"无法锁定 ISO 源卷身份。");
                            if (h.log)
                                h.log(L"ISO 已就绪：" + root);
                            return guid;
                        }
                    }
                }
            Sleep(100);
        }
        throw Error(L"ISO 挂载后 30 秒内未出现可读取的 DVD 卷，尚未清盘。");
    }
    void Detach() {
        if (!disk.valid())
            return;
        DWORD e = detach(disk, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
        if (e)
            throw Error(L"ISO 弹出失败：" + WinError(e));
        owned = false;
        disk.reset();
    }
};
class NativeBackend final : public Backend {
    std::unique_ptr<IsoMount> iso;
    Handle sourceFile, helperPin;
    CommandRunner commands;
    std::wstring volume, afterLayout, helper, work;
    uint64_t partitionOffset = MiB, partitionSize = 0;
    Disk pinned;
    std::atomic<bool> aborted{false};
    std::unique_ptr<TaskWorkspace> workspace;
    void SourceChecks(const Request &r, const Prepared &p) {
        if (!workspace || p.work != workspace->Root())
            throw Error(L"本次任务目录身份未确认。");
        workspace->Validate();
        AssertOutside(r.iso, r.disk, L"安装 ISO");
        AssertOutside(r.launcher, r.disk, L"程序");
        AssertOutside(p.work, r.disk, L"本次临时目录");
        if (!FileExists(p.source + L"efi\\boot\\bootx64.efi") || !FileExists(p.source + L"sources\\boot.wim"))
            throw Error(L"安装介质已不可读取，停止写入。");
    }
    void TargetVolume(const Request &r) {
        auto now = RecheckDisk(r.disk, false);
        if (now.layout != afterLayout)
            throw Error(L"新分区布局发生变化，停止后续操作。");
        auto vs = ReadVolumes();
        for (auto &v : vs)
            if (Lower(v.guid) == Lower(volume)) {
                if (v.extents.size() != 1 || v.extents[0].disk != r.disk.number ||
                    v.extents[0].offset != partitionOffset || v.extents[0].size != partitionSize)
                    throw Error(L"目标卷身份发生变化。");
                return;
            }
        throw Error(L"目标卷已移除，停止写入。");
    }
    void WaitVolume(const Request &r, const Hooks &h, std::atomic<bool> &stop) {
        uint64_t start = GetTickCount64(), last = start;
        while (GetTickCount64() - start < 30000) {
            CheckCancel(stop);
            auto now = RecheckDisk(r.disk, false);
            if (GetTickCount64() - last > 5000) {
                if (h.log)
                    h.log(L"正在等待新分区卷就绪…");
                last = GetTickCount64();
            }
            if (!now.partitions.empty() && !CreatedPartitionReady(now, partitionOffset, partitionSize))
                throw Error(L"新分区布局核验失败。");
            if (!CreatedPartitionReady(now, partitionOffset, partitionSize)) {
                Sleep(100);
                continue;
            }
            for (auto &v : ReadVolumes())
                if (v.extents.size() == 1 && v.extents[0].disk == r.disk.number &&
                    v.extents[0].offset == partitionOffset && v.extents[0].size == partitionSize) {
                    volume = v.guid;
                    for (auto &path : v.paths)
                        if (!DeleteVolumeMountPointW(path.c_str()))
                            throw Error(L"不能暂时移除新分区盘符：" + WinError());
                    afterLayout = now.layout;
                    return;
                }
            Sleep(100);
        }
        throw Error(L"等待新分区卷超时；未进行格式化。");
    }

  public:
    Prepared Prepare(const Request &r, const Hooks &h, std::atomic<bool> &stop) override {
        if (!IsAdmin())
            throw Error(L"制作需要管理员权限。请使用原生制作版，接受 Windows UAC 后继续。");
        EnableVolumePrivilege();
        RecheckDisk(r.disk, true);
        if (!r.disk.bootKnown)
            throw Error(L"启动分区映射未完整确认，尚未清盘。");
        partitionSize = PlannedPartitionSize(r.disk);
        if (Lower(BaseName(r.iso)).size() < 5 || Lower(r.iso).substr(r.iso.size() - 4) != L".iso")
            throw Error(L"请选择 .iso 安装文件。");
        AssertOutside(r.iso, r.disk, L"安装 ISO");
        AssertOutside(r.launcher, r.disk, L"程序");
        sourceFile.reset(CreateFileW(r.iso.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!sourceFile.valid())
            throw Error(L"安装 ISO 不可读取或正在被改写：" + WinError());
        CheckCancel(stop);
        Prepared p;
        workspace = TaskWorkspace::Create(WorkBase(), r.disk);
        p.work = workspace->Root();
        work = p.work;
        if (h.workReady)
            h.workReady(p.work);
        p.staging = workspace->Directory(L"staging");
        workspace->Directory(L"wim-temp");
        if (h.log)
            h.log(L"任务日志目录：" + p.work);
        iso = std::make_unique<IsoMount>();
        p.source = iso->Mount(r.iso, h, stop);
        auto files = ListFiles(p.source, stop);
        p.plan = PlanImages(p.source, files);
        for (auto path : {L"setup.exe", L"sources\\boot.wim", L"efi\\boot\\bootx64.efi"})
            if (!FileExists(p.source + path) || !FileSize(p.source + path))
                throw Error(std::wstring(L"ISO 缺少 x64 安装启动文件：") + path + L"，尚未清盘。");
        p.image = ReadTaskWimIdentity(p.plan.images[0].full, r.iso, p.work, r.disk, h);
        if (p.image.arch != 9)
            throw Error(L"当前原生验证版仅支持 x64 安装映像。尚未清盘。");
        if (p.image.label.empty() || p.image.label.size() > 11 ||
            p.image.label.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != p.image.label.npos)
            throw Error(L"卷标验证失败。");
        if (p.plan.total > r.disk.size || r.disk.size - p.plan.total < 128 * MiB + r.disk.size / 100)
            throw Error(L"U 盘容量不足以容纳安装文件和文件系统开销。尚未清盘。");
        if (p.plan.split) {
            if (!FileExists(SystemFile(L"dism.exe")))
                throw Error(L"精简系统缺少 dism.exe，无法拆分映像。尚未清盘。");
            auto args = PrepareImageInfoArguments(p, *workspace);
            if (h.log)
                h.log(L"正在只读检查 DISM 组件与安装映像，尚未清盘…");
            commands.Run(SystemFile(L"dism.exe"), args, p.work, p.work + L"\\dism-preflight.log", h, stop,
                         2 * 60 * 1000);
        }
        if (r.fs == L"NTFS" && !FileExists(SystemFile(L"format.com")))
            throw Error(L"精简系统缺少 format.com。尚未清盘。");
        if (r.fs == L"FAT32") {
            auto bytes = ResourceBytes(201);
            if (bytes.size() < 1024 || bytes[0] != 'M' || bytes[1] != 'Z')
                throw Error(L"内置原生 FAT32 工具不完整。尚未清盘。");
            helper = p.work + L"\\fat32format-x64.exe";
            Handle out(CreateFileW(helper.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!out.valid())
                throw Error(L"不能准备 FAT32 工具。尚未清盘。");
            DWORD n = 0;
            if (!WriteFile(out, bytes.data(), (DWORD)bytes.size(), &n, nullptr) || n != bytes.size() ||
                !FlushFileBuffers(out))
                throw Error(L"FAT32 工具写入不完整。尚未清盘。");
            out.reset();
            helperPin.reset(CreateFileW(helper.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        0, nullptr));
            if (!helperPin.valid())
                throw Error(L"不能固定 FAT32 工具身份。尚未清盘。");
            std::vector<BYTE> actual(bytes.size());
            if (!ReadFile(helperPin, actual.data(), (DWORD)actual.size(), &n, nullptr) || n != bytes.size() ||
                actual != bytes)
                throw Error(L"FAT32 工具校验失败。尚未清盘。");
            for (auto item : {std::pair<int, const wchar_t *>{202, L"fat32format-modified.c"},
                              {203, L"COPYING.GPL-2.0.txt"},
                              {204, L"fat32format-NOTICE.txt"},
                              {205, L"fat32formatsrc-vendor.zip"}}) {
                auto data = ResourceBytes(item.first);
                Handle f(CreateFileW((p.work + L"\\" + item.second).c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (!f.valid() || !WriteFile(f, data.data(), (DWORD)data.size(), &n, nullptr) ||
                    n != data.size())
                    throw Error(L"不能保存第三方工具许可证与对应源码。尚未清盘。");
            }
        }
        for (auto &letter : r.disk.letters)
            if (letter.size() == 3 && letter[0] >= L'D' && letter[0] <= L'Z') {
                p.letter = letter[0];
                break;
            }
        if (!p.letter) {
            DWORD used = GetLogicalDrives();
            for (wchar_t c = L'D'; c <= L'Z'; c++)
                if (!(used & (1u << (c - L'A')))) {
                    p.letter = c;
                    break;
                }
        }
        if (!p.letter)
            throw Error(L"没有可用盘符。尚未清盘。");
        pinned = r.disk;
        SourceChecks(r, p);
        if (h.log)
            h.log(L"FAT32 单文件上限：4 GiB（4,294,967,295 字节）；NTFS 沿用同一拆分规则。");
        return p;
    }
    void Validate(const Request &r, const Prepared &p, bool original) override {
        if (aborted)
            throw Cancelled();
        RecheckDisk(r.disk, original);
        SourceChecks(r, p);
        if (!original && !volume.empty())
            TargetVolume(r);
    }
    std::wstring Format(const Request &r, const Prepared &p, const Hooks &h,
                        std::atomic<bool> &stop) override {
        Validate(r, p, true);
        CheckCancel(stop);
        std::vector<Handle> locks;
        std::vector<Volume> old;
        for (auto &v : ReadVolumes())
            for (auto &e : v.extents)
                if (e.disk == r.disk.number) {
                    if (v.extents.size() != 1)
                        throw Error(L"所选磁盘包含跨盘卷，停止清盘。");
                    old.push_back(v);
                    break;
                }
        for (auto &v : old) {
            Handle handle(CreateFileW(Bare(v.guid).c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                      nullptr));
            if (!handle.valid())
                throw Error(L"不能独占旧分区：" + WinError());
            Io(handle, FSCTL_LOCK_VOLUME);
            Io(handle, FSCTL_DISMOUNT_VOLUME);
            locks.push_back(std::move(handle));
        }
        CheckCancel(stop);
        Validate(r, p, true);
        Handle disk(CreateFileW(r.disk.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!disk.valid())
            throw Error(L"不能打开所选整盘：" + WinError());
        auto held = ReadDisk(r.disk.path, r.disk.pnp);
        if (!SameIdentity(r.disk, held) || held.layout != r.disk.layout)
            throw Error(L"清盘前最后一次整盘复核失败。");
        CREATE_DISK create{};
        create.PartitionStyle = PARTITION_STYLE_MBR;
        GUID diskId{};
        if (FAILED(CoCreateGuid(&diskId)))
            throw Error(L"无法生成新磁盘标识，尚未清盘。");
        create.Mbr.Signature = diskId.Data1;
        if (!create.Mbr.Signature)
            create.Mbr.Signature = 0x57555342;
        for (auto &v : old)
            for (auto &path : v.paths)
                if (!DeleteVolumeMountPointW(path.c_str()))
                    throw Error(L"不能释放旧分区挂载点：" + WinError());
        CheckCancel(stop);
        if (h.log)
            h.log(L"删除磁盘 " + std::to_wstring(r.disk.number) + L" 的全部旧分区…");
        Io(disk, IOCTL_DISK_DELETE_DRIVE_LAYOUT);
        locks.clear();
        DiskRebuildOps rebuild;
        rebuild.read = [&](bool includeLayout) { return ReadDisk(r.disk.path, r.disk.pnp, includeLayout); };
        rebuild.refresh = [&] { Io(disk, IOCTL_DISK_UPDATE_PROPERTIES); };
        rebuild.initialize = [&](DWORD signature) {
            SourceChecks(r, p);
            CheckCancel(stop);
            create.Mbr.Signature = signature;
            Io(disk, IOCTL_DISK_CREATE_DISK, &create, sizeof create);
        };
        InitializeEmptyMbr(r.disk, create.Mbr.Signature, rebuild, h, stop);
        CheckCancel(stop);
        SourceChecks(r, p);
        if (h.log)
            h.log(L"空 MBR 和原 U 盘身份已核验，建立一个全容量新分区…");
        std::vector<BYTE> buffer(FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) +
                                 4 * sizeof(PARTITION_INFORMATION_EX));
        auto layout = (DRIVE_LAYOUT_INFORMATION_EX *)buffer.data();
        layout->PartitionStyle = PARTITION_STYLE_MBR;
        layout->PartitionCount = 4;
        layout->Mbr.Signature = create.Mbr.Signature;
        for (int i = 0; i < 4; i++) {
            layout->PartitionEntry[i].PartitionStyle = PARTITION_STYLE_MBR;
            layout->PartitionEntry[i].RewritePartition = TRUE;
        }
        auto &part = layout->PartitionEntry[0];
        part.StartingOffset.QuadPart = partitionOffset;
        part.PartitionLength.QuadPart = partitionSize;
        part.PartitionNumber = 1;
        part.Mbr.PartitionType = r.fs == L"FAT32" ? 0x0c : 0x07;
        part.Mbr.BootIndicator = TRUE;
        part.Mbr.RecognizedPartition = TRUE;
        part.Mbr.HiddenSectors = (DWORD)(partitionOffset / r.disk.sector);
        CheckCancel(stop);
        Io(disk, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, layout, (DWORD)buffer.size());
        Io(disk, IOCTL_DISK_UPDATE_PROPERTIES);
        disk.reset();
        WaitVolume(r, h, stop);
        CheckCancel(stop);
        TargetVolume(r);
        if (h.log)
            h.log(L"新分区已核验。使用卷 GUID 格式化，成功前不主动分配盘符。");
        if (r.fs == L"FAT32")
            commands.Run(helper,
                         {L"--confirmed-volume", Bare(volume), std::to_wstring(r.disk.number),
                          std::to_wstring(partitionOffset), std::to_wstring(partitionSize),
                          std::to_wstring(r.disk.sector)},
                         p.work, p.work + L"\\fat32format.log", h, stop, 15 * 60 * 1000);
        else
            commands.Run(SystemFile(L"format.com"),
                         {volume, L"/FS:NTFS", L"/Q", L"/X", L"/V:" + p.image.label}, p.work,
                         p.work + L"\\format-ntfs.log", h, stop, 15 * 60 * 1000, "Y\r\n\r\n");
        TargetVolume(r);
        if (!SetVolumeLabelW(volume.c_str(), p.image.label.c_str()))
            throw Error(L"文件系统已格式化，但卷标设置失败：" + WinError());
        wchar_t fs[64], label[64];
        if (!GetVolumeInformationW(volume.c_str(), label, 64, nullptr, nullptr, nullptr, fs, 64) ||
            std::wstring(fs) != r.fs || std::wstring(label) != p.image.label)
            throw Error(L"文件系统或卷标复核未通过，停止复制。");
        if (FreeBytes(volume) < p.plan.total && !p.plan.split)
            throw Error(L"格式化后的实际可用容量不足。");
        std::wstring letter{p.letter, L':', L'\\'};
        DWORD used = GetLogicalDrives();
        if (used & (1u << (p.letter - L'A'))) {
            wchar_t existing[MAX_PATH];
            if (!GetVolumeNameForVolumeMountPointW(letter.c_str(), existing, MAX_PATH) ||
                Lower(existing) != Lower(volume))
                throw Error(L"预定盘符已被其他设备占用，保留格式化分区并停止。");
        } else if (!SetVolumeMountPointW(letter.c_str(), volume.c_str()))
            throw Error(L"格式化成功，但盘符分配失败：" + WinError());
        TargetVolume(r);
        return letter;
    }
    std::vector<File> Split(const Prepared &p, const Hooks &h, std::atomic<bool> &stop) override {
        if (!workspace)
            throw Error(L"本次任务目录不可用。");
        auto args = PrepareSplitArguments(p, *workspace);
        commands.Run(SystemFile(L"dism.exe"), args, p.work, p.work + L"\\dism-split.log", h, stop);
        auto files = ListFiles(p.staging + L"\\", stop);
        CheckSwm(files);
        return files;
    }
    void Copy(const Request &r, const Prepared &p, const std::wstring &, const std::vector<File> &swm,
              const Hooks &h, std::atomic<bool> &stop) override {
        TargetVolume(r);
        auto files = InstallationFiles(p, swm);
        uint64_t total = 0;
        for (auto &f : files) {
            if (UINT64_MAX - total < f.size)
                throw Error(L"文件容量溢出。");
            total += f.size;
        }
        if (FreeBytes(volume) < total + 64 * MiB)
            throw Error(L"目标文件系统实际空间不足。");
        CopyFilesNative(files, volume, h, stop);
    }
    void Verify(const Request &r, const Prepared &p, const std::wstring &, const std::vector<File> &swm,
                std::atomic<bool> &stop) override {
        TargetVolume(r);
        VerifyFilesNative(InstallationFiles(p, swm), volume, stop);
        for (auto f : {L"efi\\boot\\bootx64.efi", L"sources\\boot.wim", L"setup.exe"})
            if (!FileSize(volume + f))
                throw Error(L"关键安装启动文件校验失败。");
        if (p.plan.split && FileExists(volume + L"sources\\install.wim"))
            throw Error(L"拆分后目标不应保留 install.wim。");
        Handle h(CreateFileW(Bare(volume).c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h.valid() || !FlushFileBuffers(h))
            throw Error(L"目标卷写入缓存刷新失败：" + WinError());
    }
    void Cleanup(const Prepared &p, const std::vector<File> &swm) override {
        if (!workspace || p.work != work || Lower(p.staging) != Lower(workspace->Directory(L"staging")))
            throw Error(L"临时目录身份校验失败。");
        workspace->CleanupSwm(swm);
    }
    void EjectUsb(const Request &r, const std::wstring &) override {
        TargetVolume(r);
        Handle handle(CreateFileW(Bare(volume).c_str(), GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!handle.valid())
            throw Error(WinError());
        if (!FlushFileBuffers(handle))
            throw Error(WinError());
        Io(handle, FSCTL_LOCK_VOLUME);
        Io(handle, FSCTL_DISMOUNT_VOLUME);
        Io(handle, FSCTL_UNLOCK_VOLUME);
        handle.reset();
        DEVINST dev = 0;
        std::wstring pnp = r.disk.pnp;
        if (CM_Locate_DevNodeW(&dev, pnp.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            throw Error(L"不能定位 USB 设备。");
        PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
        wchar_t name[512]{};
        CONFIGRET e = CM_Request_Device_EjectW(dev, &veto, name, 512, 0);
        if (e != CR_SUCCESS)
            throw Error(L"设备仍被使用（CM " + std::to_wstring(e) + L"，veto " + std::to_wstring(veto) +
                        L"）。请稍后通过系统安全弹出。");
    }
    void EjectIso() override {
        if (iso)
            iso->Detach();
    }
    void Abort(bool forced) noexcept override {
        aborted = true;
        commands.Kill();
        if (!forced && iso && iso->owned) {
            try {
                iso->Detach();
            } catch (...) {
            }
        }
    }
};
} // namespace
std::unique_ptr<Backend> CreateNativeBackend() { return std::make_unique<NativeBackend>(); }
} // namespace wu
