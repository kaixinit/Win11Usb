#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <windows.h>

namespace wu {
enum class DialogKind { Info, Warning, Error };

// Visual contract follows beta.3 MessageDialog.xaml and FormatDialog.xaml.
// All rendering and modality use Windows APIs; no managed runtime is loaded.
struct ProductDialog {
    std::wstring title = L"Windows 安装 U 盘";
    std::wstring heading;
    std::wstring message;
    std::wstring details;
    std::wstring hint;
    std::wstring primary = L"确定";
    std::wstring secondary;
    DialogKind kind = DialogKind::Info;
    bool defaultPrimary = true;
    bool format = false;
    bool ntfs = false;
};

bool ShowProductDialog(HWND owner, const ProductDialog &options);

// Adapter for existing Ask call sites. New call sites should use ProductDialog
// to provide the same explicit wording, severity and defaults as beta.3.
bool ShowProductDialog(HWND owner, const std::wstring &title, const std::wstring &message,
                       const std::wstring &details, const std::wstring &primary = L"继续",
                       const std::wstring &secondary = L"取消", bool danger = false);
} // namespace wu
