#include "platform.hpp"
#include <algorithm>

namespace wu {
namespace {
std::wstring ChildPath(const std::wstring &base, const std::wstring &name) {
    return base + (base.back() == L'\\' ? L"" : L"\\") + name;
}
std::wstring NewId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw Error(L"无法生成本次临时文件编号。尚未清盘。");
    wchar_t name[40];
    StringFromGUID2(id, name, 40);
    return std::wstring(name + 1, 36);
}
std::wstring Actual(HANDLE handle) {
    std::vector<wchar_t> path(32768);
    DWORD n = GetFinalPathNameByHandleW(handle, path.data(), (DWORD)path.size(), FILE_NAME_NORMALIZED);
    if (!n || n >= path.size())
        throw Error(L"无法确认任务目录的实际路径：" + WinError());
    return {path.data(), n};
}
bool SameFile(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
           a.nFileIndexLow == b.nFileIndexLow;
}
} // namespace
uint64_t CheckWritableDirectory(const std::wstring &path, uint64_t reserve) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(L"临时目录不存在或类型不安全：" + path);
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
        throw Error(L"无法确认临时目录可用空间：" + path + L"\r\n" + WinError());
    if (available.QuadPart < reserve)
        throw Error(L"临时目录空间不足：" + path + L"\r\n所需余量：" + FormatSize(reserve) + L"；当前可用：" +
                    FormatSize(available.QuadPart));
    auto probe = ChildPath(path, L"write-check-" + NewId() + L".tmp");
    Handle file(CreateFileW(probe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!file.valid())
        throw Error(L"临时目录不可写：" + path + L"\r\n" + WinError());
    const char content[] = "Win11Usb task directory check";
    DWORD n = 0;
    if (!WriteFile(file, content, sizeof content, &n, nullptr) || n != sizeof content ||
        !FlushFileBuffers(file))
        throw Error(L"临时目录写入检查失败：" + path + L"\r\n" + WinError());
    return available.QuadPart;
}
TaskWorkspace::DirectoryPin TaskWorkspace::Pin(const std::wstring &path) {
    DirectoryPin entry;
    entry.path = FullPath(path);
    // No FILE_SHARE_DELETE: a live task directory cannot be renamed or replaced.
    entry.handle.reset(CreateFileW(entry.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!entry.handle.valid() || !GetFileInformationByHandle(entry.handle, &entry.identity))
        throw Error(L"无法固定本次任务目录：" + entry.path + L"\r\n" + WinError());
    if (!(entry.identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (entry.identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(L"本次任务目录不是普通目录：" + entry.path);
    entry.actual = Actual(entry.handle);
    AssertOutside(entry.path, target, L"本次任务目录");
    return entry;
}
void TaskWorkspace::Check(const DirectoryPin &entry) const {
    Handle current(CreateFileW(entry.path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!current.valid() || !GetFileInformationByHandle(current, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !SameFile(info, entry.identity) ||
        Lower(Actual(current)) != Lower(entry.actual))
        throw Error(L"本次任务目录的身份或实际路径变化，停止后续操作：" + entry.path);
}
TaskWorkspace::TaskWorkspace(const std::wstring &path, const Disk &disk) : target(disk) {
    if (path.empty())
        throw Error(L"未指定本次任务目录。");
    root = Pin(path);
}
std::unique_ptr<TaskWorkspace> TaskWorkspace::Create(const std::wstring &base, const Disk &disk) {
    TaskWorkspace parent(base, disk);
    auto path = ChildPath(parent.Root(), L"Win11Usb-Native-" + NewId());
    if (!CreateDirectoryW(path.c_str(), nullptr))
        throw Error(L"无法创建独立任务目录：" + path + L"\r\n" + WinError());
    auto task = std::make_unique<TaskWorkspace>(path, disk);
    CheckWritableDirectory(task->Root(), 64 * MiB);
    return task;
}
void TaskWorkspace::Validate() const {
    Check(root);
    for (const auto &child : children)
        Check(child.second);
}
std::wstring TaskWorkspace::Directory(const std::wstring &name) {
    if (name.empty() || name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-_") != name.npos)
        throw Error(L"任务子目录名称无效。");
    Validate();
    auto found = children.find(name);
    if (found != children.end())
        return found->second.path;
    auto path = ChildPath(root.path, name);
    MakeDirectory(path);
    auto child = Pin(path);
    if (Lower(child.actual) != Lower(ChildPath(root.actual, name)))
        throw Error(L"任务子目录实际路径超出本次任务：" + path);
    CheckWritableDirectory(path, 64 * MiB);
    children.emplace(name, std::move(child));
    Validate();
    return path;
}
void TaskWorkspace::CleanupSwm(const std::vector<File> &files) {
    CheckSwm(files);
    auto staging = Directory(L"staging");
    auto actual = children.at(L"staging").actual;
    std::vector<Handle> selected;
    for (const auto &file : files) {
        if (Lower(Parent(FullPath(file.full))) != Lower(staging))
            throw Error(L"SWM 清理路径超出本次任务：" + file.full);
        Handle handle(CreateFileW(file.full.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!handle.valid() || !GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            (((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow) != file.size ||
            Lower(Actual(handle)) != Lower(actual + L"\\" + BaseName(file.full)))
            throw Error(L"SWM 文件身份或实际清理范围无法确认：" + file.full);
        selected.push_back(std::move(handle));
    }
    Validate();
    // All files are validated first. Delete the opened objects, not re-resolved paths.
    for (auto &handle : selected) {
        FILE_DISPOSITION_INFO remove{TRUE};
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &remove, sizeof remove))
            throw Error(L"本次 SWM 文件无法清理：" + WinError());
    }
}
std::vector<std::wstring> PrepareSplitArguments(const Prepared &p, TaskWorkspace &workspace) {
    workspace.Validate();
    if (Lower(FullPath(p.work)) != Lower(workspace.Root()) ||
        Lower(FullPath(p.staging)) != Lower(workspace.Directory(L"staging")))
        throw Error(L"拆分目录不属于本次任务。");
    std::atomic<bool> stop = false;
    if (!ListFiles(p.staging + L"\\", stop).empty())
        throw Error(L"本次 SWM 目录非空，停止拆分。");
    auto scratch = workspace.Directory(L"dism-temp");
    auto size = FileSize(p.plan.wim);
    if (size > UINT64_MAX - size / 20 - 64 * MiB)
        throw Error(L"拆分空间计算溢出。");
    CheckWritableDirectory(p.staging, size + size / 20 + 64 * MiB);
    CheckWritableDirectory(scratch, 64 * MiB);
    return {L"/English",
            L"/Split-Image",
            L"/ImageFile:" + p.plan.wim,
            L"/SWMFile:" + p.staging + L"\\install.swm",
            L"/FileSize:4000",
            L"/CheckIntegrity",
            L"/ScratchDir:" + scratch,
            L"/LogPath:" + p.work + L"\\dism-detail.log"};
}
std::vector<std::wstring> PrepareImageInfoArguments(const Prepared &p, TaskWorkspace &workspace) {
    PrepareSplitArguments(p, workspace);
    return {L"/English",
            L"/Get-ImageInfo",
            L"/ImageFile:" + p.plan.wim,
            L"/Index:1",
            L"/ScratchDir:" + workspace.Directory(L"dism-temp"),
            L"/LogPath:" + p.work + L"\\dism-preflight-detail.log"};
}
uint64_t PlannedPartitionSize(const Disk &disk) {
    if ((disk.sector != 512 && disk.sector != 4096) || disk.size < 8 * GiB || disk.size > 2 * 1024 * GiB)
        throw Error(L"整盘容量或逻辑扇区不能用于本流程。尚未清盘。");
    uint64_t size = ((disk.size - 2 * MiB) / MiB) * MiB;
    if ((MiB + size) / disk.sector > UINT32_MAX)
        throw Error(L"整盘容量无法用安全 MBR 布局表示。尚未清盘。");
    return size;
}
bool CreatedPartitionReady(const Disk &disk, uint64_t offset, uint64_t size) {
    return disk.complete() && disk.style == PARTITION_STYLE_MBR && disk.partitions.size() == 1 &&
           disk.partitions[0].offset == offset && disk.partitions[0].size == size;
}
} // namespace wu
