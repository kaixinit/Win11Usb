#pragma once
#include "win11usb.hpp"
#include <mutex>

namespace wu {
struct Module {
    HMODULE h = nullptr;
    explicit Module(const wchar_t *file)
        : h(LoadLibraryExW(SystemFile(file).c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!h)
            throw Error(std::wstring(L"缺少系统组件：") + file + L"。尚未清盘。" + WinError());
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
std::vector<File> ListFiles(const std::wstring &root, std::atomic<bool> &stop);
ImageIdentity ReadWimIdentity(const std::wstring &image, const std::wstring &iso, const std::wstring &scratch,
                              const Hooks &hooks = {});
ImageIdentity ReadTaskWimIdentity(const std::wstring &image, const std::wstring &iso,
                                  const std::wstring &work, const Disk &target, const Hooks &hooks = {});
ImageIdentity ParseWimXml(const std::wstring &xml, const std::wstring &iso);
void CopyFilesNative(const std::vector<File> &files, const std::wstring &target, const Hooks &hooks,
                     std::atomic<bool> &stop);
void VerifyFilesNative(const std::vector<File> &files, const std::wstring &target, std::atomic<bool> &stop);
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
