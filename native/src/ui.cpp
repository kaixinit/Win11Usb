#include "dialog.hpp"
#include "dialog_specs.hpp"
#include "product_icons.hpp"
#include "ui_updates.hpp"
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
constexpr UINT WM_CATALOG = WM_APP + 10, WM_UI_UPDATE = WM_APP + 11, WM_PROMPT_VALUE = WM_APP + 13,
               WM_FINISHED = WM_APP + 14, WM_WORK_PATH = WM_APP + 15, WM_PREVIEW_DIALOG = WM_APP + 16;
constexpr DWORD MainWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr UINT_PTR UpdateTimer = 92;
constexpr UINT UpdateInterval = 100;
constexpr int DefaultWidth = 840, DefaultHeight = 760, MinimumWidth = 720, MinimumHeight = 650;
enum { ID_ISO = 101, ID_REFRESH, ID_DISK, ID_FAT, ID_NTFS, ID_START, ID_STOP, ID_LOG, ID_EXPORT };
bool DispatchMainKey(HWND window, const MSG &message) {
    if (message.message != WM_KEYDOWN || message.wParam != VK_RETURN || !IsWindowEnabled(window))
        return false;
    HWND focus = GetFocus();
    if (!focus || !IsChild(window, focus) || !IsWindowEnabled(focus))
        return false;
    wchar_t cls[32]{};
    GetClassNameW(focus, cls, 32);
    DWORD type = GetWindowLongPtrW(focus, GWL_STYLE) & BS_TYPEMASK;
    if (_wcsicmp(cls, L"BUTTON") ||
        (type != BS_OWNERDRAW && type != BS_PUSHBUTTON && type != BS_DEFPUSHBUTTON))
        return false;
    // IsDialogMessage sends IDOK (1) for owner-drawn buttons with product IDs.
    // Activate the focused action explicitly, while leaving combo/radio keys
    // native.
    SendMessageW(focus, BM_CLICK, 0, 0);
    return true;
}
struct Ui;
void Fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void Rounded(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x = r.left + .5f, y = r.top + .5f, w = r.right - r.left - 1.0f, h = r.bottom - r.top - 1.0f;
    float d = (float)std::min({radius * 2, (int)w, (int)h});
    if (w <= 0 || h <= 0 || d <= 0)
        return;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)));
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
}
void Text(HDC dc, HFONT font, const std::wstring &s, RECT r, COLORREF c,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, font);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags);
    SelectObject(dc, old);
}
// Match the published WPF wrap opportunities after filename separators, without
// changing the actual path used by file APIs or the tooltip.
std::wstring WrappedName(HDC dc, HFONT font, const std::wstring &name, int available) {
    auto previous = SelectObject(dc, font);
    std::wstring result;
    size_t begin = 0;
    while (begin < name.size()) {
        size_t end = begin, separator = begin;
        while (end < name.size()) {
            SIZE extent{};
            GetTextExtentPoint32W(dc, name.c_str() + begin, (int)(end - begin + 1), &extent);
            if (extent.cx > available && end > begin)
                break;
            if (name[end] == L'_' || name[end] == L'-' || name[end] == L' ' || name[end] == L'\\')
                separator = end + 1;
            ++end;
        }
        if (end < name.size() && separator > begin)
            end = separator;
        if (end == begin)
            ++end;
        if (!result.empty())
            result += L"\r\n";
        result += name.substr(begin, end - begin);
        begin = end;
    }
    SelectObject(dc, previous);
    return result;
}
bool Ask(HWND owner, const std::wstring &title, const std::wstring &message, const std::wstring &details,
         const std::wstring &primary = L"继续", const std::wstring &secondary = L"取消",
         bool danger = false) {
    return ShowProductDialog(owner, title, message, details, primary, secondary, danger);
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
    HFONT font = nullptr, small = nullptr, bold = nullptr, titleFont = nullptr, labelFont = nullptr,
          stageFont = nullptr, logFont = nullptr, numberFont = nullptr;
    HBRUSH bgBrush = nullptr, logBrush = nullptr;
    HWND tooltip = nullptr, hotButton = nullptr;
    int dpi = 96, width = 824, height = 721, logTop = 423;
    int isoHeight = 44, isoDivider = 100, diskTop = 119, diskHeight = 54, diskDivider = 217, fsTop = 235,
        progressTop = 312;
    std::wstring wrappedIso;
    bool tooltipAdded = false, diskTooltipAdded = false;
    std::wstring diskTooltip;
    bool sample = false, runningSample = false, empty = false, busy = false, scanning = false,
         closed = false, finished = false, promptPreview = false, blockedPreview = false;
    int selected = -1;
    Catalog catalog;
    std::wstring iso, isoName = L"选择 Windows ISO 镜像", isoDirectory = L"支持 .iso 文件",
                      diskNote = L"正在读取 USB 设备…", phase = L"准备就绪", percentText = L"0.0%",
                      fileText = L"等待开始制作", footer = L"只读检查不会修改 U 盘。", sessionLog;
    Progress progress;
    Request activeRequest;
    UiUpdates updates;
    std::atomic<bool> cancel{false};
    Handle scanner, worker, sessionFile;
    std::unique_ptr<Backend> backend;
    Ui(HINSTANCE h, bool p, bool r, bool e) : instance(h), sample(p), runningSample(r), empty(e) {}
    int S(int n) const { return MulDiv(n, dpi, 96); }
    RECT R(int l, int t, int r, int b) const { return {S(l), S(t), S(r), S(b)}; }
    void Fonts() {
        for (auto f : {font, small, bold, titleFont, labelFont, stageFont, logFont, numberFont})
            if (f)
                DeleteObject(f);
        auto make = [&](int px, int weight) {
            return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        };
        font = make(14, FW_NORMAL);
        small = make(13, FW_NORMAL);
        bold = make(14, FW_SEMIBOLD);
        titleFont = make(15, FW_SEMIBOLD);
        labelFont = make(15, FW_SEMIBOLD);
        numberFont = make(16, FW_SEMIBOLD);
        stageFont = make(11, FW_NORMAL);
        logFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Consolas");
        for (HWND h : {isoButton, refresh, combo, fat, ntfs, start, stopButton, log, exportButton})
            if (h)
                SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        if (log)
            SendMessageW(log, WM_SETFONT, (WPARAM)logFont, TRUE);
    }
    RECT ProgressRect() const { return R(22, progressTop - 15, width - 22, logTop - 1); }
    HWND Control(const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window,
                               (HMENU)(INT_PTR)id, instance, nullptr);
    }
    void Log(const std::wstring &s) {
        if (s.empty())
            return;
        AppendLog(s + L"\r\n");
    }
    void AppendLog(const std::wstring &text) {
        sessionLog += text;
        if (sessionFile.valid()) {
            std::string bytes = Utf8(text);
            DWORD n = 0;
            if (!WriteFile(sessionFile, bytes.data(), (DWORD)bytes.size(), &n, nullptr) ||
                n != bytes.size()) {
                sessionFile.reset();
                footer = L"日志文件写入失败，请保留窗口日志。";
                auto area = R(22, height - 52, width - 272, height - 11);
                InvalidateRect(window, &area, FALSE);
            }
        }
        // Keep the newest portion visible even if a delayed UI receives one very
        // large batch. The persisted session log above always receives all text.
        std::wstring visible = text;
        if (visible.size() > 1000000) {
            auto cut = visible.find(L'\n', visible.size() - 1000000);
            visible = L"[较早日志请查看 session.log]\r\n" +
                      visible.substr(cut == visible.npos ? visible.size() - 1000000 : cut + 1);
        }
        // Appending and moving the caret can otherwise paint the EDIT repeatedly.
        // Resume and invalidate once after the complete batch, including its
        // scrollbar.
        SendMessageW(log, WM_SETREDRAW, FALSE, 0);
        int len = GetWindowTextLengthW(log);
        if ((size_t)len + visible.size() > 1500000) {
            SendMessageW(log, EM_SETSEL, 0, std::max<size_t>(500000, len + visible.size() - 1400000));
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"[较早日志请查看 session.log]\r\n");
            len = GetWindowTextLengthW(log);
        }
        SendMessageW(log, EM_SETSEL, len, len);
        SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)visible.c_str());
        SendMessageW(log, WM_SETREDRAW, TRUE, 0);
        // EDIT defers line layout while redraw is disabled. Scroll after enabling
        // it so a batched append follows the newest line instead of staying at top.
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
        RedrawWindow(log, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_NOERASE);
    }
    void QueueLog(const std::wstring &s) {
        if (updates.Log(s))
            PostMessageW(window, WM_UI_UPDATE, 0, 0);
    }
    void QueueProgress(const Progress &p) {
        if (updates.Update(p))
            PostMessageW(window, WM_UI_UPDATE, 0, 0);
    }
    void FlushUpdates() {
        KillTimer(window, UpdateTimer);
        auto batch = updates.Take();
        if (!batch.logs.empty()) {
            std::wstring text;
            for (const auto &line : batch.logs)
                text += line + L"\r\n";
            AppendLog(text);
        }
        if (batch.progress) {
            progress = std::move(*batch.progress);
            phase = progress.title;
            std::wostringstream t;
            t << std::fixed << std::setprecision(1) << std::max(0.0, progress.overall) << L"%";
            percentText = progress.overall < 0 ? L"…" : t.str();
            fileText = progress.file.empty() ? L"等待当前文件进度…" : L"当前文件：" + progress.file;
            if (!progress.file.empty() && progress.current > 0) {
                std::wostringstream current;
                current << std::fixed << std::setprecision(1) << std::min(100.0, progress.current);
                fileText += L" · " + current.str() + L"%";
            }
            auto area = ProgressRect();
            InvalidateRect(window, &area, FALSE);
        }
    }
    Disk *Selection() {
        int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (i < 0)
            return nullptr;
        int j = (int)SendMessageW(combo, CB_GETITEMDATA, i, 0);
        return j >= 0 && j < (int)catalog.disks.size() ? &catalog.disks[j] : nullptr;
    }
    std::wstring Fs() { return SendMessageW(ntfs, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"NTFS" : L"FAT32"; }
    int TextHeight(HDC dc, HFONT f, const std::wstring &text, int available, int minimum = 0) {
        RECT rect{0, 0, S(std::max(10, available)), 0};
        auto previous = SelectObject(dc, f);
        DrawTextW(dc, text.c_str(), (int)text.size(), &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous);
        return std::max(minimum, MulDiv(rect.bottom, 96, dpi));
    }
    int DiskTextHeight(HDC dc, const Disk &disk, int available) {
        return TextHeight(dc, bold, disk.friendly, available, 19) +
               TextHeight(dc, small, disk.Metadata(), available, 17) + 21;
    }
    void Layout() {
        RECT c;
        GetClientRect(window, &c);
        width = MulDiv(c.right, 96, dpi);
        height = MulDiv(c.bottom, 96, dpi);
        int field = 176, right = width - 22, fieldWidth = right - 118 - field;
        HDC dc = GetDC(window);
        wrappedIso = WrappedName(dc, font, isoName, S(fieldWidth - 24));
        isoHeight = TextHeight(dc, font, wrappedIso, fieldWidth - 24, 26) + 18;
        int dirHeight = std::min(34, TextHeight(dc, small, isoDirectory, right - field, 17));
        isoDivider = 18 + isoHeight + 5 + dirHeight + 18;
        diskTop = isoDivider + 19;
        diskHeight = 54;
        if (auto disk = Selection())
            diskHeight = DiskTextHeight(dc, *disk, fieldWidth - 46);
        int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < count; ++i) {
            int j = (int)SendMessageW(combo, CB_GETITEMDATA, i, 0);
            if (j >= 0 && j < (int)catalog.disks.size())
                SendMessageW(combo, CB_SETITEMHEIGHT, i,
                             S(DiskTextHeight(dc, catalog.disks[j], fieldWidth - 26)));
        }
        ReleaseDC(window, dc);
        diskDivider = diskTop + diskHeight + 8 + 18 + (diskNote.empty() ? 0 : 24) + 18;
        fsTop = diskDivider + 17;
        bool wrapFs = right - field < 615;
        // At narrow widths the NTFS option moves to its own line, as the published
        // WrapPanel did.
        int fsHeight = wrapFs ? 69 : 44;
        progressTop = fsTop + fsHeight + 17 + 15;
        logTop = progressTop + 104;
        // A small work area can be shorter than even the expanded text. Preserve
        // the fixed footer/log controls; the complete values remain in tooltips.
        if (logTop > height - 145) {
            int reduce = logTop - (height - 145);
            int isoReduce = std::min(reduce, std::max(0, isoHeight - 44));
            isoHeight -= isoReduce;
            isoDivider -= isoReduce;
            diskTop -= isoReduce;
            reduce -= isoReduce;
            int diskReduce = std::min(reduce, std::max(0, diskHeight - 54));
            diskHeight -= diskReduce;
            diskDivider -= isoReduce + diskReduce;
            fsTop -= isoReduce + diskReduce;
            progressTop -= isoReduce + diskReduce;
            logTop -= isoReduce + diskReduce;
        }
        auto mv = [&](HWND h, int x, int y, int w, int t) {
            MoveWindow(h, S(x), S(y), S(std::max(10, w)), S(std::max(1, t)), TRUE);
        };
        mv(isoButton, right - 106, 18, 106, 44);
        mv(combo, field, diskTop, fieldWidth, 360);
        SendMessageW(combo, CB_SETITEMHEIGHT, -1, S(diskHeight - 6));
        mv(refresh, right - 106, diskTop, 106, 44);
        mv(fat, field, fsTop + 5, 206, 23);
        mv(ntfs, wrapFs ? field : field + 212, fsTop + (wrapFs ? 30 : 5),
           right - field - (wrapFs ? 0 : 212), 23);
        mv(log, 36, logTop + 55, width - 72, std::max(20, height - logTop - 125));
        bool diagnostic = false;
#ifdef WIN11USB_READONLY
        diagnostic = !busy && !sample;
#endif
        int stopWidth = busy ? 104 : diagnostic ? 124 : 0;
        mv(start, right - 112 - stopWidth, height - 52, 112, 36);
        mv(stopButton, right - 96, height - 52, 96, 36);
        mv(exportButton, right - 112, height - 52, 112, 36);
        if (tooltip) {
            TOOLINFOW info{sizeof info};
            info.hwnd = window;
            info.uId = 1;
            info.uFlags = TTF_SUBCLASS;
            info.rect = R(field, 18, right - 118, 18 + isoHeight);
            info.lpszText = const_cast<wchar_t *>(iso.empty() ? L"支持 .iso 文件" : iso.c_str());
            SendMessageW(tooltip, tooltipAdded ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW, 0, (LPARAM)&info);
            SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&info);
            tooltipAdded = true;
            diskTooltip =
                Selection() ? Selection()->friendly + L"\r\n" + Selection()->Metadata() : diskNote;
            info.uId = 2;
            info.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            info.hwnd = window;
            info.uId = (UINT_PTR)combo;
            info.lpszText = const_cast<wchar_t *>(diskTooltip.c_str());
            SendMessageW(tooltip, diskTooltipAdded ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW, 0, (LPARAM)&info);
            diskTooltipAdded = true;
        }
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
#ifdef WIN11USB_READONLY
        ShowWindow(exportButton, busy || sample ? SW_HIDE : SW_SHOW);
