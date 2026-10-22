#include "platform.hpp"

namespace wu {
namespace {
std::wstring XmlText(const std::wstring &s, const wchar_t *tag) {
    std::wstring open = L"<" + Lower(tag) + L">", close = L"</" + Lower(tag) + L">", lower = Lower(s);
    auto a = lower.find(open);
    if (a == lower.npos)
        return L"";
    a += open.size();
    auto b = lower.find(close, a);
    if (b == lower.npos)
        throw Error(L"映像元数据标签不完整。");
    std::wstring value = s.substr(a, b - a);
    for (auto pair : {std::pair<const wchar_t *, const wchar_t *>{L"&lt;", L"<"},
                      {L"&gt;", L">"},
                      {L"&quot;", L"\""},
                      {L"&apos;", L"'"},
                      {L"&amp;", L"&"}}) {
        size_t p = 0;
        while ((p = value.find(pair.first, p)) != value.npos) {
            value.replace(p, wcslen(pair.first), pair.second);
            p += wcslen(pair.second);
        }
    }
    return Trim(value);
}
int Number(const std::wstring &s, const wchar_t *tag) {
    auto v = XmlText(s, tag);
    if (v.empty())
        return 0;
    try {
        size_t n;
        int i = std::stoi(v, &n);
        if (n != v.size() || i < 0)
            throw Error(L"映像元数据数值无效。");
        return i;
    } catch (...) {
        throw Error(L"映像元数据数值无效。");
    }
}
using CloseWim = BOOL(WINAPI *)(HANDLE);
struct WimHandle {
    HANDLE value;
    CloseWim close;
    ~WimHandle() {
        if (value && value != INVALID_HANDLE_VALUE)
            close(value);
    }
};
std::wstring PrepareScratch(const std::wstring &path, const Hooks &hooks) {
    if (path.empty())
        throw Error(L"未指定 WIM 临时目录。尚未清盘。");
    auto scratch = FullPath(path);
    MakeDirectory(scratch);
    auto available = CheckWritableDirectory(scratch, 64 * MiB);
    if (hooks.log)
        hooks.log(L"WIM 临时目录：" + scratch + L"；可用空间：" + FormatSize(available));
    return scratch;
}
} // namespace
ImageIdentity ParseWimXml(const std::wstring &xml, const std::wstring &iso) {
    if (xml.size() > 2 * 1024 * 1024)
        throw Error(L"映像元数据异常过大。");
    ImageIdentity d;
    d.name = XmlText(xml, L"NAME");
    d.description = XmlText(xml, L"DESCRIPTION");
    if (d.name.size() > 1024 || d.description.size() > 2048)
        throw Error(L"映像名称或描述异常过长。");
    auto windows = XmlText(xml, L"WINDOWS"), version = XmlText(windows, L"VERSION");
    d.edition = XmlText(windows, L"EDITIONID");
    d.installation = XmlText(windows, L"INSTALLATIONTYPE");
    d.product = XmlText(windows, L"PRODUCTTYPE");
    d.arch = Number(windows, L"ARCH");
    d.major = Number(version, L"MAJOR");
    d.minor = Number(version, L"MINOR");
    d.build = Number(version, L"BUILD");
    d.revision = Number(version, L"SPBUILD");
    if (d.name.empty() || !d.major || !d.build)
        throw Error(L"安装映像缺少系统版本元数据，尚未清盘。");
    d.version = std::to_wstring(d.major) + L"." + std::to_wstring(d.minor) + L"." + std::to_wstring(d.build) +
                L"." + std::to_wstring(d.revision);
    return ResolveIdentity(d, iso);
}
ImageIdentity ReadWimIdentity(const std::wstring &image, const std::wstring &iso,
                              const std::wstring &scratchPath, const Hooks &hooks) {
    auto scratch = PrepareScratch(scratchPath, hooks);
    Module m(L"wimgapi.dll");
    auto create = m.Proc<HANDLE(WINAPI *)(PCWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD)>("WIMCreateFile");
    auto temporary = m.Proc<BOOL(WINAPI *)(HANDLE, PCWSTR)>("WIMSetTemporaryPath");
    auto load = m.Proc<HANDLE(WINAPI *)(HANDLE, DWORD)>("WIMLoadImage");
    auto info = m.Proc<BOOL(WINAPI *)(HANDLE, PVOID *, PDWORD)>("WIMGetImageInformation");
    auto close = m.Proc<BOOL(WINAPI *)(HANDLE)>("WIMCloseHandle");
    DWORD disposition = 0;
    WimHandle file{create(image.c_str(), GENERIC_READ, OPEN_EXISTING, 0, 0, &disposition), close};
    if (!file.value || file.value == INVALID_HANDLE_VALUE)
        throw Error(L"无法读取安装映像：" + WinError());
    // WIMLoadImage requires WIMSetTemporaryPath even for read-only metadata access.
    // The task scratch is on the local drive, outside the ISO and target USB.
    if (!temporary(file.value, scratch.c_str()))
        throw Error(L"无法设置 WIM 临时目录：" + scratch + L"\r\n" + WinError() + L"\r\n尚未清盘。");
    if (hooks.log)
        hooks.log(L"正在读取安装映像索引 1…");
    WimHandle index{load(file.value, 1), close};
    if (!index.value || index.value == INVALID_HANDLE_VALUE)
        throw Error(L"无法读取安装映像索引 1：" + WinError() + L"\r\nWIM 临时目录：" + scratch +
                    L"\r\n请检查该目录的可用空间和写入权限。尚未清盘。");
    PVOID buffer = nullptr;
    DWORD length = 0;
    BOOL ok = info(index.value, &buffer, &length);
    DWORD e = GetLastError();
    if (!ok || !buffer || length < 4 || length % sizeof(wchar_t)) {
        if (buffer)
            LocalFree(buffer);
        throw Error(L"无法读取映像元数据：" + WinError(e));
    }
    if (length > 16 * 1024 * 1024) {
        LocalFree(buffer);
        throw Error(L"映像元数据异常过大。");
    }
    std::wstring xml((const wchar_t *)buffer, length / sizeof(wchar_t));
    LocalFree(buffer);
    return ParseWimXml(xml, iso);
}
ImageIdentity ReadTaskWimIdentity(const std::wstring &image, const std::wstring &iso,
                                  const std::wstring &work, const Disk &target, const Hooks &hooks) {
    if (work.empty())
        throw Error(L"未指定本次任务目录。尚未清盘。");
    // FinalPath/AssertOutside requires an existing path. Validate the existing task
    // directory first, create its fixed child, then check that child's actual volume.
    TaskWorkspace workspace(work, target);
    auto scratch = workspace.Directory(L"wim-temp");
    return ReadWimIdentity(image, iso, scratch, hooks);
}
} // namespace wu
