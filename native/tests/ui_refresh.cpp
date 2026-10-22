// Uses the production UI directly; no ISO mount or disk-writing backend is linked.
#include "../src/ui.cpp"
#include <iostream>

using namespace wu;
namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void Pump(DWORD duration) {
    auto until = GetTickCount64() + duration;
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < until);
}
struct Paints {
    unsigned parent = 0, log = 0, iso = 0, disk = 0, outside = 0;
    Ui *ui = nullptr;
    bool warningSeen = false;
    void Clear() { parent = log = iso = disk = outside = 0; }
};
LRESULT CALLBACK CountPaint(HWND window, UINT message, WPARAM a, LPARAM b, UINT_PTR id, DWORD_PTR data) {
    auto &p = *(Paints *)data;
    if (id == 1 && message == WM_TIMER && a == 93) {
        HWND popup = GetLastActivePopup(window);
        DWORD process = 0;
        GetWindowThreadProcessId(popup, &process);
        if (popup != window && process == GetCurrentProcessId() && GetWindow(popup, GW_OWNER) == window) {
            wchar_t title[128]{}, body[1024]{};
            GetWindowTextW(popup, title, 128);
            GetWindowTextW(GetDlgItem(popup, 3), body, 1024);
            p.warningSeen = std::wstring(title) == L"制作完成，U 盘尚未弹出" &&
                            std::wstring(body).find(L"不支持") != std::wstring::npos;
            KillTimer(window, 93);
            SendMessageW(popup, WM_COMMAND, IDCANCEL, 0); // This test process's informational sample only.
        }
        return 0;
    }
    if (message == WM_PAINT) {
        if (id == 1) {
            p.parent++;
            RECT dirty{};
            GetUpdateRect(window, &dirty, FALSE);
            auto allowed = p.ui->R(22, 326, p.ui->width - 22, 422);
            if (dirty.left < allowed.left || dirty.right > allowed.right || dirty.top < allowed.top ||
                dirty.bottom > allowed.bottom)
                p.outside++;
        } else if (id == 2)
            p.log++;
        else if (id == 3)
            p.iso++;
        else
            p.disk++;
    }
    return DefSubclassProc(window, message, a, b);
}
std::wstring Read(const std::wstring &path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    Require(file.valid(), "read persisted simulated log");
    std::string bytes((size_t)FileSize(path), 0);
    DWORD count = 0;
    Require(ReadFile(file, bytes.data(), (DWORD)bytes.size(), &count, nullptr) && count == bytes.size(),
            "read complete persisted simulated log");
    return Wide(bytes);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        Require(argc == 2 || argc == 3, "usage: ui-tests new-output-directory [--visual]");
        auto output = FullPath(argv[1]);
        Require(GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES,
                "test output directory must be new");
        Require(CreateDirectoryW(output.c_str(), nullptr), "create own ordinary-file test directory");
        INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        Ui ui(GetModuleHandleW(nullptr), true, true, false);
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = ui.instance;
        wc.lpszClassName = L"Win11Usb.Native.UiRefreshTest";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Require(RegisterClassW(&wc), "register test fixture window");
        RECT rect{0, 0, 826, 760};
        AdjustWindowRectEx(&rect, MainWindowStyle, FALSE, 0);
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"Win11Usb · 只读刷新测试（模拟）",
                                      MainWindowStyle, 100, 50, rect.right - rect.left,
                                      rect.bottom - rect.top, nullptr, nullptr, ui.instance, &ui);
        Require(window != nullptr, "create production UI in sample/read-only mode");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        SendMessageW(window, WM_WORK_PATH, 0, (LPARAM) new std::wstring(output));
        Pump(250);
        Require((GetWindowLongPtrW(window, GWL_STYLE) & WS_CLIPCHILDREN) != 0,
                "production main window excludes child controls when painting");
        Paints paints;
        paints.ui = &ui;
        for (const auto &entry :
             {std::pair<HWND, UINT_PTR>{window, 1}, {ui.log, 2}, {ui.isoButton, 3}, {ui.combo, 4}})
            Require(SetWindowSubclass(entry.first, CountPaint, entry.second, (DWORD_PTR)&paints),
                    "instrument this test process's own UI");
        for (int i = 0; i < 5000; ++i) {
            ui.QueueLog(L"[模拟复制] sources\\file-" + std::to_wstring(i) + L".dll");
            ui.QueueProgress({4, i / 50.0, i % 100 * 1.0, L"正在复制安装文件（模拟）",
                              L"sources\\file-" + std::to_wstring(i) + L".dll", 42});
        }
        Pump(450);
        Require(ui.progress.file == L"sources\\file-4999.dll", "latest copy progress reaches display");
        Require(SendMessageW(ui.log, EM_GETFIRSTVISIBLELINE, 0, 0) > 4900,
                "batched log follows newest lines after EDIT resumes redraw");
        Require(ui.sessionLog.find(L"file-0.dll") != ui.sessionLog.npos &&
                    ui.sessionLog.find(L"file-4999.dll") != ui.sessionLog.npos,
                "dense copy logs retain first and last entries");
        unsigned copyParent = paints.parent, copyLog = paints.log;
        Require(paints.parent > 0 && paints.parent <= 4 && paints.log > 0 && paints.log <= 4,
                "5000 copy/log updates are painted in a bounded number of batches");
        Require(paints.outside == 0 && paints.iso == 0 && paints.disk == 0,
                "copy updates do not repaint settings or cover child controls");
        paints.Clear();
        auto length = GetWindowTextLengthW(ui.log);
        for (int i = 0; i < 5000; ++i)
            ui.QueueProgress({5, i / 50.0, 25, L"正在校验（模拟）", L"sources\\install.swm", 90});
        Pump(450);
        Require(paints.parent > 0 && paints.parent <= 4 && paints.log == 0 && paints.iso == 0 &&
                    paints.disk == 0 && paints.outside == 0 && GetWindowTextLengthW(ui.log) == length,
                "progress-only updates leave log and settings entirely unpainted");
        unsigned verifyParent = paints.parent, verifyLog = paints.log;
        ui.QueueLog(std::wstring(2100000, L'X') + L"\r\n[模拟大批次末行] 保留最新内容");
        ui.FlushUpdates();
        int visibleLength = GetWindowTextLengthW(ui.log);
        std::wstring visible((size_t)visibleLength + 1, 0);
        GetWindowTextW(ui.log, visible.data(), (int)visible.size());
        Require(visibleLength < 1500000 && visible.find(L"[模拟大批次末行]") != visible.npos,
                "oversized pending batch preserves newest visible log within control limit");
        // Completion may arrive before the 100ms timer; its queued tail must be saved first.
        ui.QueueLog(L"[模拟末行] 确认最终日志不会丢失。");
        SendMessageW(window, WM_FINISHED, 0, (LPARAM) new Result{true, false, false, L"", output});
        Pump(100);
        Require(!ui.sessionFile.valid() && Read(output + L"\\session.log") == ui.sessionLog,
                "completion flushes all queued logs before closing the persisted log");
        Require(ui.finished && ui.progress.overall == 100,
                "completion cannot be overwritten by queued stale progress");
        SetTimer(window, 93, 100, nullptr);
        SendMessageW(window, WM_FINISHED, 0,
                     (LPARAM) new Result{true, false, false, L"", output,
                                         L"此设备节点不支持当前安全弹出请求。（CM 23，veto 8）"});
        Require(paints.warningSeen && ui.finished && ui.phase == L"制作完成 · U 盘未弹出" &&
                    ui.fileText.find(L"尚未安全弹出") != ui.fileText.npos,
                "eject failure uses accurate existing dialog and completed-but-not-ejected UI state");
        auto report = L"{\"simulated_log_events\":5000,\"simulated_progress_events\":10000,"
                      L"\"copy_parent_paints\":" +
                      std::to_wstring(copyParent) + L",\"copy_log_paints\":" + std::to_wstring(copyLog) +
                      L",\"verify_parent_paints\":" + std::to_wstring(verifyParent) +
                      L",\"verify_log_paints\":" + std::to_wstring(verifyLog) +
                      L",\"settings_repaints\":0,\"persisted_log_complete\":true,\"eject_warning_shown\":"
                      L"true,\"physical_disk_writes\":0}";
        WriteUtf8(output + L"\\ui-refresh.json", report);
        std::cout << "PASS actual production UI: " << Utf8(report) << '\n';
        if (argc == 3 && std::wstring(argv[2]) == L"--visual") {
            ui.Simulate();
            for (int frame = 0; frame < 600; ++frame) {
                for (int line = 0; line < 30; ++line)
                    ui.QueueLog(L"[只读模拟] 复制 sources\\example-" + std::to_wstring(frame * 30 + line) +
                                L".dll");
                ui.QueueProgress({4, frame / 6.0, (frame % 100) * 1.0, L"正在复制安装文件（只读模拟）",
                                  L"sources\\example-" + std::to_wstring(frame * 30) + L".dll", frame / 10u});
                Pump(100);
            }
            ui.busy = false;
        }
        DestroyWindow(window);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI refresh: " << e.what() << '\n';
        return 1;
    }
}
