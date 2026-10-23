#include "dialog.hpp"
#include "win11usb.hpp"
#include <algorithm>
#include <commctrl.h>
#include <gdiplus.h>

namespace wu {
namespace {
constexpr COLORREF Background = RGB(243, 246, 251), Ink = RGB(24, 38, 61), BodyInk = RGB(52, 68, 93),
                   Muted = RGB(113, 128, 152), Border = RGB(223, 230, 240), Blue = RGB(40, 120, 232),
                   Red = RGB(200, 59, 53), Amber = RGB(217, 119, 6);
constexpr DWORD DialogStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
constexpr DWORD DialogExStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
constexpr wchar_t ClassName[] = L"Win11Usb.Native.Confirm";
constexpr UINT_PTR ButtonSubclass = 10;

struct GraphicsSession {
    ULONG_PTR token = 0;
    GraphicsSession() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            throw Error(L"无法初始化弹窗绘图组件。");
    }
    ~GraphicsSession() { Gdiplus::GdiplusShutdown(token); }
};

Gdiplus::Color Color(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }
void Fill(HDC dc, const RECT &r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}
void Round(HDC dc, const RECT &r, float radius, COLORREF background, COLORREF border,
           float borderWidth = 1.0f) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float l = (float)r.left + borderWidth / 2, t = (float)r.top + borderWidth / 2;
    float w = (float)(r.right - r.left) - borderWidth, h = (float)(r.bottom - r.top) - borderWidth;
    if (w <= 0 || h <= 0)
        return;
    float d = std::min(radius * 2, std::min(w, h));
    Gdiplus::GraphicsPath p;
    p.AddArc(l, t, d, d, 180, 90);
    p.AddArc(l + w - d, t, d, d, 270, 90);
    p.AddArc(l + w - d, t + h - d, d, d, 0, 90);
    p.AddArc(l, t + h - d, d, d, 90, 90);
    p.CloseFigure();
    Gdiplus::SolidBrush brush(Color(background));
    g.FillPath(&brush, &p);
    if (borderWidth > 0) {
        Gdiplus::Pen pen(Color(border), borderWidth);
        g.DrawPath(&pen, &p);
    }
}
void Text(HDC dc, HFONT font, const std::wstring &value, RECT r, COLORREF color,
          UINT flags = DT_LEFT | DT_WORDBREAK | DT_NOPREFIX) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), (int)value.size(), &r, flags);
    SelectObject(dc, previous);
}
int Measure(HDC dc, HFONT font, const std::wstring &value, int width) {
    if (value.empty())
        return 0;
    HGDIOBJ previous = SelectObject(dc, font);
    RECT r{0, 0, std::max(width, 1), 0};
    DrawTextW(dc, value.c_str(), (int)value.size(), &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, previous);
    return r.bottom;
}
std::wstring CrLf(const std::wstring &value) {
    std::wstring result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == L'\n' && (i == 0 || value[i - 1] != L'\r'))
            result += L'\r';
        result += value[i];
    }
    return result;
}
struct State {
    ProductDialog options;
    HWND window = nullptr, body = nullptr, yes = nullptr, no = nullptr;
    HFONT font = nullptr, heading = nullptr, label = nullptr, button = nullptr, note = nullptr;
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    int dpi = 96;
    bool answer = false, done = false, hoverYes = false, hoverNo = false;
    RECT headingRect{}, messageRect{}, cardRect{}, hintRect{}, compatibilityRect{};
    int badgeY = 20, bodyTop = 0;
    explicit State(const ProductDialog &p) : options(p) {
        if (options.format) {
            options.kind = DialogKind::Warning;
            options.defaultPrimary = false;
            if (options.secondary.empty())
                options.secondary = L"取消";
            if (options.hint.empty())
                options.hint = L"默认取消 · 请核对磁盘编号、名称和容量";
        }
        if (options.secondary.empty())
            options.defaultPrimary = true;
    }
    ~State() {
        for (auto f : {font, heading, label, button, note})
            if (f)
                DeleteObject(f);
        DeleteObject(white);
    }
    int S(int n) const { return MulDiv(n, dpi, 96); }
    COLORREF Accent() const {
        return options.format || options.kind == DialogKind::Error ? Red
               : options.kind == DialogKind::Warning               ? Amber
                                                                   : Blue;
    }
    void Fonts() {
        auto make = [&](int size, int weight) {
            return CreateFontW(-S(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               L"Microsoft YaHei UI");
        };
        HFONT oldFont = font, oldHeading = heading, oldLabel = label, oldButton = button, oldNote = note;
        font = make(13, FW_NORMAL);
        heading = make(options.format ? 19 : 18, FW_BOLD);
        label = make(11, FW_SEMIBOLD);
        button = make(13, FW_SEMIBOLD);
        note = make(12, FW_NORMAL);
        for (HWND child : {body, yes, no})
            if (child)
                SendMessageW(child, WM_SETFONT, (WPARAM)(child == body ? font : button), FALSE);
        for (auto f : {oldFont, oldHeading, oldLabel, oldButton, oldNote})
            if (f)
                DeleteObject(f);
    }
    int ButtonWidth(HDC dc, const std::wstring &caption, bool primary) const {
        auto previous = SelectObject(dc, button);
        SIZE size{};
        GetTextExtentPoint32W(dc, caption.c_str(), (int)caption.size(), &size);
        SelectObject(dc, previous);
        return std::max<int>(size.cx + S(34), S(options.format ? (primary ? 128 : 92) : 76));
    }
    int TopLayout(HDC dc, int width) {
        int left = S(20), right = width - left;
        headingRect = {S(options.format ? 64 : 70), S(36), right, 0};
        headingRect.bottom = headingRect.top + std::max(S(26), Measure(dc, heading, options.heading,
                                                                       right - headingRect.left));
        int top = std::max<int>(S(58), headingRect.bottom) + S(13);
        int messageHeight = Measure(dc, font, options.message, right - left - S(options.format ? 20 : 0));
        if (options.format)
            messageHeight = std::max(S(18), messageHeight) + S(20);
        messageRect = {left, top, right, top + messageHeight};
        top = messageRect.bottom;
        if (messageHeight)
            top += S(12);
        compatibilityRect = {};
        if (options.format && options.ntfs) {
            const std::wstring compatibility =
                L"NTFS：部分 UEFI 固件可能不支持启动；不确定时请取消并改选 FAT32。";
            int height = Measure(dc, note, compatibility, right - left);
            compatibilityRect = {left, top - S(3), right, top - S(3) + height};
            top = compatibilityRect.bottom + S(14);
        }
        bodyTop = top;
        return top;
    }
    int FooterHeight(HDC dc, int width, int yesWidth, int noWidth) const {
        int remaining = width - S(40) - yesWidth - (options.secondary.empty() ? 0 : noWidth + S(8));
        if (options.hint.empty())
            return S(38);
        if (remaining < S(options.format ? 245 : 130) ||
            Measure(dc, note, options.hint, std::max(1, remaining - S(12))) > S(38))
            return S(38 + 12) + std::max(S(18), Measure(dc, note, options.hint, width - S(40)));
        return S(38);
    }
    int PreferredHeight(HDC dc, int width) {
        int yesWidth = ButtonWidth(dc, options.primary, true),
            noWidth = ButtonWidth(dc, options.secondary, false);
        int top = TopLayout(dc, width);
        int details =
            options.details.empty()
                ? 0
                : std::clamp(Measure(dc, font, options.details, width - S(68)), S(56), S(190)) + S(26);
        return top + details + (details ? S(13) : 0) + FooterHeight(dc, width, yesWidth, noWidth) + S(20);
    }
    void Layout() {
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = GetDC(window);
        int yesWidth = ButtonWidth(dc, options.primary, true),
            noWidth = ButtonWidth(dc, options.secondary, false);
        int top = TopLayout(dc, client.right);
        int footerHeight = FooterHeight(dc, client.right, yesWidth, noWidth);
        int footerTop = client.bottom - S(20) - footerHeight;
        int buttonY = client.bottom - S(20 + 38);
        int yesX = client.right - S(20) - yesWidth;
        int noX = yesX - S(8) - noWidth;
        SetWindowPos(yes, nullptr, yesX, buttonY, yesWidth, S(38), SWP_NOZORDER | SWP_NOACTIVATE);
        if (!options.secondary.empty())
            SetWindowPos(no, nullptr, noX, buttonY, noWidth, S(38), SWP_NOZORDER | SWP_NOACTIVATE);
        int actionLeft = options.secondary.empty() ? yesX : noX;
        if (footerHeight > S(38))
            hintRect = {S(20), footerTop, client.right - S(20), buttonY - S(10)};
        else
            hintRect = {S(20), footerTop, actionLeft - S(12), footerTop + footerHeight};
        cardRect =
            options.details.empty() ? RECT{} : RECT{S(20), top, client.right - S(20), footerTop - S(13)};
        if (!options.details.empty()) {
            // A native, borderless read-only EDIT preserves selection, Ctrl+C,
            // wheel scrolling and screen-reader text without a managed control.
            int pad = S(options.format ? 14 : 12);
            SetWindowPos(body, nullptr, cardRect.left + pad, cardRect.top + pad,
                         std::max<int>(S(40), cardRect.right - cardRect.left - pad * 2),
                         std::max<int>(S(22), cardRect.bottom - cardRect.top - pad * 2),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ReleaseDC(window, dc);
        InvalidateRect(window, nullptr, FALSE);
    }
};

void Badge(HDC dc, const State &s) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float diameter = (float)s.S(s.options.format ? 34 : 38);
    float x = (float)s.S(20), y = (float)s.S(s.badgeY);
    COLORREF background = s.options.format || s.options.kind == DialogKind::Error ? RGB(253, 232, 231)
                          : s.options.kind == DialogKind::Warning                 ? RGB(255, 242, 217)
                                                                                  : RGB(230, 240, 255);
    Gdiplus::SolidBrush circle(Color(background)), ink(Color(s.Accent()));
    g.FillEllipse(&circle, x, y, diameter, diameter);
    float unit = diameter / 38.0f, cx = x + diameter / 2, cy = y + diameter / 2;
    if (s.options.kind == DialogKind::Error) {
        Gdiplus::Pen pen(Color(s.Accent()), 2.7f * unit);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&pen, cx - 5 * unit, cy - 5 * unit, cx + 5 * unit, cy + 5 * unit);
        g.DrawLine(&pen, cx + 5 * unit, cy - 5 * unit, cx - 5 * unit, cy + 5 * unit);
    } else if (s.options.format || s.options.kind == DialogKind::Warning) {
        g.FillRectangle(&ink, cx - 1.5f * unit, cy - 9 * unit, 3 * unit, 12 * unit);
        g.FillEllipse(&ink, cx - 1.8f * unit, cy + 6 * unit, 3.6f * unit, 3.6f * unit);
    } else {
        g.FillEllipse(&ink, cx - 1.7f * unit, cy - 9 * unit, 3.4f * unit, 3.4f * unit);
        g.FillRectangle(&ink, cx - 1.5f * unit, cy - 2 * unit, 3 * unit, 11 * unit);
        g.FillRectangle(&ink, cx - 3.5f * unit, cy - 2 * unit, 5 * unit, 2 * unit);
        g.FillRectangle(&ink, cx - 3.5f * unit, cy + 7 * unit, 7 * unit, 2 * unit);
    }
}
LRESULT CALLBACK ButtonProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR id, DWORD_PTR data) {
    auto &s = *(State *)data;
    bool &hover = GetDlgCtrlID(w) == IDOK ? s.hoverYes : s.hoverNo;
    if (m == WM_MOUSEMOVE && !hover) {
        hover = true;
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0};
        TrackMouseEvent(&track);
        InvalidateRect(w, nullptr, FALSE);
    } else if (m == WM_MOUSELEAVE) {
        hover = false;
        InvalidateRect(w, nullptr, FALSE);
    } else if (m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_UPDATEUISTATE)
        InvalidateRect(w, nullptr, FALSE);
    else if (m == WM_ERASEBKGND)
        return 1;
    else if (m == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    } else if (m == WM_NCDESTROY)
        RemoveWindowSubclass(w, ButtonProc, id);
    return DefSubclassProc(w, m, a, b);
}
void DrawButton(const State &s, const DRAWITEMSTRUCT &item) {
    bool primary = item.CtlID == IDOK;
    COLORREF fill = primary ? s.Accent() : RGB(231, 237, 246);
    if ((primary ? s.hoverYes : s.hoverNo) || (item.itemState & ODS_SELECTED)) {
        int alpha = (item.itemState & ODS_SELECTED) ? 210 : 225;
        fill = RGB((GetRValue(fill) * alpha + 255 * (255 - alpha)) / 255,
                   (GetGValue(fill) * alpha + 255 * (255 - alpha)) / 255,
                   (GetBValue(fill) * alpha + 255 * (255 - alpha)) / 255);
    }
    bool focus = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
    Fill(item.hDC, item.rcItem, Background);
    Round(item.hDC, item.rcItem, (float)s.S(7), fill, focus ? RGB(53, 105, 173) : fill,
          focus ? (float)s.S(2) : 0.0f);
    Text(item.hDC, s.button, primary ? s.options.primary : s.options.secondary, item.rcItem,
         primary ? RGB(255, 255, 255) : RGB(38, 53, 77),
         DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}
void Paint(State &s, HDC dc, const RECT &client) {
    Fill(dc, client, Background);
    Badge(dc, s);
    std::wstring kind = s.options.format                        ? L"最后确认"
                        : s.options.kind == DialogKind::Warning ? L"需要确认"
                        : s.options.kind == DialogKind::Error   ? L"出现问题"
                                                                : L"提示信息";
    Text(dc, s.label, kind, {s.headingRect.left, s.S(20), client.right - s.S(20), s.S(35)}, Muted,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    Text(dc, s.heading, s.options.heading, s.headingRect, Ink);
    if (!s.options.message.empty() || s.options.format) {
        RECT text = s.messageRect;
        if (s.options.format) {
            Round(dc, s.messageRect, (float)s.S(7), RGB(255, 244, 242), RGB(244, 211, 208), (float)s.S(1));
            InflateRect(&text, -s.S(10), -s.S(10));
        }
        Text(dc, s.font, s.options.message, text, s.options.format ? RGB(162, 58, 50) : BodyInk);
    }
    if (s.options.format && s.options.ntfs)
        Text(dc, s.note, L"NTFS：部分 UEFI 固件可能不支持启动；不确定时请取消并改选 FAT32。",
             s.compatibilityRect, RGB(156, 87, 13));
    if (!s.options.details.empty())
        Round(dc, s.cardRect, (float)s.S(8), RGB(255, 255, 255), Border, (float)s.S(1));
    Text(dc, s.note, s.options.hint, s.hintRect, Muted);
}
LRESULT CALLBACK DialogProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    State *s = (State *)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        s = (State *)((CREATESTRUCTW *)b)->lpCreateParams;
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)s);
        s->window = w;
    }
    if (!s)
        return DefWindowProcW(w, m, a, b);
    switch (m) {
    case WM_CREATE: {
        s->body = CreateWindowExW(0, L"EDIT", CrLf(s->options.details).c_str(),
                                  WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                      WS_VSCROLL | (s->options.details.empty() ? 0 : WS_VISIBLE),
                                  0, 0, 0, 0, w, (HMENU)3, nullptr, nullptr);
        s->no = CreateWindowExW(0, L"BUTTON", s->options.secondary.c_str(),
                                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW |
                                    (s->options.secondary.empty() ? 0 : WS_VISIBLE),
                                0, 0, 0, 0, w, (HMENU)IDCANCEL, nullptr, nullptr);
        s->yes = CreateWindowExW(0, L"BUTTON", s->options.primary.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, w,
                                 (HMENU)IDOK, nullptr, nullptr);
        if (!s->body || !s->yes || !s->no)
            return -1;
        for (HWND child : {s->body, s->yes, s->no})
            SendMessageW(child, WM_SETFONT, (WPARAM)(child == s->body ? s->font : s->button), FALSE);
        SendMessageW(s->body, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(s->body, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SendMessageW(s->body, EM_SETSEL, 0, 0);
        SetWindowSubclass(s->yes, ButtonProc, ButtonSubclass, (DWORD_PTR)s);
        SetWindowSubclass(s->no, ButtonProc, ButtonSubclass, (DWORD_PTR)s);
        return 0;
    }
    case WM_SIZE:
        if (a != SIZE_MINIMIZED)
            s->Layout();
        return 0;
    case WM_DPICHANGED: {
        s->dpi = HIWORD(a);
        s->Fonts();
        auto r = (RECT *)b;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        s->Layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MONITORINFO monitor{sizeof monitor};
        GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &monitor);
        HDC dc = GetDC(w);
        int width = s->S(s->options.format ? 500 : 440);
        RECT previousHeading = s->headingRect, previousMessage = s->messageRect,
             previousCompatibility = s->compatibilityRect;
        int previousBodyTop = s->bodyTop;
        int top = s->TopLayout(dc, width);
        int footer = s->FooterHeight(dc, width, s->ButtonWidth(dc, s->options.primary, true),
                                     s->ButtonWidth(dc, s->options.secondary, false));
        ReleaseDC(w, dc);
        s->headingRect = previousHeading;
        s->messageRect = previousMessage;
        s->compatibilityRect = previousCompatibility;
        s->bodyTop = previousBodyTop;
        int height = top + (s->options.details.empty() ? 0 : s->S(75)) + footer + s->S(50);
        if (s->options.format)
            height = std::max(height, s->S(400));
        ((MINMAXINFO *)b)->ptMinTrackSize = {
            std::min<LONG>(width, monitor.rcWork.right - monitor.rcWork.left),
            std::min<LONG>(height, monitor.rcWork.bottom - monitor.rcWork.top)};
        return 0;
    }
    case DM_GETDEFID:
        return MAKELRESULT(s->options.defaultPrimary ? IDOK : IDCANCEL, DC_HASDEFID);
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
        s->answer = s->options.secondary.empty();
        s->done = true;
        DestroyWindow(w);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)a, RGB(39, 54, 77));
        SetBkColor((HDC)a, RGB(255, 255, 255));
        return (LRESULT)s->white;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)b)->CtlType == ODT_BUTTON) {
            DrawButton(*s, *(DRAWITEMSTRUCT *)b);
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(w, &ps);
        RECT client{};
        GetClientRect(w, &client);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap =
            CreateCompatibleBitmap(dc, std::max<LONG>(1, client.right), std::max<LONG>(1, client.bottom));
        auto previous = SelectObject(buffer, bitmap);
        Paint(*s, buffer, client);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, buffer, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(buffer, previous);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        s->done = true;
        s->window = nullptr;
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(w, m, a, b);
}
} // namespace

