#include "platform.hpp"
#include <algorithm>
#include <cstring>
#include <exception>

namespace wu {
uint64_t ManifestBytes(const std::vector<File> &files) {
    if (files.empty())
        throw Error(L"安装文件清单为空。");
    uint64_t total = 0;
    std::set<std::wstring> paths;
    for (const auto &f : files) {
        auto path = Lower(f.relative);
        if (path.empty() || path.front() == L'\\' || path.back() == L'\\' ||
            path.find_first_of(L"/:*?<>|\"") != path.npos || !paths.insert(path).second)
            throw Error(L"安装文件相对路径无效或重复：" + f.relative);
        size_t start = 0;
        do {
            auto end = path.find(L'\\', start);
            auto name = path.substr(start, end == path.npos ? path.npos : end - start);
            if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
                std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }))
                throw Error(L"安装文件路径包含不安全的目录或文件名：" + f.relative);
            if (end == path.npos)
                break;
            start = end + 1;
        } while (true);
        if (UINT64_MAX - total < f.size)
            throw Error(L"安装文件容量溢出。");
        total += f.size;
    }
    for (const auto &path : paths)
        for (size_t slash = path.find(L'\\'); slash != path.npos; slash = path.find(L'\\', slash + 1))
            if (paths.count(path.substr(0, slash)))
                throw Error(L"安装文件与目录路径冲突：" + path);
    return total;
}
uint64_t RequiredCopyBytes(const std::vector<File> &files, DWORD cluster) {
    ManifestBytes(files);
    if (cluster < 512 || cluster > 2 * MiB || (cluster & (cluster - 1)))
        throw Error(L"目标文件系统的分配单元无效。");
    uint64_t total = 64 * MiB; // Directory and filesystem growth reserve; not only file payload bytes.
    for (const auto &f : files) {
        uint64_t extra = f.size % cluster ? cluster - f.size % cluster : 0;
        if (f.size > UINT64_MAX - extra || total > UINT64_MAX - f.size - extra)
            throw Error(L"目标文件系统分配容量计算溢出。");
        total += f.size + extra;
    }
    return total;
}
std::vector<File> InstallationFiles(const Prepared &p, const std::vector<File> &swm) {
    if (p.plan.split) {
        CheckSwm(swm);
        if (p.staging.empty())
            throw Error(L"本次 SWM 目录未确认。");
        for (const auto &f : swm)
            if (Lower(FullPath(Parent(f.full))) != Lower(FullPath(p.staging)) ||
                Lower(f.relative) != Lower(BaseName(f.full)))
                throw Error(L"SWM 文件不属于本次拆分目录：" + f.full);
    } else if (!swm.empty())
        throw Error(L"未拆分的任务出现额外 SWM 文件。");
    std::vector<File> files;
    for (const auto &file : p.plan.files)
        if (!p.plan.split || Lower(file.relative) != L"sources\\install.wim")
            files.push_back(file);
    for (auto file : swm) {
        file.relative = L"sources\\" + BaseName(file.full);
        files.push_back(std::move(file));
    }
    ManifestBytes(files);
    return files;
}
void MakeDirectory(const std::wstring &p) {
    if (p.empty())
        throw Error(L"临时路径为空。");
    // Win32 volume-GUID and drive roots require the trailing separator.
    if ((p.size() == 2 && p[1] == L':') || (p.rfind(L"\\\\?\\Volume{", 0) == 0 && p.back() == L'}')) {
        DWORD root = GetFileAttributesW((p + L"\\").c_str());
        if (root == INVALID_FILE_ATTRIBUTES || !(root & FILE_ATTRIBUTE_DIRECTORY))
            throw Error(L"目标卷根目录不可读取。" + p);
        return;
    }
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) {
        if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw Error(L"目录路径不安全：" + p);
        return;
    }
    auto parent = Parent(p);
    if (parent != p && !parent.empty())
        MakeDirectory(parent);
    if (!CreateDirectoryW(p.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        throw Error(L"不能创建目录：" + p + L"\r\n" + WinError());
    a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(L"新目录校验失败：" + p);
}
std::vector<File> ListFiles(const std::wstring &root, std::atomic<bool> &stop) {
    std::vector<File> out;
    std::function<void(std::wstring)> walk = [&](std::wstring relative) {
        CheckCancel(stop);
        WIN32_FIND_DATAW data{};
        HANDLE h = FindFirstFileW((root + relative + L"*").c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return;
            throw Error(L"源目录无法枚举：" + root + relative + L"\r\n" + WinError());
        }
        try {
            do {
                CheckCancel(stop);
                std::wstring name = data.cFileName;
                if (name == L"." || name == L"..")
                    continue;
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    throw Error(L"文件目录包含重解析点，停止当前操作：" + relative + name);
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    walk(relative + name + L"\\");
                else
                    out.push_back({root + relative + name, relative + name,
                                   ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow});
            } while (FindNextFileW(h, &data));
            if (GetLastError() != ERROR_NO_MORE_FILES)
                throw Error(L"文件枚举未完成：" + WinError());
        } catch (...) {
            FindClose(h);
            throw;
        }
        FindClose(h);
    };
    walk(L"");
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return Lower(a.relative) < Lower(b.relative); });
    return out;
}
namespace {
Handle ReadPinned(const std::wstring &path, uint64_t expected) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!file.valid() || !GetFileInformationByHandle(file, &info))
        throw Error(L"不能读取或固定安装文件：" + path + L"\n" + WinError());
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        (((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow) != expected)
        throw Error(L"安装文件类型或大小已变化：" + path);
    return file;
}
struct CopyProgress {
    const Hooks *hooks;
    std::atomic<bool> *stop;
    uint64_t done, total, start, last = 0;
    std::wstring name;
    std::exception_ptr failure;
};
DWORD CALLBACK CopyCallback(LARGE_INTEGER size, LARGE_INTEGER copied, LARGE_INTEGER, LARGE_INTEGER, DWORD,
                            DWORD, HANDLE, HANDLE, LPVOID value) {
    auto p = (CopyProgress *)value;
    try {
        if (*p->stop)
            return PROGRESS_CANCEL;
        auto now = GetTickCount64();
        if (now - p->last < 100 && copied.QuadPart != size.QuadPart)
            return PROGRESS_CONTINUE;
        p->last = now;
        if (p->hooks->progress)
            (*p->hooks).progress({4, 100.0 * (p->done + copied.QuadPart) / std::max<uint64_t>(1, p->total),
                                  100.0 * copied.QuadPart / std::max<LONGLONG>(1, size.QuadPart),
                                  L"正在复制安装文件", p->name, (now - p->start) / 1000});
        return PROGRESS_CONTINUE;
    } catch (...) {
        // Never unwind C++ exceptions through CopyFileEx's system frames. Let it
        // cancel and close its source/destination handles before reporting failure.
        p->failure = std::current_exception();
        return PROGRESS_CANCEL;
    }
}
} // namespace
std::vector<Handle> PinFilesReadOnly(const std::vector<File> &files) {
    ManifestBytes(files);
    std::vector<Handle> pins;
    for (const auto &file : files)
        pins.push_back(ReadPinned(file.full, file.size));
    return pins;
}
void CopyFilesNative(const std::vector<File> &files, const std::wstring &target, const Hooks &h,
                     std::atomic<bool> &stop) {
    if (target.empty() || target.back() != L'\\')
        throw Error(L"复制目标根路径无效。");
    CopyProgress p{&h, &stop, 0, 0, GetTickCount64(), 0, L"", {}};
    p.total = ManifestBytes(files);
    for (auto &f : files) {
        CheckCancel(stop);
        auto sourcePin = ReadPinned(f.full, f.size);
        std::wstring dest = target + f.relative;
        MakeDirectory(Parent(dest));
        if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw Error(L"复制目标出现同名文件，停止覆盖：" + f.relative);
        p.name = f.relative;
        BOOL cancelled = FALSE;
        if (h.log)
            h.log(L"复制：" + f.relative + L" · " + FormatSize(f.size));
        BOOL copied =
            CopyFileExW(f.full.c_str(), dest.c_str(), CopyCallback, &p, &cancelled, COPY_FILE_FAIL_IF_EXISTS);
        if (p.failure)
            std::rethrow_exception(p.failure);
        if (!copied) {
            if (stop)
                throw Cancelled();
            throw Error(L"复制失败：" + f.relative + L"\r\n" + WinError());
        }
        p.done += f.size;
    }
    if (h.progress)
        h.progress({4, 100, 100, L"安装文件复制完成", L"等待校验", (GetTickCount64() - p.start) / 1000});
}
void VerifyFilesNative(const std::vector<File> &files, const std::wstring &target, std::atomic<bool> &stop,
                       const Hooks &h) {
    if (target.empty() || target.back() != L'\\')
        throw Error(L"校验目标根路径无效。");
    uint64_t total = ManifestBytes(files), done = 0, started = GetTickCount64(), last = 0;
    std::vector<BYTE> sourceBuffer(MiB), targetBuffer(MiB);
    for (auto &f : files) {
        CheckCancel(stop);
        auto source = ReadPinned(f.full, f.size);
        auto dest = ReadPinned(target + f.relative, f.size);
        uint64_t offset = 0;
        if (h.log)
            h.log(L"校验内容：" + f.relative);
        while (offset < f.size) {
            CheckCancel(stop);
            DWORD count = (DWORD)std::min<uint64_t>(MiB, f.size - offset), readSource = 0, readTarget = 0;
            if (!ReadFile(source, sourceBuffer.data(), count, &readSource, nullptr) ||
                !ReadFile(dest, targetBuffer.data(), count, &readTarget, nullptr))
                throw Error(L"文件内容读取失败：" + f.relative + L"\n" + WinError());
            if (readSource != count || readTarget != count)
                throw Error(L"文件内容未完整读取：" + f.relative);
            if (std::memcmp(sourceBuffer.data(), targetBuffer.data(), count))
                throw Error(L"文件内容不一致：" + f.relative + L"；偏移=" + std::to_wstring(offset));
            offset += count;
            auto now = GetTickCount64();
            if (h.progress && (now - last >= 100 || offset == f.size)) {
                h.progress({5, 100.0 * (done + offset) / std::max<uint64_t>(1, total),
                            100.0 * offset / std::max<uint64_t>(1, f.size), L"正在校验安装文件内容",
                            f.relative, (now - started) / 1000});
                last = now;
            }
        }
        done += f.size;
    }
    CheckCancel(stop);
    if (h.progress)
        h.progress(
            {5, 100, 100, L"文件大小和内容校验完成", L"校验完成", (GetTickCount64() - started) / 1000});
}
} // namespace wu
