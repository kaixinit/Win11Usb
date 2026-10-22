#include "win11usb.hpp"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <iomanip>
#include <shellapi.h>
#include <sstream>

namespace wu {
namespace {
constexpr COLORREF Bg = RGB(250, 252, 255), Ink = RGB(23, 43, 72), Muted = RGB(100, 116, 139),
                   Blue = RGB(33, 101, 223), Line = RGB(223, 230, 239), LogBg = RGB(20, 34, 53);
constexpr UINT WM_CATALOG = WM_APP + 10, WM_LOG_LINE = WM_APP + 11, WM_PROGRESS_VALUE = WM_APP + 12,
               WM_PROMPT_VALUE = WM_APP + 13, WM_FINISHED = WM_APP + 14, WM_WORK_PATH = WM_APP + 15,
               WM_PREVIEW_DIALOG = WM_APP + 16;
enum { ID_ISO = 101, ID_REFRESH, ID_DISK, ID_FAT, ID_NTFS, ID_START, ID_STOP, ID_LOG, ID_EXPORT };
struct Ui;
void Fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void Rounded(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    auto ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
void Text(HDC dc, HFONT font, const std::wstring &s, RECT r, COLORREF c,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, font);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags);
    SelectObject(dc, old);
}
struct DialogState {
    std::wstring heading, message, details, hint, primary, secondary;
    bool danger = false, answer = false, done = false;
    HWND window = nullptr, body = nullptr, yes = nullptr, no = nullptr;
    HFONT font = nullptr, bold = nullptr;
    int dpi = 96;
    int S(int n) const { return MulDiv(n, dpi, 96); }
};
LRESULT CALLBACK DialogProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto s = (DialogState *)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        s = (DialogState *)((CREATESTRUCTW *)b)->lpCreateParams;
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)s);
        s->window = w;
    }
    if (!s)
        return DefWindowProcW(w, m, a, b);
    switch (m) {
    case WM_CREATE: {
        s->font = CreateFontW(-s->S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        s->bold = CreateFontW(-s->S(21), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        s->body =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s->details.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                            22, 148, 550, 260, w, (HMENU)3, nullptr, nullptr);
        s->no = CreateWindowExW(0, L"BUTTON", s->secondary.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 300, 426, 112, 38, w,
                                (HMENU)IDCANCEL, nullptr, nullptr);
        s->yes = CreateWindowExW(0, L"BUTTON", s->primary.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 426, 426, 146, 38, w,
                                 (HMENU)IDOK, nullptr, nullptr);
        for (HWND c : {s->body, s->no, s->yes})
            SendMessageW(c, WM_SETFONT, (WPARAM)s->font, TRUE);
        SendMessageW(s->body, EM_SETSEL, 0, 0);
        return 0;
    }
    case WM_SIZE: {
        int cw = LOWORD(b), ch = HIWORD(b);
        MoveWindow(s->body, s->S(22), s->S(140), std::max(s->S(100), cw - s->S(44)),
                   std::max(s->S(80), ch - s->S(222)), TRUE);
        MoveWindow(s->no, std::max(s->S(22), cw - s->S(294)), ch - s->S(54), s->S(112), s->S(36), TRUE);
        MoveWindow(s->yes, std::max(s->S(148), cw - s->S(168)), ch - s->S(54), s->S(146), s->S(36), TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)b)->ptMinTrackSize = {s->S(520), s->S(430)};
        return 0;
    case DM_GETDEFID:
        return MAKELRESULT(IDCANCEL, DC_HASDEFID);
    case DM_SETDEFID:
        return 0;
    case WM_COMMAND:
        if (LOWORD(a) == IDOK || LOWORD(a) == IDCANCEL) {
            s->answer = LOWORD(a) == IDOK;
            s->done = true;
            DestroyWindow(w);
            return 0;
        }
        break;
    case WM_CLOSE:
        s->answer = false;
        s->done = true;
        DestroyWindow(w);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)a, Ink);
        SetBkColor((HDC)a, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        Fill(dc, r, RGB(243, 246, 251));
        Text(dc, s->bold, s->heading, {s->S(22), s->S(17), r.right - s->S(22), s->S(48)}, Ink);
        Text(dc, s->font, s->message, {s->S(22), s->S(57), r.right - s->S(22), s->S(132)},
             s->danger ? RGB(162, 58, 50) : Muted, DT_LEFT | DT_WORDBREAK);
        Text(dc, s->font, s->hint, {s->S(22), r.bottom - s->S(78), r.right - s->S(22), r.bottom - s->S(58)},
             Muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        DeleteObject(s->font);
        DeleteObject(s->bold);
        break;
    }
    return DefWindowProcW(w, m, a, b);
}
bool Ask(HWND owner, const std::wstring &title, const std::wstring &message, const std::wstring &details,
         const std::wstring &primary = L"继续", const std::wstring &secondary = L"取消",
         bool danger = false) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Win11Usb.Native.Confirm";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    DialogState d;
    auto getDpi = (UINT(WINAPI *)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    d.dpi = getDpi ? getDpi(owner) : 96;
    d.heading = title;
    d.message = message;
    // EDIT controls require CRLF; preserve existing CRLF and normalize LF-only
    // source/notice text so license paragraphs remain readable.
    for (size_t i = 0; i < details.size(); i++) {
        if (details[i] == L'\n' && (i == 0 || details[i - 1] != L'\r'))
            d.details += L'\r';
        d.details += details[i];
    }
    d.primary = primary;
    d.secondary = secondary;
    d.danger = danger;
    d.hint = danger ? L"默认取消 · 请核对磁盘编号、名称和容量" : L"按 Esc 可返回；详细内容可滚动查看。";
    RECT pr;
    GetWindowRect(owner, &pr);
    MONITORINFO monitor{sizeof monitor};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = std::min<int>(d.S(620), monitor.rcWork.right - monitor.rcWork.left);
    int height = std::min<int>(d.S(535), monitor.rcWork.bottom - monitor.rcWork.top);
    int left = std::max(monitor.rcWork.left,
                        std::min(pr.left + (pr.right - pr.left - width) / 2, monitor.rcWork.right - width));
    int top = std::max(monitor.rcWork.top, std::min(pr.top + d.S(40), monitor.rcWork.bottom - height));
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"Win11Usb.Native.Confirm", title.c_str(),
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, left, top, width, height,
                             owner, nullptr, GetModuleHandleW(nullptr), &d);
    if (!w)
        throw Error(L"无法打开确认窗口：" + WinError());
    SendMessageW(w, WM_SETICON, ICON_SMALL, SendMessageW(owner, WM_GETICON, ICON_SMALL, 0));
    EnableWindow(owner, FALSE);
    ShowWindow(w, SW_SHOW);
    SetFocus(d.no);
    MSG msg{};
    while (!d.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(w, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        // Enter on the read-only details must never default to destructive approval.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetFocus() != d.yes) {
            SendMessageW(w, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return d.answer;
}
struct PromptMessage {
    Prompt kind;
    std::wstring detail;
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool answer = false;
};
struct Ui {
    HINSTANCE instance;
    HWND window = nullptr, isoButton = nullptr, refresh = nullptr, combo = nullptr, fat = nullptr,
         ntfs = nullptr, start = nullptr, stopButton = nullptr, log = nullptr, exportButton = nullptr;
    HFONT font = nullptr, small = nullptr, bold = nullptr, titleFont = nullptr;
    HBRUSH bgBrush = nullptr, logBrush = nullptr;
    int dpi = 96, width = 826, height = 760, logTop = 423;
    bool sample = false, runningSample = false, empty = false, busy = false, scanning = false, closed = false,
         finished = false, promptPreview = false, blockedPreview = false;
    int selected = -1;
    Catalog catalog;
    std::wstring iso, isoName = L"选择 Windows ISO 镜像", isoDirectory = L"支持 .iso 文件",
                      diskNote = L"正在读取 USB 设备…", phase = L"准备就绪", percentText = L"0.0%",
                      fileText = L"等待开始制作", footer = L"只读检查不会修改 U 盘。", sessionLog;
    Progress progress;
    std::atomic<bool> cancel{false};
    Handle scanner, worker, sessionFile;
    std::unique_ptr<Backend> backend;
    Ui(HINSTANCE h, bool p, bool r, bool e) : instance(h), sample(p), runningSample(r), empty(e) {}
    int S(int n) const { return MulDiv(n, dpi, 96); }
    RECT R(int l, int t, int r, int b) const { return {S(l), S(t), S(r), S(b)}; }
    void Fonts() {
        for (auto f : {font, small, bold, titleFont})
            if (f)
                DeleteObject(f);
        auto make = [&](int px, int weight) {
            return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        };
        font = make(14, FW_NORMAL);
        small = make(12, FW_NORMAL);
        bold = make(14, FW_SEMIBOLD);
        titleFont = make(16, FW_SEMIBOLD);
        for (HWND h : {isoButton, refresh, combo, fat, ntfs, start, stopButton, log, exportButton})
            if (h)
                SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    }
    HWND Control(const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window,
                               (HMENU)(INT_PTR)id, instance, nullptr);
    }
    void Log(const std::wstring &s) {
        if (s.empty())
            return;
        sessionLog += s + L"\r\n";
        if (sessionFile.valid()) {
            std::string bytes = Utf8(s + L"\r\n");
            DWORD n = 0;
            if (!WriteFile(sessionFile, bytes.data(), (DWORD)bytes.size(), &n, nullptr) ||
                n != bytes.size()) {
                sessionFile.reset();
                footer = L"日志文件写入失败，请保留窗口日志。";
            }
        }
        int len = GetWindowTextLengthW(log);
        if (len > 1500000) {
            SendMessageW(log, EM_SETSEL, 0, 500000);
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"[较早日志请查看 session.log]\r\n");
            len = GetWindowTextLengthW(log);
        }
        SendMessageW(log, EM_SETSEL, len, len);
        std::wstring line = s + L"\r\n";
        SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    Disk *Selection() {
        int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (i < 0)
            return nullptr;
        int j = (int)SendMessageW(combo, CB_GETITEMDATA, i, 0);
        return j >= 0 && j < (int)catalog.disks.size() ? &catalog.disks[j] : nullptr;
    }
    std::wstring Fs() { return SendMessageW(ntfs, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"NTFS" : L"FAT32"; }
    void Layout() {
        RECT c;
        GetClientRect(window, &c);
        width = MulDiv(c.right, 96, dpi);
        height = MulDiv(c.bottom, 96, dpi);
        int field = 176, right = width - 22;
        auto mv = [&](HWND h, int x, int y, int w, int t) {
            MoveWindow(h, S(x), S(y), S(std::max(10, w)), S(t), TRUE);
        };
        mv(isoButton, right - 106, 20, 106, 44);
        // Height includes the list: 56px previously left almost no rows to open.
        mv(combo, field, 132, right - 118 - field, 360);
        SendMessageW(combo, CB_SETITEMHEIGHT, -1, S(48));
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, S(52));
        mv(refresh, right - 106, 132, 106, 48);
        int fsY = 249;
        mv(fat, field, fsY, 206, 28);
        mv(ntfs, field + 212, fsY, right - field - 212, 28);
        logTop = 428;
        mv(log, 34, logTop + 42, width - 68, std::max(105, height - logTop - 104));
        mv(start, right - 112, height - 50, 112, 36);
        mv(stopButton, right - 235, height - 50, 112, 36);
        mv(exportButton, right - 235, height - 50, 112, 36);
        InvalidateRect(window, nullptr, FALSE);
    }
    void State() {
        bool can = !busy && !scanning;
        for (HWND h : {isoButton, refresh, fat, ntfs})
            EnableWindow(h, can);
        EnableWindow(combo, can && SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0);
        bool selectionReady = Selection() != nullptr;
#ifndef WIN11USB_READONLY
        selectionReady = selectionReady && !iso.empty();
#endif
        EnableWindow(start, can && ((sample && !empty) || selectionReady));
        EnableWindow(exportButton, !scanning);
        ShowWindow(exportButton, busy ? SW_HIDE : SW_SHOW);
        ShowWindow(stopButton, busy ? SW_SHOW : SW_HIDE);
        SetWindowTextW(start, busy     ? L"制作中…"
                              : sample ? L"预览制作状态"
                                       :
#ifdef WIN11USB_READONLY
                                       L"只读检查"
#else
                                       L"开始制作"
#endif
        );
        InvalidateRect(window, nullptr, FALSE);
    }
    void ApplyCatalog(Catalog c) {
        std::wstring selectedPath;
        if (auto old = Selection())
            selectedPath = old->path;
        catalog = std::move(c);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        int restore = -1;
        for (size_t i = 0; i < catalog.disks.size(); i++)
#ifdef WIN11USB_READONLY
        {
#else
            if (catalog.disks[i].eligible()) {
#endif
            int n = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)catalog.disks[i].friendly.c_str());
            SendMessageW(combo, CB_SETITEMDATA, n, i);
            if (!selectedPath.empty() && Lower(catalog.disks[i].path) == Lower(selectedPath))
                restore = n;
        }
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
            SendMessageW(combo, CB_SETCURSEL, restore >= 0 ? restore : 0, 0);
            auto selected = Selection();
            diskNote = selected && !selected->eligible() ? selected->blocked : L"";
        } else
            diskNote = catalog.disks.empty() ? L"未检测到 USB 整盘，请连接后刷新；仍为空时请导出诊断。"
                                             : L"检测到 USB，但未通过写入检查。请查看日志或导出诊断。";
        for (auto &d : catalog.disks)
            if (!d.eligible())
                Log(L"制作禁用：" + d.friendly + L" · " + d.Metadata() + L" · " + d.blocked);
        for (const auto &d : catalog.disks)
            for (const auto &e : d.probeErrors)
                Log(d.friendly + L"：" + e);
        for (auto &s : catalog.diagnostics)
            Log(s);
        scanning = false;
        State();
    }
    void ExportDiagnostics() {
        std::vector<wchar_t> path(32768);
        wcscpy_s(path.data(), path.size(), L"Win11Usb-DeviceReport.txt");
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof dialog;
        dialog.hwndOwner = window;
        dialog.lpstrTitle = L"导出设备识别诊断（仅保存报告，不操作磁盘）";
        dialog.lpstrFilter = L"诊断报告 (*.txt)\0*.txt\0";
        dialog.lpstrFile = path.data();
        dialog.nMaxFile = (DWORD)path.size();
        dialog.lpstrDefExt = L"txt";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return;
        std::wstring report = L"Win11Usb 0.2.0-preview.3 — 设备识别诊断\r\n";
        report += sample ? L"注意：这是预览示例数据，不能作为实机检测结果。\r\n"
                         : L"导出操作仅保存本次刷新所读信息，不执行清盘或格式化。\r\n";
        report += L"未收集原始序列号、PnP 实例 ID 或安装 ISO 内容。\r\n\r\n" + DiskJson(catalog) +
                  L"\r\n\r\n窗口日志：\r\n" + sessionLog;
        WriteUtf8(path.data(), report);
        Log(L"诊断报告已保存：" + std::wstring(path.data()));
    }
    void Scan() {
        if (busy || scanning || sample)
            return;
        scanning = true;
        diskNote = L"正在读取 USB 整盘设备…";
        State();
        scanner.reset(CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto u = (Ui *)p;
                auto c = new Catalog;
                try {
                    *c = ReadDisks();
                } catch (const Error &e) {
                    c->diagnostics.push_back(e.text);
                }
                if (!PostMessageW(u->window, WM_CATALOG, 0, (LPARAM)c))
                    delete c;
                return 0;
            },
            this, 0, nullptr));
        if (!scanner.valid()) {
            scanning = false;
            diskNote = L"无法启动设备查询，请再次刷新。";
            Log(diskNote);
            State();
        }
    }
    void Sample() {
        Disk d;
        d.number = 3;
        d.friendly = L"Kingston DataTraveler 3.0";
        d.size = (uint64_t)(57.8 * GiB);
        d.partitions.resize(1);
        d.letters = {L"F:\\"};
        Catalog c;
        if (blockedPreview) {
            Disk blocked = d;
            blocked.friendly = L"USB 设备 · 部分查询未完成（示例）";
            blocked.attributesKnown = false;
            blocked.layoutKnown = false;
            blocked.probeErrors = {L"离线与只读属性：访问被拒绝（示例）"};
            blocked.blocked = L"设备查询不完整，仅可查看，不能制作";
            blocked.path = L"sample-blocked";
            c.disks.push_back(blocked);
            d.path = L"sample-ready";
        }
        if (!empty)
            c.disks.push_back(d);
        ApplyCatalog(c);
        if (!empty) {
            iso = L"D:\\Windows ISO\\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06.iso";
            isoName = BaseName(iso);
            isoDirectory = Parent(iso);
            diskNote = L"示例设备。预览模式不会读取或修改磁盘。";
        }
        footer = L"界面预览模式：不会挂载 ISO，也不会修改磁盘。";
        State();
        if (runningSample)
            Simulate();
    }
    void Simulate() {
        busy = true;
        finished = false;
        progress = {4, 68.0, 36.9, L"正在复制拆分映像", L"install2.swm", 272};
        phase = progress.title;
        percentText = L"68.0%";
        fileText = L"当前文件：install2.swm · 36.9%";
        SetWindowTextW(log, L"");
        Log(L"[完成] 已创建全容量主分区（模拟）");
        Log(L"[检查] install.wim 大小 7.48 GiB，需要拆分（模拟）");
        Log(L"[完成] 已生成 2 个 SWM 文件（模拟）");
        Log(L"[复制] 安装文件与 SWM 的实时进度将在这里显示。");
        State();
    }
    void SelectIso() {
        std::vector<wchar_t> b(32768);
        OPENFILENAMEW o{};
        o.lStructSize = sizeof o;
        o.hwndOwner = window;
        o.lpstrTitle = L"选择 Windows 安装 ISO";
        o.lpstrFilter = L"Windows ISO 映像 (*.iso)\0*.iso\0所有文件\0*.*\0";
        o.lpstrFile = b.data();
        o.nMaxFile = (DWORD)b.size();
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (GetOpenFileNameW(&o)) {
            iso = b.data();
            isoName = BaseName(iso);
            isoDirectory = Parent(iso);
            State();
        }
    }
    void Start() {
        if (sample) {
            if (blockedPreview) {
                Ask(window, L"USB 只读信息 · 示例",
                    L"这是界面预览，不会操作磁盘。部分查询失败的 USB 也应显示在只读列表中。",
                    DiskJson(catalog), L"确定", L"返回");
                return;
            }
            if (promptPreview) {
                SendMessageW(window, WM_PREVIEW_DIALOG, 0, 0);
                return;
            }
            Simulate();
            return;
        }
        auto d = Selection();
        if (!d)
            return;
#ifdef WIN11USB_READONLY
        try {
            std::wstring details = L"USB 整盘\r\n" + d->friendly + L"\r\n" + d->Metadata() +
                                   L"\r\nUSB 识别依据：" + d->usbEvidence + L"\r\n\r\n";
            details += d->eligible() ? L"本次设备信息查询完整。\r\n"
                                     : L"该设备目前不能用于制作：" + d->blocked + L"\r\n";
            for (const auto &error : d->probeErrors)
                details += error + L"\r\n";
            details +=
                L"\r\n信息来自最近一次刷新。可用“导出诊断”保存报告。\r\n.NET / PowerShell / WMI：不需要。";
            Ask(window, L"USB 设备只读信息",
                L"本版本只读取设备信息，不会挂载 ISO、清盘、格式化或复制安装文件。", details, L"确定",
                L"返回");
        } catch (const Error &e) {
            Log(L"检查失败：" + e.text);
            Ask(window, L"只读检查未通过", L"未修改任何磁盘。", e.text, L"确定", L"返回");
        }
#else
        if (iso.empty())
            return;
        Request request{iso, Fs(), ExePath(), *d};
        if (!Ask(window, L"先检查本次制作目标",
                 L"将自动挂载并识别 ISO。清盘前还会单独显示整盘确认，默认取消。",
                 L"安装镜像\r\n" + iso + L"\r\n\r\n写入 U 盘\r\n" + d->friendly + L"\r\n" + d->Metadata() +
                     L"\r\n\r\n文件系统：" + Fs(),
                 L"继续检查", L"返回检查"))
            return;
        cancel = false;
        busy = true;
        finished = false;
        sessionLog.clear();
        sessionFile.reset();
        SetWindowTextW(log, L"");
        phase = L"正在准备 ISO";
        progress = {1, -1, 0, phase, L"", 0};
        footer = L"正在准备镜像，请保持文件和 U 盘可用。";
        State();
        auto payload = new std::pair<Ui *, Request>(this, request);
        backend = CreateNativeBackend();
        worker.reset(CreateThread(
            nullptr, 0,
            [](LPVOID value) -> DWORD {
                std::unique_ptr<std::pair<Ui *, Request>> p((std::pair<Ui *, Request> *)value);
                Ui *u = p->first;
                Result r;
                try {
                    Hooks h;
                    h.workReady = [u](const std::wstring &s) {
                        auto t = new std::wstring(s);
                        if (!PostMessageW(u->window, WM_WORK_PATH, 0, (LPARAM)t))
                            delete t;
                    };
                    h.log = [u](const std::wstring &s) {
                        auto t = new std::wstring(s);
                        if (!PostMessageW(u->window, WM_LOG_LINE, 0, (LPARAM)t))
                            delete t;
                    };
                    h.progress = [u](const Progress &v) {
                        auto t = new Progress(v);
                        if (!PostMessageW(u->window, WM_PROGRESS_VALUE, 0, (LPARAM)t))
                            delete t;
                    };
                    h.ask = [u](Prompt k, const std::wstring &s) {
                        auto q = std::make_shared<PromptMessage>();
                        q->kind = k;
                        q->detail = s;
                        auto m = new std::shared_ptr<PromptMessage>(q);
                        if (!PostMessageW(u->window, WM_PROMPT_VALUE, 0, (LPARAM)m)) {
                            delete m;
                            return false;
                        }
                        while (WaitForSingleObject(q->ready, 100) == WAIT_TIMEOUT)
                            if (u->cancel)
                                return false;
                        return q->answer;
                    };
                    r = RunWorkflow(p->second, *u->backend, h, u->cancel);
                } catch (const Error &e) {
                    r.error = e.text;
                } catch (const std::exception &e) {
                    r.error = Wide(e.what());
                }
                auto out = new Result(r);
                if (!PostMessageW(u->window, WM_FINISHED, 0, (LPARAM)out))
                    delete out;
                return 0;
            },
            payload, 0, nullptr));
        if (!worker.valid()) {
            delete payload;
            busy = false;
            backend.reset();
            State();
            throw Error(L"无法创建后台制作线程：" + WinError());
        }
#endif
    }
    void Stop(bool close) {
        if (!busy) {
            if (close)
                DestroyWindow(window);
            return;
        }
        if (sample) {
            busy = false;
            phase = L"准备就绪";
            progress = {};
            State();
            if (close)
                DestroyWindow(window);
            return;
        }
        if (!Ask(window, L"结束当前制作？",
                 L"结束不能回滚格式化或复制。U 盘可能需要重新制作，ISO 可能保留挂载。",
                 L"只会结束本程序及其启动的命令，不会结束其他应用。", L"结束并退出", L"继续等待", true))
            return;
        cancel = true;
        if (sessionFile.valid()) {
            Log(L"用户确认强制结束。未完成的 U 盘需要重新制作。");
            FlushFileBuffers(sessionFile);
        }
        EnableWindow(window, FALSE);
        if (backend)
            backend->Abort(true);
        if (worker.valid())
            WaitForSingleObject(worker, 2000);
        ExitProcess(2);
    }
    void Icon(HDC dc, int type, int x, int y, COLORREF color = Blue) {
        auto pen = CreatePen(PS_SOLID, S(2), color);
        auto op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        auto ln = [&](int x1, int y1, int x2, int y2) {
            MoveToEx(dc, S(x + x1), S(y + y1), nullptr);
            LineTo(dc, S(x + x2), S(y + y2));
        };
        if (type == 0) {
            Rectangle(dc, S(x + 5), S(y + 2), S(x + 23), S(y + 29));
            ln(16, 2, 23, 9);
            ln(16, 2, 16, 9);
            ln(16, 9, 23, 9);
            Ellipse(dc, S(x + 16), S(y + 18), S(x + 31), S(y + 33));
            ln(23, 21, 23, 29);
            ln(19, 25, 27, 25);
        } else if (type == 1) {
            RoundRect(dc, S(x + 8), S(y + 10), S(x + 25), S(y + 32), S(7), S(7));
            Rectangle(dc, S(x + 11), S(y + 2), S(x + 22), S(y + 12));
            ln(15, 4, 15, 7);
            ln(19, 4, 19, 7);
            Ellipse(dc, S(x + 14), S(y + 22), S(x + 19), S(y + 27));
        } else if (type == 2) {
            POINT pts[24];
            for (int i = 0; i < 24; i++) {
                double a = i * 3.141592653589793 / 12, r = i % 3 == 0 ? 15 : 12;
                pts[i] = {S(x + 17 + (int)(std::cos(a) * r)), S(y + 17 + (int)(std::sin(a) * r))};
            }
            Polygon(dc, pts, 24);
            Ellipse(dc, S(x + 11), S(y + 11), S(x + 23), S(y + 23));
        } else if (type == 3) {
            for (int i = 0; i < 3; i++) {
                ln(3, 5 + i * 8, 5, 5 + i * 8);
                ln(10, 5 + i * 8, 29, 5 + i * 8);
            }
        } else {
            Rectangle(dc, S(x + 1), S(y + 2), S(x + 23), S(y + 22));
            ln(5, 7, 10, 12);
            ln(10, 12, 5, 17);
            ln(13, 17, 19, 17);
        }
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
    }
    void Paint() {
        PAINTSTRUCT ps;
        HDC dest = BeginPaint(window, &ps);
        RECT cr;
        GetClientRect(window, &cr);
        HDC dc = CreateCompatibleDC(dest);
        HBITMAP bitmap = CreateCompatibleBitmap(dest, cr.right, cr.bottom);
        auto old = SelectObject(dc, bitmap);
        Fill(dc, cr, Bg);
        int right = width - 22, field = 176;
        for (int y : {114, 230, 311})
            Fill(dc, R(22, y, right, y + 1), Line);
        for (auto xy : std::vector<std::pair<int, int>>{{22, 23}, {22, 132}, {22, 248}})
            Rounded(dc, R(xy.first, xy.second, xy.first + 50, xy.second + 50), RGB(232, 240, 254),
                    RGB(232, 240, 254), S(12));
        Icon(dc, 0, 31, 30);
        Icon(dc, 1, 30, 140);
        Icon(dc, 2, 29, 254);
        Text(dc, bold, L"安装镜像", R(88, 30, 168, 66), Ink);
        Text(dc, bold, L"写入U盘", R(88, 142, 168, 176), Ink);
        Text(dc, bold, L"文件系统", R(88, 250, 168, 288), Ink);
        Rounded(dc, R(field, 20, right - 118, 77), RGB(248, 250, 253), RGB(203, 215, 231), S(6));
        Text(dc, font, isoName, R(field + 12, 27, right - 130, 74), iso.empty() ? Muted : Ink,
             DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        Text(dc, small, isoDirectory, R(field, 81, right, 108), Muted,
             DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        if (!Selection())
            Text(dc, font, scanning ? L"正在读取 USB 设备…" : L"连接 U 盘后选择设备",
                 R(field + 12, 144, right - 150, 176), Muted);
        auto disk = Selection();
        std::wstring warning =
            disk ? L"△ 制作将清除磁盘 " + std::to_wstring(disk->number) + L" 的全部分区和文件。"
                 : L"△ 制作将清除所选 U 盘的全部分区和文件。";
        Text(dc, small, warning, R(field, 191, right, 211), RGB(156, 87, 13));
        if (!diskNote.empty())
            Text(dc, small, diskNote, R(field, 211, right, 229), Muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        Text(dc, small, L"install.wim 超过 4 GiB 时按相同规则拆分", R(field, 280, right, 303), Muted);
        Icon(dc, 3, 22, 328);
        Text(dc, titleFont, phase, R(54, 326, right - 180, 352), Ink);
        Text(dc, small,
             busy && progress.stage == 4 ? L"复制整体"
             : finished                  ? L"制作结果"
                                         : L"当前操作",
             R(right - 178, 327, right - 65, 352), Muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Text(dc, titleFont, finished ? L"已完成" : percentText, R(right - 70, 327, right, 352), Blue,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t *stages[] = {L"检查镜像", L"格式化", L"处理映像", L"复制文件", L"校验", L"清理"};
        int step = (right - 35 - 22) / 5;
        for (int i = 0; i < 6; i++) {
            int x = 22 + i * step;
            bool done = finished || (progress.stage > i + 1), active = progress.stage == i + 1;
            COLORREF c = done || active ? RGB(40, 120, 240) : RGB(220, 228, 239);
            HBRUSH b = CreateSolidBrush(c);
            auto ob = SelectObject(dc, b);
            auto op = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, S(x), S(366), S(x + 10), S(376));
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(b);
            if (done) {
                auto check = CreatePen(PS_SOLID, S(1), RGB(255, 255, 255));
                op = SelectObject(dc, check);
                MoveToEx(dc, S(x + 2), S(371), nullptr);
                LineTo(dc, S(x + 4), S(373));
                LineTo(dc, S(x + 8), S(369));
                SelectObject(dc, op);
                DeleteObject(check);
            }
            Text(dc, small, stages[i], R(x + 15, 360, std::min(x + 86, right), 381), active ? Blue : Muted);
            if (i < 5)
                Fill(dc, R(x + 88, 370, x + step - 8, 371), done ? RGB(155, 186, 237) : Line);
        }
        Fill(dc, R(22, 386, right, 391), RGB(225, 233, 245));
        double value = std::max(0.0, std::min(100.0, progress.overall));
        if (finished)
            value = 100;
        Fill(dc, R(22, 386, 22 + (int)((right - 22) * value / 100), 391), Blue);
        Text(dc, small, fileText, R(22, 395, right - 100, 416), Muted,
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (progress.elapsed) {
            wchar_t b[32];
            swprintf(b, 32, L"用时 %02llu:%02llu", progress.elapsed / 60, progress.elapsed % 60);
            Text(dc, small, b, R(right - 100, 395, right, 416), Muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        Fill(dc, R(22, 419, right, 421), Line);
        if (progress.current > 0)
            Fill(dc, R(22, 419, 22 + (int)((right - 22) * std::min(100.0, progress.current) / 100), 421),
                 RGB(102, 156, 247));
        Rounded(dc, R(22, logTop, right, height - 65), LogBg, LogBg, S(9));
        Icon(dc, 4, 35, logTop + 12, RGB(124, 179, 255));
        Text(dc, bold, L"制作日志", R(64, logTop + 9, right - 100, logTop + 35), RGB(229, 240, 255));
        Text(dc, small,
             busy       ? L"制作中"
             : finished ? L"已结束"
                        : L"等待开始",
             R(right - 90, logTop + 9, right - 15, logTop + 35), RGB(207, 224, 251),
             DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        Text(dc, small, footer, R(22, height - 52, right - 250, height - 11), Muted,
             DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        BitBlt(dest, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &ps);
    }
    void DrawButton(const DRAWITEMSTRUCT &d) {
        RECT r = d.rcItem;
        bool primary = d.CtlID == ID_START, disabled = (d.itemState & ODS_DISABLED) != 0;
        COLORREF bg = primary ? (disabled ? RGB(157, 185, 234) : RGB(37, 99, 235)) : RGB(237, 243, 253);
        Rounded(d.hDC, r, bg, (d.itemState & ODS_FOCUS) ? Blue : bg, S(7));
        wchar_t text[128];
        GetWindowTextW(d.hwndItem, text, 128);
        Text(d.hDC, bold, text, r,
             primary    ? RGB(255, 255, 255)
             : disabled ? Muted
                        : RGB(36, 86, 165),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d.itemState & ODS_FOCUS) {
            InflateRect(&r, -3, -3);
            DrawFocusRect(d.hDC, &r);
        }
    }
    LRESULT Message(UINT m, WPARAM a, LPARAM b) {
        switch (m) {
        case WM_CREATE: {
            HMENU systemMenu = GetSystemMenu(window, FALSE);
            AppendMenuW(systemMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(systemMenu, MF_STRING, 0x1ff0, L"关于 Win11Usb / 许可证");
            bgBrush = CreateSolidBrush(Bg);
            logBrush = CreateSolidBrush(LogBg);
            isoButton = Control(L"BUTTON", L"选择 ISO…", WS_TABSTOP | BS_OWNERDRAW, ID_ISO);
            refresh = Control(L"BUTTON", L"⟳ 刷新", WS_TABSTOP | BS_OWNERDRAW, ID_REFRESH);
            combo = Control(WC_COMBOBOXW, L"",
                            WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                            ID_DISK);
            fat = Control(L"BUTTON", L"FAT32（UEFI 兼容性更广）", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                          ID_FAT);
            ntfs = Control(L"BUTTON", L"NTFS（部分 UEFI 固件可能不支持启动）",
                           WS_TABSTOP | BS_AUTORADIOBUTTON, ID_NTFS);
            SendMessageW(fat, BM_SETCHECK, BST_CHECKED, 0);
            start = Control(L"BUTTON", L"开始制作", WS_TABSTOP | BS_OWNERDRAW, ID_START);
            stopButton = Control(L"BUTTON", L"强制结束", WS_TABSTOP | BS_OWNERDRAW, ID_STOP);
            exportButton = Control(L"BUTTON", L"导出诊断", WS_TABSTOP | BS_OWNERDRAW, ID_EXPORT);
            log = Control(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                          ID_LOG);
            SendMessageW(log, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
            Fonts();
            Log(L"C++ / Win32 原生版本：不加载 .NET、PowerShell 或 WMI。");
#ifdef WIN11USB_READONLY
            Log(L"只读验证版：不挂载 ISO，不清盘，不格式化，不复制。");
            footer = L"只读版本：仅检查文件和 USB 整盘身份。";
#else
            footer = L"制作前会再次校对设备身份，并单独确认整盘格式化。";
#endif
            Layout();
            if (sample)
                Sample();
            else
                Scan();
            return 0;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto p = (MINMAXINFO *)b;
            p->ptMinTrackSize = {S(760), S(620)};
            return 0;
        }
        case WM_DPICHANGED: {
            dpi = HIWORD(a);
            Fonts();
            auto r = (RECT *)b;
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            if ((HWND)b == log) {
                SetTextColor((HDC)a, RGB(224, 236, 254));
                SetBkColor((HDC)a, LogBg);
                return (LRESULT)logBrush;
            }
            SetTextColor((HDC)a, Ink);
            SetBkColor((HDC)a, Bg);
            return (LRESULT)bgBrush;
        case WM_DRAWITEM: {
            auto d = (DRAWITEMSTRUCT *)b;
            if (d->CtlType == ODT_BUTTON) {
                DrawButton(*d);
                return TRUE;
            }
            if (d->CtlType == ODT_COMBOBOX) {
                Fill(d->hDC, d->rcItem,
                     (d->itemState & ODS_SELECTED) ? RGB(232, 240, 254) : RGB(248, 250, 253));
                if (d->itemID != (UINT)-1) {
                    int j = (int)d->itemData;
                    if (j >= 0 && j < (int)catalog.disks.size()) {
                        auto disk = catalog.disks[j];
                        RECT r = d->rcItem;
                        r.left += S(10);
                        r.top += S(3);
                        r.bottom = r.top + S(24);
                        Text(d->hDC, bold, disk.friendly, r, Ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                        r.top += S(23);
                        r.bottom = r.top + S(21);
                        Text(d->hDC, small, disk.Metadata(), r, Muted,
                             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                } else {
                    RECT r = d->rcItem;
                    r.left += S(12);
                    Text(d->hDC, font, scanning ? L"正在读取 USB 设备…" : L"连接 U 盘后选择设备", r, Muted);
                }
                return TRUE;
            }
            break;
        }
        case WM_MEASUREITEM:
            ((MEASUREITEMSTRUCT *)b)->itemHeight = S(52);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(a)) {
            case ID_ISO:
                SelectIso();
                break;
            case ID_REFRESH:
                Scan();
                break;
            case ID_START:
                Start();
                break;
            case ID_STOP:
                Stop(false);
                break;
            case ID_DISK:
                if (auto selected = Selection())
                    diskNote = selected->eligible() ? L"" : selected->blocked;
                State();
                break;
            case ID_EXPORT:
                ExportDiagnostics();
                break;
            case ID_FAT:
            case ID_NTFS:
                InvalidateRect(window, nullptr, FALSE);
                break;
            }
            return 0;
        case WM_DEVICECHANGE:
            if (!sample && !busy)
                SetTimer(window, 91, 500, nullptr);
            return 0;
        case WM_TIMER:
            if (a == 91) {
                KillTimer(window, 91);
                Scan();
            }
            return 0;
        case WM_CATALOG: {
            std::unique_ptr<Catalog> c((Catalog *)b);
            ApplyCatalog(std::move(*c));
            return 0;
        }
        case WM_LOG_LINE: {
            std::unique_ptr<std::wstring> s((std::wstring *)b);
            Log(*s);
            return 0;
        }
        case WM_WORK_PATH: {
            std::unique_ptr<std::wstring> path((std::wstring *)b);
            sessionFile.reset(CreateFileW((*path + L"\\session.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!sessionFile.valid()) {
                Log(L"无法创建持续日志文件：" + WinError());
                return 0;
            }
            auto bytes = Utf8(sessionLog);
            DWORD n = 0;
            WriteFile(sessionFile, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
            footer = L"日志目录：" + *path;
            return 0;
        }
        case WM_PREVIEW_DIALOG: {
            bool answer =
                Ask(window, L"整盘格式化 · 弹窗预览",
                    L"这是界面预览，不会修改磁盘。正式制作时会删除所选 U 盘的全部分区和文件。",
                    L"磁盘 3 | Kingston DataTraveler 3.0 | 57.8 GiB\r\n当前 2 "
                    L"个分区\r\n\r\n文件系统：FAT32\r\n卷标：WIN11_26H2\r\n\r\n安装镜像：\r\nD:\\Windows "
                    L"ISO\\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06."
                    L"iso\r\n\r\n完成后重建为一个全容量 FAT32 主分区。",
                    L"确认格式化", L"取消", true);
            Log(answer ? L"弹窗预览：已选择确认（未执行写盘）。" : L"弹窗预览：已取消（未执行写盘）。");
            return 0;
        }
        case WM_PROGRESS_VALUE: {
            std::unique_ptr<Progress> p((Progress *)b);
            progress = *p;
            phase = p->title;
            std::wostringstream t;
            t << std::fixed << std::setprecision(1) << std::max(0.0, p->overall) << L"%";
            percentText = p->overall < 0 ? L"…" : t.str();
            fileText = p->file.empty() ? L"等待当前文件进度…" : L"当前文件：" + p->file;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_PROMPT_VALUE: {
            std::unique_ptr<std::shared_ptr<PromptMessage>> holder((std::shared_ptr<PromptMessage> *)b);
            auto q = *holder;
            bool fmt = q->kind == Prompt::Format;
            std::wstring name = fmt                           ? L"整盘格式化"
                                : q->kind == Prompt::Cleanup  ? L"清理临时 SWM"
                                : q->kind == Prompt::EjectUsb ? L"安全弹出 U 盘"
                                : q->kind == Prompt::EjectIso ? L"弹出 Windows ISO"
                                                              : L"继续制作";
            std::wstring msg = fmt ? L"确认后将删除这支 U 盘的全部分区和文件。请确认已备份。"
                                   : L"请核对本步信息，再决定是否执行。";
            if (fmt && Fs() == L"NTFS")
                msg += L"\r\nNTFS：部分 UEFI 固件可能不支持直接启动。";
            q->answer = !cancel && Ask(window, name, msg, q->detail, fmt ? L"确认格式化" : L"执行",
                                       fmt                           ? L"取消"
                                       : q->kind == Prompt::Cleanup  ? L"保留文件"
                                       : q->kind == Prompt::EjectUsb ? L"保持连接"
                                       : q->kind == Prompt::EjectIso ? L"保留挂载"
                                                                     : L"取消",
                                       fmt);
            SetEvent(q->ready);
            return 0;
        }
        case WM_FINISHED: {
            std::unique_ptr<Result> r((Result *)b);
            busy = false;
            finished = r->copied;
            phase = r->copied ? L"制作完成" : r->cancelled ? L"制作已取消" : L"制作未完成";
            if (r->copied) {
                progress.stage = 6;
                progress.overall = 100;
                percentText = L"已完成";
            }
            fileText = r->copied ? L"复制文件大小校验通过" : L"日志保留在窗口中。";
            if (!r->error.empty() && sessionLog.find(L"已停止：" + r->error + L"\r\n") == sessionLog.npos)
                Log(L"已停止：" + r->error);
            footer = r->work.empty() ? L"制作日志保留在窗口中。" : L"日志目录：" + r->work;
            try {
                if (sessionFile.valid()) {
                    FlushFileBuffers(sessionFile);
                    sessionFile.reset();
                } else if (!r->work.empty() && !FileExists(r->work + L"\\session.log"))
                    WriteUtf8(r->work + L"\\session.log", sessionLog);
            } catch (const Error &e) {
                Log(e.text);
            }
            backend.reset();
            State();
            Scan();
            if (!r->error.empty())
                Ask(window, L"本次制作已停止",
                    r->rebuildStarted ? L"目标磁盘可能需要重新制作，请查看日志。"
                                      : L"本次未开始清盘，请查看日志。",
                    r->error, L"确定", L"返回");
            return 0;
        }
        case WM_CLOSE:
            Stop(true);
            return 0;
        case WM_SYSCOMMAND:
            if ((a & 0xfff0) == 0x1ff0) {
                std::wstring detail;
                for (int id : {302, 301}) {
                    auto b = ResourceBytes(id);
                    detail += Wide(std::string(b.begin(), b.end())) + L"\r\n\r\n";
                }
                Ask(window, L"关于 Win11Usb",
                    L"C++ / Win32 原生验证版 · 无需 .NET、PowerShell 或 WMI。\r\nFAT32 工具的 GPL "
                    L"许可证和对应源码会随任务一并保存。",
                    detail, L"确定", L"返回");
                return 0;
            }
            break;
        case WM_DESTROY:
            closed = true;
            cancel = true;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, m, a, b);
    }
};
LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto u = (Ui *)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        u = (Ui *)((CREATESTRUCTW *)b)->lpCreateParams;
        u->window = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)u);
    }
    if (!u)
        return DefWindowProcW(w, m, a, b);
    try {
        return u->Message(m, a, b);
    } catch (const Error &e) {
        MessageBoxW(w, e.text.c_str(), L"Win11Usb", MB_OK | MB_ICONERROR);
        return 0;
    }
}
} // namespace
int RunUi(HINSTANCE instance, bool preview, bool running, bool empty, bool promptPreview,
          bool blockedPreview) {
    auto setDpi = (BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT))GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                        "SetProcessDpiAwarenessContext");
    if (setDpi)
        setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&cc);
    auto u = new Ui(instance, preview, running, empty);
    u->promptPreview = promptPreview;
    u->blockedPreview = blockedPreview;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"Win11Usb.Native.Main";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    RegisterClassExW(&wc);
    auto getDpi = (UINT(WINAPI *)())GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    u->dpi = getDpi ? (int)getDpi() : 96;
    RECT r{0, 0, u->S(826), u->S(760)};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = std::min(r.right - r.left, wa.right - wa.left),
        h = std::min(r.bottom - r.top, wa.bottom - wa.top);
    HWND window = CreateWindowExW(0, wc.lpszClassName,
#ifdef WIN11USB_READONLY
                                  L"Windows 安装 U 盘制作工具 · 原生只读验证版",
#else
                                  L"Windows 安装 U 盘制作工具 · C++ 原生验证版",
#endif
                                  WS_OVERLAPPEDWINDOW, (wa.right - w) / 2, (wa.bottom - h) / 2, w, h, nullptr,
                                  nullptr, instance, u);
    if (!window)
        throw Error(L"原生窗口创建失败：" + WinError());
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // Closing must also stop a stalled read-only device query. All worker memory
    // remains alive until process exit; no detached thread can outlive the app.
    if (u->scanner.valid())
        WaitForSingleObject(u->scanner, 1000);
    if (u->worker.valid())
        WaitForSingleObject(u->worker, 1000);
    ExitProcess((UINT)msg.wParam);
    return 0;
}
} // namespace wu