#else
        ShowWindow(exportButton, SW_HIDE);
#endif
        ShowWindow(stopButton, busy ? SW_SHOW : SW_HIDE);
        SetWindowTextW(start, busy     ? L"制作中…"
                              : sample ? L"预览状态"
                                       :
#ifdef WIN11USB_READONLY
                                       L"只读检查"
#else
                                       L"开始制作"
#endif
        );
        Layout();
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
        std::wstring report = L"Win11Usb 0.2.0-preview.13 — 设备识别诊断\r\n";
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
            iso = L"D:\\Windows "
                  L"ISO\\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_"
                  L"74d48a06.iso";
            isoName = BaseName(iso);
            isoDirectory = Parent(iso);
            diskNote.clear();
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
                    L"这是界面预览，不会操作磁盘。部分查询失败的 USB "
                    L"也应显示在只读列表中。",
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
            details += L"\r\n信息来自最近一次刷新。可用“导出诊断”保存报告。\r\n.NET "
                       L"/ PowerShell / WMI：不需要。";
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
        activeRequest = request;
        if (!ShowProductDialog(window, StartDialog(request)))
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
                    h.log = [u](const std::wstring &s) { u->QueueLog(s); };
                    h.progress = [u](const Progress &v) { u->QueueProgress(v); };
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
        if (!ShowProductDialog(window, StopDialog(close)))
            return;
        cancel = true;
        FlushUpdates();
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
    void DrawDiskFace(HDC dc, RECT rect) {
        Fill(dc, rect, Bg);
        bool enabled = IsWindowEnabled(combo) != FALSE;
        Rounded(dc, rect, enabled ? RGB(247, 250, 255) : RGB(245, 247, 250),
                GetFocus() == combo ? RGB(77, 130, 223) : RGB(203, 215, 231), S(6));
        RECT text = rect;
        text.left += S(12);
        text.right -= S(34);
        text.top += S(8);
        if (auto disk = Selection()) {
            HDC measure = GetDC(window);
            int first =
                TextHeight(measure, bold, disk->friendly, MulDiv(text.right - text.left, 96, dpi), 19);
            ReleaseDC(window, measure);
            RECT name = text;
            name.bottom = name.top + S(first);
            Text(dc, bold, disk->friendly, name, enabled ? Ink : Muted,
                 DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text.top = name.bottom + S(3);
            text.bottom = rect.bottom - S(7);
            Text(dc, small, disk->Metadata(), text, RGB(82, 103, 130),
                 DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else {
            text = rect;
            text.left += S(12);
            text.right -= S(34);
            Text(dc, font, scanning ? L"正在读取 USB 设备…" : L"连接 U 盘后选择设备", text,
                 RGB(118, 133, 154));
        }
        DrawProductIcon(
            dc, ProductIcon::Chevron,
            {rect.right - S(28), (rect.bottom - S(16)) / 2, rect.right - S(12), (rect.bottom + S(16)) / 2},
            RGB(118, 133, 154));
    }
    void DrawRadio(HDC dc, HWND control, RECT rect) {
        Fill(dc, rect, Bg);
        bool enabled = IsWindowEnabled(control) != FALSE;
        bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        int cy = (rect.top + rect.bottom) / 2, size = S(12), left = S(1);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto color = [&](COLORREF c) {
            return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
        };
        Gdiplus::Pen pen(color(enabled && checked ? Blue : RGB(139, 150, 166)), 1.0f);
        Gdiplus::SolidBrush white(color(Bg));
        g.FillEllipse(&white, (float)left, (float)(cy - size / 2), (float)size, (float)size);
        g.DrawEllipse(&pen, (float)left + .5f, (float)(cy - size / 2) + .5f, (float)size - 1,
                      (float)size - 1);
        if (checked) {
            Gdiplus::SolidBrush dot(color(enabled ? Blue : RGB(150, 161, 179)));
            g.FillEllipse(&dot, (float)(left + S(3)), (float)(cy - size / 2 + S(3)), (float)(size - S(6)),
                          (float)(size - S(6)));
        }
        wchar_t text[160]{};
        GetWindowTextW(control, text, 160);
        rect.left = S(20);
        Text(dc, font, text, rect, enabled ? Ink : RGB(150, 157, 169));
        if (GetFocus() == control) {
            InflateRect(&rect, -1, -2);
            DrawFocusRect(dc, &rect);
        }
    }
    static LRESULT CALLBACK SurfaceProc(HWND control, UINT message, WPARAM a, LPARAM b, UINT_PTR,
                                        DWORD_PTR data) {
        auto u = (Ui *)data;
        int id = GetDlgCtrlID(control);
        if (message == WM_PAINT && (id == ID_DISK || id == ID_FAT || id == ID_NTFS)) {
            PAINTSTRUCT ps;
            HDC dest = BeginPaint(control, &ps);
            RECT rect;
            GetClientRect(control, &rect);
            HDC dc = CreateCompatibleDC(dest);
            auto bitmap = CreateCompatibleBitmap(dest, rect.right, rect.bottom);
            auto previous = SelectObject(dc, bitmap);
            if (id == ID_DISK)
                u->DrawDiskFace(dc, rect);
            else
                u->DrawRadio(dc, control, rect);
            BitBlt(dest, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            EndPaint(control, &ps);
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (id == ID_DISK && message == WM_NCPAINT)
            return 0;
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
            message == BM_SETCHECK) {
            auto result = DefSubclassProc(control, message, a, b);
            InvalidateRect(control, nullptr, FALSE);
            return result;
        }
        if (message == WM_MOUSEMOVE && id != ID_DISK) {
            if (u->hotButton != control) {
                u->hotButton = control;
                TRACKMOUSEEVENT tracking{sizeof tracking, TME_LEAVE, control, 0};
                TrackMouseEvent(&tracking);
                InvalidateRect(control, nullptr, FALSE);
            }
        }
        if (message == WM_MOUSELEAVE && u->hotButton == control) {
            u->hotButton = nullptr;
            InvalidateRect(control, nullptr, FALSE);
        }
        if (message == WM_SETCURSOR && IsWindowEnabled(control)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefSubclassProc(control, message, a, b);
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
        Fill(dc, R(22, isoDivider, right, isoDivider + 1), Line);
        Fill(dc, R(22, diskDivider, right, diskDivider + 1), Line);
        Fill(dc, R(22, progressTop - 15, right, progressTop - 14), Line);
        Rounded(dc, R(22, 21, 72, 71), RGB(232, 240, 254), RGB(232, 240, 254), S(11));
        Rounded(dc, R(22, diskTop, 72, diskTop + 50), RGB(232, 240, 254), RGB(232, 240, 254), S(11));
        Rounded(dc, R(22, fsTop, 66, fsTop + 44), RGB(237, 244, 255), RGB(237, 244, 255), S(10));
        DrawProductIcon(dc, ProductIcon::Iso, R(32, 30, 61, 61), Blue);
        DrawProductIcon(dc, ProductIcon::Usb, R(35, diskTop + 13, 59, diskTop + 37), Blue, -45);
        DrawProductIcon(dc, ProductIcon::Settings, R(30, fsTop + 8, 57, fsTop + 35), Blue);
        Text(dc, labelFont, L"安装镜像", R(88, 18, 168, 18 + isoHeight + 22), Ink);
        Text(dc, labelFont, L"写入U盘", R(88, diskTop, 168, diskTop + diskHeight), Ink);
        Text(dc, labelFont, L"文件系统", R(88, fsTop + 6, 168, fsTop + 30), Ink);
        Rounded(dc, R(field, 18, right - 118, 18 + isoHeight), RGB(248, 250, 253), RGB(203, 215, 231),
                S(6));
        Text(dc, font, wrappedIso, R(field + 12, 27, right - 130, 18 + isoHeight - 7),
             iso.empty() ? RGB(118, 133, 154) : Ink, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        Text(dc, small, isoDirectory, R(field, 18 + isoHeight + 5, right, isoDivider - 12), Muted,
             DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        auto disk = Selection();
        auto warning = disk ? L"制作将清除磁盘 " + std::to_wstring(disk->number) + L" 的全部分区和文件。"
                            : L"制作将清除所选 U 盘的全部分区和文件。";
        int warningY = diskTop + diskHeight + 8;
        DrawProductIcon(dc, ProductIcon::Warning, R(field, warningY + 1, field + 15, warningY + 16),
                        RGB(217, 119, 6));
        Text(dc, small, warning, R(field + 21, warningY, right, warningY + 18), RGB(156, 87, 13));
        if (!diskNote.empty())
            Text(dc, small, diskNote, R(field, warningY + 24, right, diskDivider - 12), Muted,
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        bool wrapFs = right - field < 615;
        Text(dc, small, L"install.wim 超过 4 GiB 时按相同规则拆分",
             R(field, fsTop + (wrapFs ? 56 : 31), right, fsTop + (wrapFs ? 76 : 51)), Muted);
        DrawProductIcon(dc, ProductIcon::Stage, R(22, progressTop, 44, progressTop + 22), Blue);
        Text(dc, titleFont, phase, R(54, progressTop, right - 180, progressTop + 24), Ink,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Text(dc, small,
             busy && progress.stage == 4   ? L"复制整体"
             : busy && progress.stage == 5 ? L"校验整体"
             : finished                    ? L"制作结果"
                                           : L"当前操作",
             R(right - 178, progressTop, right - 65, progressTop + 24), RGB(82, 103, 130),
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Text(dc, numberFont, finished ? L"已完成" : percentText,
             R(right - 70, progressTop, right, progressTop + 24), Blue,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t *stages[] = {L"检查镜像", L"格式化", L"处理映像", L"复制文件", L"校验", L"清理"};
        // Widths follow the original equal star columns plus the final automatic
        // label.
        int step = (right - 22 - 40) / 5;
        for (int i = 0; i < 6; ++i) {
            int x = 22 + i * step, y = progressTop + 30;
            bool done = finished || progress.stage > i + 1, active = progress.stage == i + 1;
            auto icon = done     ? ProductIcon::StepDone
                        : active ? ProductIcon::StepCurrent
                                 : ProductIcon::StepPending;
            DrawProductIcon(dc, icon, R(x, y + 5, x + 10, y + 15),
                            done || active ? RGB(40, 120, 240) : RGB(220, 228, 239));
            Text(dc, stageFont, stages[i], R(x + 15, y, std::min(x + 82, right), y + 20),
                 active ? Blue : Muted);
            if (i < 5) {
                HDC measure = GetDC(window);
                auto f = SelectObject(measure, stageFont);
                SIZE extent{};
                GetTextExtentPoint32W(measure, stages[i], (int)wcslen(stages[i]), &extent);
                SelectObject(measure, f);
                ReleaseDC(window, measure);
                int linkStart = x + 15 + MulDiv(extent.cx, 96, dpi) + 8;
                Fill(dc, R(linkStart, y + 10, x + step - 8, y + 11), done ? RGB(155, 186, 237) : Line);
            }
        }
        int barY = progressTop + 55;
        Fill(dc, R(22, barY, right, barY + 4), RGB(226, 233, 244));
        double value = finished ? 100 : std::clamp(progress.overall, 0.0, 100.0);
        Fill(dc, R(22, barY, 22 + (int)((right - 22) * value / 100), barY + 4), RGB(40, 120, 240));
        int fileY = progressTop + 67;
        Text(dc, small, fileText, R(22, fileY, right - 100, fileY + 18), RGB(82, 103, 130),
             DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (progress.elapsed) {
            wchar_t elapsed[40];
            swprintf(elapsed, 40, L"用时 %02llu:%02llu", progress.elapsed / 60, progress.elapsed % 60);
            Text(dc, small, elapsed, R(right - 100, fileY, right, fileY + 18), Muted,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        Fill(dc, R(22, progressTop + 90, right, progressTop + 92), RGB(229, 235, 245));
        if (progress.current > 0)
            Fill(dc,
                 R(22, progressTop + 90, 22 + (int)((right - 22) * std::min(100.0, progress.current) / 100),
                   progressTop + 92),
                 RGB(123, 167, 255));
        Rounded(dc, R(22, logTop, right, height - 66), LogBg, LogBg, S(8));
        DrawProductIcon(dc, ProductIcon::Log, R(36, logTop + 12, 57, logTop + 33), RGB(127, 178, 255));
        Text(dc, bold, L"制作日志", R(65, logTop + 12, right - 100, logTop + 33), RGB(231, 239, 250));
        Text(dc, small,
             busy       ? L"制作中"
             : finished ? L"已结束"
                        : L"等待开始",
             R(right - 90, logTop + 12, right - 14, logTop + 33), RGB(175, 192, 216),
             DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        Fill(dc, R(36, logTop + 43, right - 14, logTop + 44), RGB(48, 64, 85));
        int buttons = busy ? 224 : 112;
#ifdef WIN11USB_READONLY
        if (!busy && !sample)
            buttons = 236;
#endif
        DrawProductIcon(dc, ProductIcon::Info, R(22, height - 42, 37, height - 27), RGB(118, 139, 170));
        Text(dc, small, footer, R(44, height - 55, right - buttons - 12, height - 13), Muted,
             DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        BitBlt(dest, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &ps);
    }
    void DrawButton(const DRAWITEMSTRUCT &d) {
        RECT r = d.rcItem;
        Fill(d.hDC, r, Bg);
        bool primary = d.CtlID == ID_START, stop = d.CtlID == ID_STOP, refreshing = d.CtlID == ID_REFRESH;
        bool disabled = (d.itemState & ODS_DISABLED) != 0, hot = hotButton == d.hwndItem;
        COLORREF bg = primary      ? (disabled ? RGB(157, 185, 234)
                                      : hot    ? RGB(58, 114, 239)
                                               : RGB(37, 99, 235))
                      : stop       ? RGB(255, 255, 255)
                      : refreshing ? Bg
                      : hot        ? RGB(242, 247, 255)
                                   : RGB(237, 243, 253);
        COLORREF border = primary ? bg : stop ? RGB(231, 141, 137) : refreshing ? Bg : RGB(215, 227, 244);
        if (d.itemState & ODS_FOCUS)
            border = Blue;
        Rounded(d.hDC, r, bg, border, S(6));
        wchar_t text[160]{};
        GetWindowTextW(d.hwndItem, text, 160);
        COLORREF ink = primary    ? RGB(255, 255, 255)
                       : disabled ? RGB(143, 158, 180)
                       : stop     ? RGB(191, 52, 48)
                                  : RGB(36, 86, 165);
        if (primary || refreshing) {
            auto previous = SelectObject(d.hDC, bold);
            SIZE extent{};
            GetTextExtentPoint32W(d.hDC, text, (int)wcslen(text), &extent);
            SelectObject(d.hDC, previous);
            int icon = S(primary ? 15 : 21), gap = S(7), left = (r.right - extent.cx - icon - gap) / 2,
                top = (r.bottom - icon) / 2;
            DrawProductIcon(d.hDC, primary ? ProductIcon::Play : ProductIcon::Refresh,
                            {left, top, left + icon, top + icon}, ink);
            r.left = left + icon + gap;
            Text(d.hDC, bold, text, r, ink);
        } else
            Text(d.hDC, bold, text, r, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d.itemState & ODS_FOCUS) {
            RECT focus = d.rcItem;
            InflateRect(&focus, -S(3), -S(3));
            DrawFocusRect(d.hDC, &focus);
        }
    }
    LRESULT Message(UINT m, WPARAM a, LPARAM b) {
        switch (m) {
        case WM_CREATE: {
            HMENU systemMenu = GetSystemMenu(window, FALSE);
            AppendMenuW(systemMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(systemMenu, MF_STRING, 0x1ff0, L"关于 Win11Usb / 许可证");
            AppendMenuW(systemMenu, MF_STRING, 0x1fe0, L"导出设备诊断…");
            bgBrush = CreateSolidBrush(Bg);
            logBrush = CreateSolidBrush(LogBg);
            isoButton = Control(L"BUTTON", L"选择 ISO…", WS_TABSTOP | BS_OWNERDRAW, ID_ISO);
            refresh = Control(L"BUTTON", L"刷新", WS_TABSTOP | BS_OWNERDRAW, ID_REFRESH);
            combo =
                Control(WC_COMBOBOXW, L"",
                        WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS,
                        ID_DISK);
            fat = Control(L"BUTTON", L"FAT32（UEFI 兼容性更广）",
                          WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ID_FAT);
            ntfs = Control(L"BUTTON", L"NTFS（部分 UEFI 固件可能不支持启动）",
                           WS_TABSTOP | BS_AUTORADIOBUTTON, ID_NTFS);
            SendMessageW(fat, BM_SETCHECK, BST_CHECKED, 0);
            start = Control(L"BUTTON", L"开始制作", WS_TABSTOP | WS_GROUP | BS_OWNERDRAW, ID_START);
            stopButton = Control(L"BUTTON", L"强制结束", WS_TABSTOP | BS_OWNERDRAW, ID_STOP);
            exportButton = Control(L"BUTTON", L"导出诊断", WS_TABSTOP | BS_OWNERDRAW, ID_EXPORT);
            log = Control(L"EDIT", L"",
                          WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, ID_LOG);
            SendMessageW(log, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
            for (HWND control : {isoButton, refresh, combo, fat, ntfs, start, stopButton, exportButton})
                SetWindowSubclass(control, SurfaceProc, 1, (DWORD_PTR)this);
            tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window, nullptr,
                                      instance, nullptr);
            SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, S(600));
            Fonts();
#ifdef WIN11USB_READONLY
            Log(L"只读验证版：不挂载 ISO，不清盘，不格式化，不复制。");
            footer = L"只读版本：仅检查文件和 USB 整盘身份。";
#else
            footer = L"清盘前会再次核对磁盘并单独确认。";
            Log(L"等待开始制作。操作日志将在这里实时显示。");
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
            MONITORINFO monitor{sizeof monitor};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            p->ptMinTrackSize = {S(720),
                                 std::min(S(650), (int)(monitor.rcWork.bottom - monitor.rcWork.top))};
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
                Fill(d->hDC, d->rcItem, RGB(255, 255, 255));
                if (d->itemState & ODS_SELECTED)
                    Rounded(d->hDC, d->rcItem, RGB(232, 240, 254), RGB(232, 240, 254), S(4));
                if (d->itemID != (UINT)-1) {
                    int j = (int)d->itemData;
                    if (j >= 0 && j < (int)catalog.disks.size()) {
                        auto disk = catalog.disks[j];
                        RECT r = d->rcItem;
                        r.left += S(10);
                        r.right -= S(10);
                        r.top += S(8);
                        r.bottom = r.top + S(TextHeight(d->hDC, bold, disk.friendly,
                                                        MulDiv(r.right - r.left, 96, dpi), 19));
                        Text(d->hDC, bold, disk.friendly, r, Ink, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                        r.top = r.bottom + S(3);
                        r.bottom = d->rcItem.bottom - S(7);
                        Text(d->hDC, small, disk.Metadata(), r, Muted,
                             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
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
                if (HIWORD(a) != CBN_SELCHANGE && HIWORD(a) != CBN_CLOSEUP)
                    break;
                if (auto selected = Selection())
                    diskNote = selected->eligible() ? L"" : selected->blocked;
                State();
                break;
            case ID_EXPORT:
                ExportDiagnostics();
                break;
            case ID_FAT:
            case ID_NTFS:
                InvalidateRect(fat, nullptr, FALSE);
                InvalidateRect(ntfs, nullptr, FALSE);
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
            if (a == UpdateTimer)
                FlushUpdates();
            return 0;
        case WM_CATALOG: {
            std::unique_ptr<Catalog> c((Catalog *)b);
            ApplyCatalog(std::move(*c));
            return 0;
        }
        case WM_UI_UPDATE: {
            if (!SetTimer(window, UpdateTimer, UpdateInterval, nullptr))
                FlushUpdates();
            return 0;
        }
        case WM_WORK_PATH: {
            std::unique_ptr<std::wstring> path((std::wstring *)b);
            FlushUpdates();
            sessionFile.reset(CreateFileW((*path + L"\\session.log").c_str(), GENERIC_WRITE,
                                          FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                          nullptr));
            if (!sessionFile.valid()) {
                Log(L"无法创建持续日志文件：" + WinError());
                return 0;
            }
            auto bytes = Utf8(sessionLog);
            DWORD n = 0;
            WriteFile(sessionFile, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
            footer = L"日志目录：" + *path;
            auto area = R(22, height - 52, width - 272, height - 11);
            InvalidateRect(window, &area, FALSE);
            return 0;
        }
        case WM_PREVIEW_DIALOG: {
            ProductDialog preview;
            preview.title = L"最后确认：整盘格式化 · 界面预览";
            preview.heading = L"整盘格式化";
            preview.message = L"这是界面预览，不会修改磁盘。正式制作会删除所选 U "
                              L"盘上的全部分区和文件。";
            preview.details = L"安装系统：Windows 11 教育版 26H2\r\n卷标：WIN11_26H2\r\n\r\n"
                              L"磁盘 3 | Kingston DataTraveler 3.0 | 57.8 GiB\r\n"
                              L"当前 2 个分区，包括 EFI、隐藏分区和 PE 分区。\r\n重建为一个全容量 "
                              L"FAT32 主分区。\r\n\r\n"
                              L"ISO：D:\\Windows "
                              L"ISO\\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_"
                              L"74d48a06.iso";
            preview.primary = L"确认格式化";
            preview.secondary = L"取消";
            preview.format = true;
            bool answer = ShowProductDialog(window, preview);
            Log(answer ? L"弹窗预览：已选择确认（未执行写盘）。" : L"弹窗预览：已取消（未执行写盘）。");
            return 0;
        }
        case WM_PROMPT_VALUE: {
            std::unique_ptr<std::shared_ptr<PromptMessage>> holder((std::shared_ptr<PromptMessage> *)b);
            auto q = *holder;
            struct ReplyOnExit {
                PromptMessage &request;
                ~ReplyOnExit() { SetEvent(request.ready); }
            } reply{*q};
            // Dialog creation failure must release the worker as an unapproved prompt.
            FlushUpdates();
            if (!cancel)
                q->answer = ShowProductDialog(window, WorkflowDialog(q->kind, q->detail, activeRequest));
            return 0;
        }
        case WM_FINISHED: {
            std::unique_ptr<Result> r((Result *)b);
            FlushUpdates();
            busy = false;
            finished = r->copied;
            phase = r->copied ? L"制作完成" : r->cancelled ? L"制作已取消" : L"制作未完成";
            if (r->copied && !r->ejectWarning.empty())
                phase = L"制作完成 · U 盘未弹出";
            if (r->copied) {
                progress.stage = 6;
                progress.overall = 100;
                percentText = L"已完成";
            }
            fileText = r->copied ? L"文件大小和内容校验通过" : L"日志保留在窗口中。";
            if (r->copied && !r->ejectWarning.empty())
                fileText += L"；U 盘尚未安全弹出";
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
            if (!r->error.empty() || !r->ejectWarning.empty() || r->copied)
                ShowProductDialog(window, ResultDialog(activeRequest, *r));
            return 0;
        }
        case WM_CLOSE:
            Stop(true);
            return 0;
        case WM_SYSCOMMAND:
            if ((a & 0xfff0) == 0x1fe0) {
                if (!busy && !scanning)
                    ExportDiagnostics();
                return 0;
            }
            if ((a & 0xfff0) == 0x1ff0) {
                std::wstring detail;
                for (int id : {302, 301, 303, 304, 305}) {
                    auto b = ResourceBytes(id);
                    detail += Wide(std::string(b.begin(), b.end())) + L"\r\n\r\n";
                }
                Ask(window, L"关于 Win11Usb",
                    L"C++ / Win32 原生验证版 · 无需 .NET、PowerShell 或 WMI。\r\nFAT32 "
                    L"工具的 GPL "
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
    ProductGraphicsSession graphics;
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
    RECT r{0, 0, u->S(DefaultWidth), u->S(DefaultHeight)};
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = std::min(r.right - r.left, wa.right - wa.left),
        h = std::min(r.bottom - r.top, wa.bottom - wa.top);
    HWND window = CreateWindowExW(0, wc.lpszClassName,
#ifdef WIN11USB_READONLY
                                  preview ? L"Windows 安装 U 盘制作工具 · 界面预览（只读）"
                                          : L"Windows 安装 U 盘制作工具 · 原生只读验证版",
#else
                                  L"Windows 安装 U 盘制作工具",
#endif
                                  MainWindowStyle, (wa.right - w) / 2, (wa.bottom - h) / 2, w, h, nullptr,
                                  nullptr, instance, u);
    if (!window)
        throw Error(L"原生窗口创建失败：" + WinError());
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (DispatchMainKey(window, msg))
            continue;
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
