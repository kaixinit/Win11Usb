#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <winioctl.h>
#ifndef IOCTL_DISK_GET_DISK_ATTRIBUTES
#define IOCTL_DISK_GET_DISK_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003c, METHOD_BUFFERED, FILE_ANY_ACCESS)
typedef struct _GET_DISK_ATTRIBUTES {
    DWORD Version;
    DWORD Reserved1;
    DWORDLONG Attributes;
} GET_DISK_ATTRIBUTES;
#define DISK_ATTRIBUTE_OFFLINE 0x0000000000000001ull
#define DISK_ATTRIBUTE_READ_ONLY 0x0000000000000002ull
#endif

namespace wu {
constexpr uint64_t GiB = 1024ull * 1024 * 1024, MiB = 1024ull * 1024, Fat32Limit = 0xffffffffull;
struct Error : std::runtime_error {
    std::wstring text;
    explicit Error(const std::wstring &s);
};
struct Cancelled : Error {
    Cancelled() : Error(L"用户已请求结束制作。") {}
};
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE p) : h(p) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&p) noexcept : h(p.h) { p.h = INVALID_HANDLE_VALUE; }
    Handle &operator=(Handle &&p) noexcept {
        if (this != &p) {
            reset();
            h = p.h;
            p.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
    void reset(HANDLE p = INVALID_HANDLE_VALUE) {
        if (valid())
            CloseHandle(h);
        h = p;
    }
    HANDLE release() {
        HANDLE p = h;
        h = INVALID_HANDLE_VALUE;
        return p;
    }
    operator HANDLE() const { return h; }
};
std::wstring WinError(DWORD code = GetLastError());
std::wstring Lower(std::wstring s), Trim(std::wstring s), FormatSize(uint64_t n);
std::string Utf8(const std::wstring &s);
std::wstring Wide(const std::string &s);
std::wstring Json(const std::wstring &s);
std::wstring ExePath(), Parent(const std::wstring &p), BaseName(const std::wstring &p);
std::wstring FullPath(const std::wstring &p), SystemFile(const wchar_t *name);
std::wstring FinalPath(const std::wstring &p);
void WriteUtf8(const std::wstring &path, const std::wstring &text);
bool FileExists(const std::wstring &p);
uint64_t FileSize(const std::wstring &p);
bool IsAdmin();
void CheckCancel(const std::atomic<bool> &stop);
std::vector<BYTE> ResourceBytes(int id);

struct Partition {
    DWORD number = 0;
    uint64_t offset = 0, size = 0;
    BYTE mbrType = 0;
    bool active = false;
    GUID gptType{}, gptId{};
};
struct Disk {
    DWORD number = 0, sector = 0, style = PARTITION_STYLE_RAW;
    uint64_t size = 0;
    bool offline = false, readOnly = false, system = false, bootKnown = false;
    bool descriptorKnown = true, numberKnown = true, capacityKnown = true, attributesKnown = true,
         layoutKnown = true;
    DWORD reportedBus = 0, layoutEntries = 0;
    std::wstring friendly, serial, pnp, path, layout, blocked, tableId, usbEvidence;
    std::vector<std::wstring> probeErrors;
    std::vector<Partition> partitions;
    std::vector<std::wstring> letters;
    bool complete() const {
        return descriptorKnown && numberKnown && capacityKnown && attributesKnown && layoutKnown &&
               probeErrors.empty();
    }
    bool eligible() const { return complete() && blocked.empty(); }
    std::wstring Metadata() const;
};
struct Volume {
    std::wstring guid, device;
    std::vector<std::wstring> paths;
    struct Extent {
        DWORD disk;
        uint64_t offset, size;
    };
    std::vector<Extent> extents;
};
struct Catalog {
    std::vector<Disk> disks;
    std::vector<std::wstring> diagnostics;
    bool bootKnown = false;
    DWORD interfaces = 0, nonUsb = 0, failed = 0;
};
bool IsUsbStorage(DWORD reportedBus, const std::vector<std::wstring> &deviceAncestors);
void ParseDiskGeometry(Disk &disk, const std::vector<BYTE> &bytes);
void ParseStorageDescriptor(Disk &disk, const std::vector<BYTE> &bytes);
void ParseDriveLayout(Disk &disk, const std::vector<BYTE> &bytes);
std::vector<Volume> ReadVolumes();
Catalog ReadDisks();
Disk ReadDisk(const std::wstring &path, const std::wstring &pnp = L"", bool includeLayout = true);
Disk RecheckDisk(const Disk &expected, bool checkLayout);
std::vector<DWORD> PathDisks(const std::wstring &path);
void AssertOutside(const std::wstring &path, const Disk &target, const std::wstring &purpose);
std::wstring DiskJson(const Catalog &catalog);
bool SameIdentity(const Disk &a, const Disk &b);
std::wstring LayoutSignature(const Disk &d);

struct ImageIdentity {
    std::wstring display = L"Windows 安装介质（版本未识别）", label = L"WINSETUP", note, version, name,
                 description, edition, installation, product, release, status;
    int major = 0, minor = 0, build = 0, revision = 0, arch = 9;
    bool ltsc = false;
};
ImageIdentity ResolveIdentity(ImageIdentity identity, const std::wstring &isoName);
struct File {
    std::wstring full, relative;
    uint64_t size = 0;
};
struct ImagePlan {
    bool split = false;
    uint64_t total = 0;
    std::wstring wim;
    std::vector<File> files, images;
};
ImagePlan PlanImages(const std::wstring &root, const std::vector<File> &files);
void CheckSwm(const std::vector<File> &files);

enum class Prompt { Format, Split, Copy, Cleanup, EjectUsb, EjectIso };
struct Progress {
    int stage = 0;
    double overall = -1, current = 0;
    std::wstring title, file;
    uint64_t elapsed = 0;
};
struct Hooks {
    std::function<void(const std::wstring &)> workReady;
    std::function<void(const std::wstring &)> log;
    std::function<void(const Progress &)> progress;
    std::function<bool(Prompt, const std::wstring &)> ask;
};
struct Request {
    std::wstring iso, fs = L"FAT32", launcher;
    Disk disk;
};
struct Prepared {
    ImagePlan plan;
    ImageIdentity image;
    std::wstring source, work, staging;
    wchar_t letter = 0;
};
struct Result {
    bool copied = false, cancelled = false, rebuildStarted = false;
    std::wstring error, work;
};
class Backend {
  public:
    virtual ~Backend() = default;
    virtual Prepared Prepare(const Request &, const Hooks &, std::atomic<bool> &) = 0;
    virtual void Validate(const Request &, const Prepared &, bool originalLayout) = 0;
    virtual std::wstring Format(const Request &, const Prepared &, const Hooks &, std::atomic<bool> &) = 0;
    virtual std::vector<File> Split(const Prepared &, const Hooks &, std::atomic<bool> &) = 0;
    virtual void Copy(const Request &, const Prepared &, const std::wstring &, const std::vector<File> &,
                      const Hooks &, std::atomic<bool> &) = 0;
    virtual void Verify(const Request &, const Prepared &, const std::wstring &, const std::vector<File> &,
                        std::atomic<bool> &) = 0;
    virtual void Cleanup(const Prepared &, const std::vector<File> &) = 0;
    virtual void EjectUsb(const Request &, const std::wstring &) = 0;
    virtual void EjectIso() = 0;
    virtual void Abort(bool forced) noexcept = 0;
};
Result RunWorkflow(const Request &, Backend &, const Hooks &, std::atomic<bool> &);
std::unique_ptr<Backend> CreateNativeBackend();
int RunUi(HINSTANCE, bool sample, bool running, bool empty, bool promptPreview = false,
          bool blockedPreview = false);
void WriteDependencies(const std::wstring &path);
} // namespace wu
