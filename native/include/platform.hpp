#pragma once
#include "win11usb.hpp"
#include <mutex>

namespace wu {
struct Module {
    HMODULE h = nullptr;
    explicit Module(const wchar_t *file)
        : h(LoadLibraryExW(SystemFile(file).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!h)
            throw Error(std::wstring(L"缺少系统组件：") + file + L"。" + WinError());
    }
    ~Module() {
        if (h)
            FreeLibrary(h);
    }
    Module(const Module &) = delete;
    template <class T> T Proc(const char *name) {
        auto p = GetProcAddress(h, name);
        if (!p)
            throw Error(L"系统组件接口不可用：" + Wide(name));
        return reinterpret_cast<T>(p);
    }
};
void MakeDirectory(const std::wstring &path);
uint64_t CheckWritableDirectory(const std::wstring &path, uint64_t reserve = 0);
class TaskWorkspace {
    struct DirectoryPin {
        Handle handle;
        std::wstring path, actual;
        BY_HANDLE_FILE_INFORMATION identity{};
    };
    Disk target;
    DirectoryPin root;
    std::map<std::wstring, DirectoryPin> children;
    DirectoryPin Pin(const std::wstring &path);
    void Check(const DirectoryPin &entry) const;

  public:
    TaskWorkspace(const std::wstring &existingRoot, const Disk &target);
    static std::unique_ptr<TaskWorkspace> Create(const std::wstring &base, const Disk &target);
    const std::wstring &Root() const { return root.path; }
    std::wstring Directory(const std::wstring &name);
    void Validate() const;
    void CleanupSwm(const std::vector<File> &files);
};
std::vector<std::wstring> PrepareSplitArguments(const Prepared &prepared, TaskWorkspace &workspace);
std::vector<std::wstring> PrepareImageInfoArguments(const Prepared &prepared, TaskWorkspace &workspace);
uint64_t PlannedPartitionSize(const Disk &disk);
bool CreatedPartitionReady(const Disk &disk, uint64_t offset, uint64_t size);
// Production and tests use the same bounded initialization/verification sequence.
// Only the caller's retained whole-disk handle may initialize or refresh the disk.
struct DiskRebuildOps {
    std::function<Disk(bool includeLayout)> read;
    std::function<void()> refresh;
    std::function<void(DWORD signature)> initialize;
    std::function<uint64_t()> now;
    std::function<void(DWORD milliseconds)> wait;
};
struct NewVolumeOps : DiskRebuildOps {
    std::function<std::vector<Volume>()> volumes;
};
Volume WaitCreatedVolume(const Disk &expected, DWORD signature, BYTE type, uint64_t offset, uint64_t size,
                         const NewVolumeOps &ops, const Hooks &hooks, std::atomic<bool> &stop);
Disk InitializeEmptyMbr(const Disk &expected, DWORD signature, const DiskRebuildOps &ops, const Hooks &hooks,
                        std::atomic<bool> &stop);
struct VolumeState {
    std::wstring fs, label;
    uint64_t free = 0, total = 0;
    DWORD cluster = 0;
};
std::vector<std::wstring> FormatArguments(const Request &request, const Prepared &prepared,
                                          const std::wstring &volume, uint64_t offset, uint64_t size);
VolumeState WaitFileSystem(const std::wstring &fs, const std::function<VolumeState()> &probe,
                           const std::function<void()> &validate, const Hooks &hooks, std::atomic<bool> &stop,
                           std::function<uint64_t()> now = {}, std::function<void(DWORD)> wait = {});
uint64_t ManifestBytes(const std::vector<File> &files);
uint64_t RequiredCopyBytes(const std::vector<File> &files, DWORD cluster);
std::vector<File> ListFiles(const std::wstring &root, std::atomic<bool> &stop);
ImageIdentity ReadWimIdentity(const std::wstring &image, const std::wstring &iso, const std::wstring &scratch,
                              const Hooks &hooks = {});
ImageIdentity ReadTaskWimIdentity(const std::wstring &image, const std::wstring &iso,
                                  const std::wstring &work, const Disk &target, const Hooks &hooks = {});
ImageIdentity ParseWimXml(const std::wstring &xml, const std::wstring &iso);
void CopyFilesNative(const std::vector<File> &files, const std::wstring &target, const Hooks &hooks,
                     std::atomic<bool> &stop);
void VerifyFilesNative(const std::vector<File> &files, const std::wstring &target, std::atomic<bool> &stop,
                       const Hooks &hooks = {});
void CheckSplitParts(const std::vector<File> &files, std::atomic<bool> &stop);
std::vector<File> InstallationFiles(const Prepared &prepared, const std::vector<File> &swm);
std::wstring QuoteArg(const std::wstring &arg);
class CommandRunner {
    std::mutex mutex;
    Handle job;

  public:
    DWORD Run(const std::wstring &program, const std::vector<std::wstring> &args, const std::wstring &cwd,
              const std::wstring &logfile, const Hooks &hooks, std::atomic<bool> &stop,
              DWORD timeoutMs = 2 * 60 * 60 * 1000, const std::string &input = "");
    void Kill() noexcept;
};
} // namespace wu
