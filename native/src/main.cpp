#include "win11usb.hpp"
#include "eject.hpp"
#include <algorithm>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace wu;
    try {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; i++)
            args.push_back(argv[i]);
        LocalFree(argv);
        for (size_t i = 0; i < args.size(); i++)
            if (args[i] == L"--eject-report") {
                if (i + 1 == args.size())
                    throw Error(L"--eject-report 需要指定只读诊断报告路径。");
                WriteUtf8(FullPath(args[i + 1]), UsbEjectReport(ReadDisks()));
                return 0;
            }
        for (size_t i = 0; i < args.size(); i++)
            if (args[i] == L"--dependency-report") {
                if (i + 1 == args.size())
                    throw Error(L"请指定报告路径。");
                WriteDependencies(FullPath(args[i + 1]));
                return 0;
            }
        for (size_t i = 0; i < args.size(); i++)
            if (args[i] == L"--list-disks") {
                if (i + 1 == args.size())
                    throw Error(L"--list-disks 需要指定 JSON 报告路径。");
                WriteUtf8(FullPath(args[i + 1]), DiskJson(ReadDisks()));
                return 0;
            }
        if (std::find(args.begin(), args.end(), L"--self-check") != args.end()) {
            HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(1));
            return icon ? 0 : 1;
        }
        auto has = [&](const wchar_t *s) { return std::find(args.begin(), args.end(), s) != args.end(); };
        bool running = has(L"--preview-running"), empty = has(L"--preview-empty"),
             blocked = has(L"--preview-blocked");
        return RunUi(instance, has(L"--preview") || running || empty || blocked || has(L"--preview-dialog"),
                     running, empty, has(L"--preview-dialog"), blocked);
    } catch (const Error &e) {
        MessageBoxW(nullptr, e.text.c_str(), L"Win11Usb · 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    } catch (const std::exception &e) {
        MessageBoxA(nullptr, e.what(), "Win11Usb", MB_OK | MB_ICONERROR);
        return 1;
    }
}
