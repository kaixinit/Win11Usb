#include "platform.hpp"
#include <algorithm>

namespace wu {
std::vector<File> InstallationFiles(const Prepared &p, const std::vector<File> &swm) {
    std::vector<File> files;
    for (const auto &file : p.plan.files)
        if (!p.plan.split || Lower(file.relative) != L"sources\\install.wim")
            files.push_back(file);
    for (auto file : swm) {
        file.relative = L"sources\\" + BaseName(file.full);
        files.push_back(std::move(file));
    }
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
                    throw Error(L"源目录包含重解析点，尚未清盘。" + relative + name);
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
struct CopyProgress {
    const Hooks *hooks;
    std::atomic<bool> *stop;
    uint64_t done, total, start, last = 0;
    std::wstring name;
};
DWORD CALLBACK CopyCallback(LARGE_INTEGER size, LARGE_INTEGER copied, LARGE_INTEGER, LARGE_INTEGER, DWORD,
                            DWORD, HANDLE, HANDLE, LPVOID value) {
    auto p = (CopyProgress *)value;
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
}
} // namespace
void CopyFilesNative(const std::vector<File> &files, const std::wstring &target, const Hooks &h,
                     std::atomic<bool> &stop) {
    CopyProgress p{&h, &stop, 0, 0, GetTickCount64(), 0, L""};
    for (auto &f : files) {
        if (UINT64_MAX - p.total < f.size)
            throw Error(L"复制容量溢出。");
        p.total += f.size;
    }
    for (auto &f : files) {
        CheckCancel(stop);
        if (FileSize(f.full) != f.size)
            throw Error(L"源文件大小已变化：" + f.relative);
        std::wstring dest = target + f.relative;
        MakeDirectory(Parent(dest));
        if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw Error(L"复制目标出现同名文件，停止覆盖：" + f.relative);
        p.name = f.relative;
        BOOL cancelled = FALSE;
        if (h.log)
            h.log(L"复制：" + f.relative + L" · " + FormatSize(f.size));
        if (!CopyFileExW(f.full.c_str(), dest.c_str(), CopyCallback, &p, &cancelled,
                         COPY_FILE_FAIL_IF_EXISTS)) {
            if (stop)
                throw Cancelled();
            throw Error(L"复制失败：" + f.relative + L"\r\n" + WinError());
        }
        p.done += f.size;
    }
    if (h.progress)
        h.progress({4, 100, 100, L"安装文件复制完成", L"等待校验", (GetTickCount64() - p.start) / 1000});
}
void VerifyFilesNative(const std::vector<File> &files, const std::wstring &target, std::atomic<bool> &stop) {
    for (auto &f : files) {
        CheckCancel(stop);
        if (FileSize(target + f.relative) != f.size)
            throw Error(L"目标文件大小不匹配：" + f.relative);
    }
    CheckCancel(stop);
}
} // namespace wu
