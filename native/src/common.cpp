#include "win11usb.hpp"
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <shlobj.h>
#include <sstream>

namespace wu {
Error::Error(const std::wstring &s) : std::runtime_error(Utf8(s)), text(s) {}
std::wstring WinError(DWORD code) {
    wchar_t *msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring s = msg ? msg : L"未知 Windows 错误";
    if (msg)
        LocalFree(msg);
    return Trim(s) + L"（" + std::to_wstring(code) + L"）";
}
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}
std::wstring Trim(std::wstring s) {
    auto a = s.find_first_not_of(L" \r\n\t\0");
    if (a == s.npos)
        return L"";
    return s.substr(a, s.find_last_not_of(L" \r\n\t\0") - a + 1);
}
std::string Utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring Wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw Error(L"文本不是有效的 UTF-8。");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}
std::wstring Json(const std::wstring &s) {
    std::wstring r = L"\"";
    for (auto c : s) {
        switch (c) {
        case L'"':
            r += L"\\\"";
            break;
        case L'\\':
            r += L"\\\\";
            break;
        case L'\n':
            r += L"\\n";
            break;
        case L'\r':
            r += L"\\r";
            break;
        case L'\t':
            r += L"\\t";
            break;
        default:
            if (c < 32) {
                wchar_t b[8];
                swprintf(b, 8, L"\\u%04x", (unsigned)c);
                r += b;
            } else
                r += c;
        }
    }
    return r + L"\"";
}
std::wstring FormatSize(uint64_t n) {
    std::wostringstream s;
    s << std::fixed << std::setprecision(1) << (double)n / GiB << L" GiB";
    return s.str();
}
std::wstring ExePath() {
    std::vector<wchar_t> b(32768);
    DWORD n = GetModuleFileNameW(nullptr, b.data(), (DWORD)b.size());
    if (!n || n >= b.size())
        throw Error(L"无法获取程序路径。");
    return {b.data(), n};
}
std::wstring Parent(const std::wstring &p) {
    auto n = p.find_last_of(L"\\/");
    return n == p.npos ? L"" : p.substr(0, n);
}
std::wstring BaseName(const std::wstring &p) {
    auto n = p.find_last_of(L"\\/");
    return n == p.npos ? p : p.substr(n + 1);
}
std::wstring FullPath(const std::wstring &p) {
    std::vector<wchar_t> b(32768);
    DWORD n = GetFullPathNameW(p.c_str(), (DWORD)b.size(), b.data(), nullptr);
    if (!n || n >= b.size())
        throw Error(L"文件路径过长或无效。");
    return {b.data(), n};
}
std::wstring FinalPath(const std::wstring &p) {
    Handle h(CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!h.valid())
        throw Error(L"无法确认实际路径：" + p + L"\n" + WinError());
    std::vector<wchar_t> b(32768);
    DWORD n = GetFinalPathNameByHandleW(h, b.data(), (DWORD)b.size(), FILE_NAME_NORMALIZED);
    if (!n || n >= b.size())
        throw Error(L"无法解析实际路径：" + WinError());
    return {b.data(), n};
}
std::wstring SystemFile(const wchar_t *name) {
    wchar_t b[MAX_PATH];
    if (!GetSystemDirectoryW(b, MAX_PATH))
        throw Error(L"无法读取系统目录。");
    return std::wstring(b) + L"\\" + name;
}
bool FileExists(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
uint64_t FileSize(const std::wstring &p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d))
        throw Error(L"无法读取文件：" + p + L"\n" + WinError());
    if (d.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(L"文件类型无效：" + p);
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
void WriteUtf8(const std::wstring &p, const std::wstring &text) {
    Handle h(CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid())
        throw Error(L"无法写入日志：" + p + L"\n" + WinError());
    std::string b = Utf8(text);
    DWORD n = 0;
    if (b.size() > MAXDWORD || !WriteFile(h, b.data(), (DWORD)b.size(), &n, nullptr) || n != b.size())
        throw Error(L"日志写入失败。");
}
bool IsAdmin() {
    SID_IDENTIFIER_AUTHORITY a = SECURITY_NT_AUTHORITY;
    PSID s = nullptr;
    BOOL yes = FALSE;
    if (AllocateAndInitializeSid(&a, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &s)) {
        CheckTokenMembership(nullptr, s, &yes);
        FreeSid(s);
    }
    return yes != FALSE;
}
void CheckCancel(const std::atomic<bool> &stop) {
    if (stop.load())
        throw Cancelled();
}
std::vector<BYTE> ResourceBytes(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r)
        throw Error(L"程序资源缺失：" + std::to_wstring(id));
    HGLOBAL g = LoadResource(nullptr, r);
    auto p = (const BYTE *)LockResource(g);
    DWORD n = SizeofResource(nullptr, r);
    if (!p || !n)
        throw Error(L"程序资源无法读取。");
    return {p, p + n};
}
void WriteDependencies(const std::wstring &path) {
    std::wstring s =
        L"{\"native\":true,\"dotnet_required\":false,\"powershell_required\":false,\"system_components\":[";
    bool first = true;
    for (auto name : {L"virtdisk.dll", L"wimgapi.dll", L"dism.exe", L"format.com"}) {
        if (!first)
            s += L",";
        first = false;
        s += L"{\"name\":" + Json(name) + L",\"present\":" +
             (FileExists(SystemFile(name)) ? L"true" : L"false");
        if (std::wstring(name).find(L".dll") != std::wstring::npos) {
            HMODULE library = LoadLibraryExW(SystemFile(name).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            s += L",\"loadable\":" + std::wstring(library ? L"true" : L"false");
            if (!library)
                s += L",\"error\":" + Json(WinError());
            if (library)
                FreeLibrary(library);
        }
        s += L"}";
    }
    WriteUtf8(path, s + L"]}");
}
} // namespace wu
