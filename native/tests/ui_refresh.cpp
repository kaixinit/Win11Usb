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
    enum class ModalAction { Close, Enter, Escape, ResizeEscape };
    unsigned parent = 0, log = 0, iso = 0, disk = 0, outside = 0;
    Ui *ui = nullptr;
    bool warningSeen = false, completionSeen = false, modalOwnerDisabled = false;
    bool modalFocusPrimary = false, modalFocusSecondary = false;
    bool modalFits = false;
    unsigned modalCount = 0;
    ModalAction modalAction = ModalAction::Close;
    std::wstring modalTitle, modalDetails;
    void Clear() { parent = log = iso = disk = outside = 0; }
};
LRESULT CALLBACK CountPaint(HWND window, UINT message, WPARAM a, LPARAM b, UINT_PTR id, DWORD_PTR data) {
    auto &p = *(Paints *)data;
    if (id == 1 && message == WM_TIMER && a == 93) {
        HWND popup = GetLastActivePopup(window);
        DWORD process = 0;
        GetWindowThreadProcessId(popup, &process);
        if (popup != window && process == GetCurrentProcessId() && GetWindow(popup, GW_OWNER) == window) {
            if (p.modalAction == Paints::ModalAction::ResizeEscape)
                SetWindowPos(popup, nullptr, 0, 0, 500, 400,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            wchar_t title[128]{}, body[8192]{};
            GetWindowTextW(popup, title, 128);
            GetWindowTextW(GetDlgItem(popup, 3), body, 8192);
            p.modalTitle = title;
            p.modalDetails = body;
            p.modalOwnerDisabled = !IsWindowEnabled(window);
            p.modalFocusPrimary = GetFocus() == GetDlgItem(popup, IDOK);
            p.modalFocusSecondary = GetFocus() == GetDlgItem(popup, IDCANCEL);
            RECT client{}, bodyRect{}, yesRect{};
            GetClientRect(popup, &client);
            auto inside = [&](HWND control, RECT *bounds = nullptr) {
                if (!control || !IsWindowVisible(control))
                    return true;
                RECT r{};
                GetWindowRect(control, &r);
                MapWindowPoints(nullptr, popup, reinterpret_cast<POINT *>(&r), 2);
                if (bounds)
                    *bounds = r;
                return r.left >= 0 && r.top >= 0 && r.right <= client.right &&
                       r.bottom <= client.bottom && r.right > r.left && r.bottom > r.top;
            };
            p.modalFits = inside(GetDlgItem(popup, 3), &bodyRect) &&
                          inside(GetDlgItem(popup, IDOK), &yesRect) &&
                          inside(GetDlgItem(popup, IDCANCEL)) &&
                          (bodyRect.bottom == 0 || bodyRect.bottom <= yesRect.top);
            p.modalCount++;
            p.completionSeen = p.modalTitle == L"制作结果";
            p.warningSeen = std::wstring(title) == L"制作完成，U 盘尚未弹出" &&
                            std::wstring(body).find(L"不支持") != std::wstring::npos;
            KillTimer(window, 93);
            // Close only this process's owned informational sample. Never locate,
            // confirm or send input to another program or a physical-disk prompt.
            if (p.modalAction == Paints::ModalAction::Close)
                SendMessageW(popup, WM_CLOSE, 0, 0);
            else
                PostMessageW(popup, WM_KEYDOWN,
                             p.modalAction == Paints::ModalAction::Enter ? VK_RETURN : VK_ESCAPE, 0);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        if (id == 1) {
            p.parent++;
            RECT dirty{};
            GetUpdateRect(window, &dirty, FALSE);
            auto allowed = p.ui->ProgressRect();
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
RECT ChildBounds(HWND owner, HWND child) {
    RECT bounds{};
    GetWindowRect(child, &bounds);
    MapWindowPoints(nullptr, owner, reinterpret_cast<POINT *>(&bounds), 2);
    return bounds;
}
void CheckMainLayout(Ui &ui, const char *scenario) {
    RECT client{};
    GetClientRect(ui.window, &client);
    for (HWND control : {ui.isoButton, ui.refresh, ui.combo, ui.fat, ui.ntfs, ui.log,
                         ui.start, ui.stopButton, ui.exportButton}) {
        if (!IsWindowVisible(control))
            continue;
        auto bounds = ChildBounds(ui.window, control);
        bool fits = bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right &&
                    bounds.bottom <= client.bottom && bounds.right > bounds.left &&
                    bounds.bottom > bounds.top;
        if (!fits) {
            RECT outer{};
            GetWindowRect(ui.window, &outer);
            MONITORINFO monitor{sizeof monitor};
            GetMonitorInfoW(MonitorFromWindow(ui.window, MONITOR_DEFAULTTONEAREST), &monitor);
            std::cerr << "layout diagnostic: scenario=" << scenario << " layout_dpi=" << ui.dpi
                      << " window_dpi=" << GetDpiForWindow(ui.window)
                      << " client=[" << client.left << ',' << client.top << ',' << client.right
                      << ',' << client.bottom << "] window=[" << outer.left << ',' << outer.top
                      << ',' << outer.right << ',' << outer.bottom << "] work_area=["
                      << monitor.rcWork.left << ',' << monitor.rcWork.top << ','
                      << monitor.rcWork.right << ',' << monitor.rcWork.bottom << "] control_id="
                      << GetDlgCtrlID(control) << " bounds=[" << bounds.left << ',' << bounds.top
                      << ',' << bounds.right << ',' << bounds.bottom << "]\n";
        }
        Require(fits,
                "long-path layout keeps every visible product control inside its client area");
    }
    auto log = ChildBounds(ui.window, ui.log);
    auto fat = ChildBounds(ui.window, ui.fat), ntfs = ChildBounds(ui.window, ui.ntfs);
    Require(fat.bottom <= ui.ProgressRect().top && ntfs.bottom <= ui.ProgressRect().top,
            "wrapped file-system choices stay above progress");
    for (HWND button : {ui.start, ui.stopButton, ui.exportButton})
        if (IsWindowVisible(button))
            Require(log.bottom <= ChildBounds(ui.window, button).top,
                    "long-path log body never overlaps footer actions");
    std::vector<wchar_t> text(32768);
    TOOLINFOW tip{sizeof tip};
    tip.hwnd = ui.window;
    tip.uId = 1;
    tip.lpszText = text.data();
    SendMessageW(ui.tooltip, TTM_GETTEXTW, text.size(), (LPARAM)&tip);
    if (std::wstring(text.data()) != ui.iso)
        std::cerr << "tooltip ISO diagnostic: expected=" << Utf8(ui.iso)
                  << " actual=" << Utf8(std::wstring(text.data())) << '\n';
    Require(std::wstring(text.data()) == ui.iso,
            "ISO tooltip preserves the full original path when layout has limited room");
    text.assign(text.size(), 0);
    tip.uId = (UINT_PTR)ui.combo;
    tip.lpszText = text.data();
    SendMessageW(ui.tooltip, TTM_GETTEXTW, text.size(), (LPARAM)&tip);
    Require(std::wstring(text.data()).find(ui.Selection()->Metadata()) != std::wstring::npos,
            "USB tooltip retains complete long metadata");
}
void CheckMainKeyboard(Ui &ui) {
    ui.busy = false;
    ui.finished = false;
    ui.promptPreview = false;
    ui.State();
    SetFocus(ui.start);
    MSG enter{};
    enter.hwnd = ui.start;
    enter.message = WM_KEYDOWN;
    enter.wParam = VK_RETURN;
    Require(GetFocus() == ui.start && DispatchMainKey(ui.window, enter) && ui.busy &&
                ui.progress.stage == 4,
            "Enter on the focused product Start button activates read-only sample creation");
    ui.busy = false;
    ui.State();
    SetFocus(ui.start);
    MSG space = enter;
    space.wParam = VK_SPACE;
    Require(!DispatchMainKey(ui.window, space), "custom Enter dispatch leaves Space native");
    DispatchMessageW(&space);
    space.message = WM_KEYUP;
    DispatchMessageW(&space);
    Require(ui.busy, "Space on Start still activates the native owner-drawn button");
    ui.busy = false;
    ui.State();
    SendMessageW(ui.ntfs, BM_CLICK, 0, 0);
    Require(ui.Fs() == L"NTFS", "native NTFS radio can be selected");
    SetFocus(ui.ntfs);
    MSG right{};
    right.hwnd = ui.ntfs;
    right.message = WM_KEYDOWN;
    right.wParam = VK_RIGHT;
    Require(IsDialogMessageW(ui.window, &right) && GetFocus() == ui.fat && ui.Fs() == L"FAT32",
            "Right from NTFS cycles within the two file-system radios and selects FAT32");
    SetFocus(ui.isoButton);
    MSG tab = right;
    tab.hwnd = ui.isoButton;
    tab.wParam = VK_TAB;
    Require(IsDialogMessageW(ui.window, &tab), "product main window supports native Tab navigation");
    for (HWND button : {ui.isoButton, ui.refresh, ui.start, ui.stopButton, ui.exportButton})
        Require((GetWindowLongPtrW(button, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW,
                "Tab navigation preserves the product button rendering style");
}
void CheckLongPathLayouts(Ui &ui) {
    auto originalDisk = *ui.Selection();
    auto originalIso = ui.iso, originalName = ui.isoName, originalDirectory = ui.isoDirectory;
    std::wstring longName = L"zh-cn_windows_11_business_editions_version_26h2_x64";
    for (int i = 0; i < 10; ++i)
        longName += L"_long_name_2026";
    longName += L".iso";
    Require(longName.size() >= 190 && longName.size() <= 255,
            "long ISO fixture remains a valid-sized Windows basename");
    ui.isoName = longName;
    ui.isoDirectory = L"D:\\Windows ISO\\系统安装介质\\长期备份\\教育版安装映像";
    ui.iso = ui.isoDirectory + L"\\" + longName;
    auto selected = ui.Selection();
    selected->friendly = L"Kingston DataTraveler 3.0 USB Storage Device with a deliberately long "
                         L"friendly name for the published wrapping layout";
    selected->letters = {L"F:\\", L"C:\\Mounts\\This is a long diagnostic mount path for the "
                                   L"read-only USB metadata fixture\\"};
    ui.State();
    SetWindowPos(ui.window, nullptr, 0, 0, 720, 700,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(100);
    CheckMainLayout(ui, "96dpi-720x700");
    MONITORINFO monitor{sizeof monitor};
    GetMonitorInfoW(MonitorFromWindow(ui.window, MONITOR_DEFAULTTONEAREST), &monitor);
    // This is a fixed-size geometry test at simulated 144 DPI. The monitor's
    // real DPI has not changed, so clipping the synthetic window to a small
    // service desktop mixes unrelated work-area constraints into this test.
    // A test window may extend off-screen; this does not claim small-screen fit.
    RECT suggested{monitor.rcWork.left, monitor.rcWork.top,
                   monitor.rcWork.left + 1080, monitor.rcWork.top + 1050};
    SendMessageW(ui.window, WM_DPICHANGED, MAKELONG(144, 144), (LPARAM)&suggested);
    Pump(100);
    Require(ui.dpi == 144, "exercise production layout and fonts at simulated 144 dpi");
    CheckMainLayout(ui, "simulated-144dpi-1080x1050");
    *ui.Selection() = std::move(originalDisk);
    ui.iso = std::move(originalIso);
    ui.isoName = std::move(originalName);
    ui.isoDirectory = std::move(originalDirectory);
    suggested = {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.left + 840,
                 monitor.rcWork.top + 800};
    SendMessageW(ui.window, WM_DPICHANGED, MAKELONG(96, 96), (LPARAM)&suggested);
    ui.State();
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        Require(argc == 2 || argc == 3, "usage: ui-tests new-output-directory [--visual]");
        auto output = FullPath(argv[1]);
        Require(GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES,
                "test output directory must be new");
        Require(CreateDirectoryW(output.c_str(), nullptr), "create own ordinary-file test directory");
        ProductGraphicsSession graphics;
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
        SetTimer(window, 93, 100, nullptr);
        SendMessageW(window, WM_FINISHED, 0, (LPARAM) new Result{true, false, false, L"", output});
        KillTimer(window, 93);
        Pump(100);
        Require(!ui.sessionFile.valid() && Read(output + L"\\session.log") == ui.sessionLog,
                "completion flushes all queued logs before closing the persisted log");
        Require(ui.finished && ui.progress.overall == 100,
                "completion cannot be overwritten by queued stale progress");
        Require(paints.completionSeen && paints.modalCount == 1 && paints.modalOwnerDisabled,
                "successful creation shows a distinct owned completion dialog");
        SetTimer(window, 93, 100, nullptr);
        SendMessageW(window, WM_FINISHED, 0,
                     (LPARAM) new Result{true, false, false, L"", output,
                                         L"此设备节点不支持当前安全弹出请求。（CM 23，veto 8）"});
        KillTimer(window, 93);
        Require(paints.warningSeen && ui.finished && ui.phase == L"制作完成 · U 盘未弹出" &&
                    ui.fileText.find(L"尚未安全弹出") != ui.fileText.npos,
                "eject failure uses accurate existing dialog and completed-but-not-ejected UI state");
        Require(paints.modalCount == 2 && paints.modalOwnerDisabled,
                "eject warning remains distinct from the successful completion dialog");
        // Exercise only this read-only test process's product-dialog fixture.
        // No disk-writing backend is linked and these options identify sample data.
        ProductDialog confirm;
        confirm.title = L"最后确认：整盘格式化（只读测试）";
        confirm.heading = L"整盘格式化";
        confirm.details = L"仅界面测试：没有真实目标磁盘，不执行清盘或格式化。";
        confirm.primary = L"确认格式化";
        confirm.secondary = L"取消";
        confirm.format = true;
        confirm.defaultPrimary = true; // Format policy must still enforce default cancel.
        paints.modalAction = Paints::ModalAction::Enter;
        SetTimer(window, 93, 100, nullptr);
        bool formatConfirmed = ShowProductDialog(window, confirm);
        KillTimer(window, 93);
        Require(!formatConfirmed && paints.modalFocusSecondary && IsWindowEnabled(window),
                "format dialog enforces default cancel and Enter cannot approve formatting");
        confirm.ntfs = true;
        paints.modalAction = Paints::ModalAction::ResizeEscape;
        SetTimer(window, 93, 100, nullptr);
        Require(!ShowProductDialog(window, confirm) && IsWindowEnabled(window) && paints.modalFits,
                "minimum-size NTFS format preview keeps controls visible and Escape restores its owner");
        KillTimer(window, 93);
        ProductDialog information;
        information.title = L"制作结果（只读测试）";
        information.heading = L"安装 U 盘已制作完成（模拟）";
        information.message = L"仅测试完成提示，不操作磁盘。";
        information.primary = L"完成";
        paints.modalAction = Paints::ModalAction::Enter;
        SetTimer(window, 93, 100, nullptr);
        Require(ShowProductDialog(window, information) && paints.modalFocusPrimary &&
                    IsWindowEnabled(window),
                "single-button completion dialog defaults to its primary action");
        KillTimer(window, 93);
        paints.modalAction = Paints::ModalAction::Close;
        CheckMainKeyboard(ui);
        CheckLongPathLayouts(ui);
        auto report = L"{\"simulated_log_events\":5000,\"simulated_progress_events\":10000,"
                      L"\"copy_parent_paints\":" +
                      std::to_wstring(copyParent) + L",\"copy_log_paints\":" + std::to_wstring(copyLog) +
                      L",\"verify_parent_paints\":" + std::to_wstring(verifyParent) +
                      L",\"verify_log_paints\":" + std::to_wstring(verifyLog) +
                      L",\"settings_repaints\":0,\"persisted_log_complete\":true,\"eject_warning_shown\":"
                      L"true,\"completion_dialog_shown\":true,\"format_enter_cancels\":true,"
                      L"\"format_escape_cancels\":true,\"completion_enter_accepts\":true,"
                      L"\"minimum_format_dialog_fits\":true,"
                      L"\"start_enter_and_space\":true,\"radio_keyboard_cycles\":true,"
                      L"\"long_path_layout_720\":true,\"simulated_144dpi_layout\":true,"
                      L"\"complete_path_tooltips\":true,"
                      L"\"physical_disk_writes\":0}";
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