bool ShowProductDialog(HWND owner, const ProductDialog &options) {
    GraphicsSession graphics;
    static ATOM registered = 0;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = ClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        registered = RegisterClassW(&wc);
        if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw Error(L"无法注册提示窗口：" + WinError());
    }
    State state(options);
    if (state.options.heading.empty())
        state.options.heading = options.title;
    if (state.options.format) {
        state.options.heading = L"整盘格式化";
        state.options.message = L"确认后将删除目标 U 盘上的全部分区和文件。";
    }
    auto getDpi = (UINT(WINAPI *)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    state.dpi = owner && getDpi ? getDpi(owner) : 96;
    state.Fonts();
    MONITORINFO monitor{sizeof monitor};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    HDC dc = GetDC(owner);
    int width = state.S(state.options.format ? 580 : 520);
    // beta.3 uses a 520px message window and 580px format window. The detail
    // pane grows within the monitor work area and remains scrollable.
    RECT frame{0, 0, 0, 0};
    auto adjust = (BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT))GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adjust)
        adjust(&frame, DialogStyle, FALSE, DialogExStyle, (UINT)state.dpi);
    else
        AdjustWindowRectEx(&frame, DialogStyle, FALSE, DialogExStyle);
    int frameWidth = frame.right - frame.left, frameHeight = frame.bottom - frame.top;
    width = std::min<int>(width, monitor.rcWork.right - monitor.rcWork.left);
    int height = state.PreferredHeight(dc, width - frameWidth) + frameHeight;
    if (state.options.format)
        height = state.S(470);
    ReleaseDC(owner, dc);
    height = std::min<int>(height, monitor.rcWork.bottom - monitor.rcWork.top);
    RECT parent = monitor.rcWork;
    if (owner)
        GetWindowRect(owner, &parent);
    int left = std::clamp<int>(parent.left + (parent.right - parent.left - width) / 2, monitor.rcWork.left,
                               monitor.rcWork.right - width);
    int top = std::clamp<int>(parent.top + (parent.bottom - parent.top - height) / 2, monitor.rcWork.top,
                              monitor.rcWork.bottom - height);
    HWND window = CreateWindowExW(DialogExStyle, ClassName, state.options.title.c_str(), DialogStyle, left,
                                  top, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window)
        throw Error(L"无法打开提示窗口：" + WinError());
    if (owner)
        SendMessageW(window, WM_SETICON, ICON_SMALL, SendMessageW(owner, WM_GETICON, ICON_SMALL, 0));
    HWND previousFocus = GetFocus();
    bool enableOwner = owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (enableOwner)
        EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(state.options.defaultPrimary ? state.yes : state.no);
    int quitCode = 0;
    bool repostQuit = false;
    MSG message{};
    while (!state.done) {
        int status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            repostQuit = status == 0;
            quitCode = (int)message.wParam;
            break;
        }
        if (owner && !IsWindow(owner))
            break;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            HWND focus = GetFocus();
            int action = focus == state.yes             ? IDOK
                         : focus == state.no            ? IDCANCEL
                         : state.options.defaultPrimary ? IDOK
                                                        : IDCANCEL;
            SendMessageW(window, WM_COMMAND, action, 0);
            continue;
        }
        // IsDialogMessage can turn an owner-drawn default button into a stock
        // BS_DEFPUSHBUTTON while tabbing. Handle tab traversal explicitly so
        // rounded rendering and the default-cancel contract remain intact.
        if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
            HWND next = GetNextDlgTabItem(window, GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (next)
                SetFocus(next);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(window))
        DestroyWindow(window);
    if (enableOwner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
        if (previousFocus && IsWindow(previousFocus) && IsChild(owner, previousFocus))
            SetFocus(previousFocus);
    }
    if (repostQuit)
        PostQuitMessage(quitCode);
    return state.answer;
}

bool ShowProductDialog(HWND owner, const std::wstring &title, const std::wstring &message,
                       const std::wstring &details, const std::wstring &primary,
                       const std::wstring &secondary, bool danger) {
    ProductDialog options;
    options.title = options.heading = title;
    options.message = message;
    options.details = details;
    options.primary = primary;
    options.secondary = secondary;
    options.kind = danger ? DialogKind::Warning : DialogKind::Info;
    options.defaultPrimary = secondary.empty();
    options.hint = danger ? L"默认取消 · 请核对磁盘编号、名称和容量" : L"详细内容可滚动查看。";
    return ShowProductDialog(owner, options);
}
} // namespace wu
