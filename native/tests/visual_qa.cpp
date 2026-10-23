// Read-only visual acceptance fixture. The production UI, icons, layout, and
// dialog specifications are compiled directly; no disk-writing backend is linked.
#ifndef WIN11USB_READONLY
#error Visual QA must be built with WIN11USB_READONLY.
#endif
#include "../src/ui.cpp"
#include "dialog_specs.hpp"

using namespace wu;
namespace {
constexpr UINT ShowCaseMessage = WM_APP + 50;
enum class Case : UINT {
    Ready = 0x2100,
    Running = 0x2110,
    Start = 0x2120,
    StartNtfs = 0x2130,
    FormatFat32 = 0x2140,
    FormatNtfs = 0x2150,
    Cleanup = 0x2160,
    EjectUsb = 0x2170,
    EjectIso = 0x2180,
    Success = 0x2190,
    EjectWarning = 0x21a0,
    Stop = 0x21b0
};
struct Scene {
    Case id;
    const wchar_t *argument;
    const wchar_t *menu;
};
constexpr Scene Scenes[] = {
    {Case::Ready, L"ready", L"&1 就绪主界面（只读验收）"},
    {Case::Running, L"running", L"&2 制作中主界面（只读验收）"},
    {Case::Start, L"start", L"&3 开始检查 · FAT32（只读验收）"},
    {Case::StartNtfs, L"start-ntfs", L"&4 开始检查 · NTFS（只读验收）"},
    {Case::FormatFat32, L"format", L"&5 整盘格式化 · FAT32（只读验收）"},
    {Case::FormatNtfs, L"ntfs", L"&6 整盘格式化 · NTFS（只读验收）"},
    {Case::Cleanup, L"cleanup", L"&7 清理临时 SWM（只读验收）"},
    {Case::EjectUsb, L"usb", L"&8 安全弹出 U 盘（只读验收）"},
    {Case::EjectIso, L"iso", L"&9 弹出安装 ISO（只读验收）"},
    {Case::Success, L"success", L"&A 制作成功结果（只读验收）"},
    {Case::EjectWarning, L"warning", L"&B 制作完成 · 弹出警告（只读验收）"},
    {Case::Stop, L"stop", L"&C 强制结束提示（只读验收）"}
};
void FileSystem(Ui &ui, const std::wstring &fs) {
    SendMessageW(ui.fat, BM_SETCHECK, fs == L"FAT32" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ui.ntfs, BM_SETCHECK, fs == L"NTFS" ? BST_CHECKED : BST_UNCHECKED, 0);
}
void Ready(Ui &ui, const std::wstring &fs = L"FAT32") {
    ui.busy = false;
    ui.finished = false;
    ui.runningSample = false;
    ui.Sample();
    FileSystem(ui, fs);
    ui.phase = L"准备就绪";
    ui.percentText = L"0.0%";
    ui.fileText = L"等待开始制作";
    ui.progress = {0, 0, 0, L"准备就绪", L"", 0};
    SetWindowTextW(ui.log, L"");
    ui.Log(L"等待开始制作。操作日志将在这里实时显示。");
    ui.Log(L"[只读验收] 所有设备、镜像和制作状态均为示例，不操作磁盘。");
    ui.State();
}
Request Snapshot(Ui &ui) {
    Request request;
    request.iso = ui.iso;
    request.fs = ui.Fs();
    request.launcher = ExePath();
    if (auto disk = ui.Selection())
        request.disk = *disk;
    ui.activeRequest = request;
    return request;
}
Result Completed(bool warning) {
    Result result;
    result.copied = true;
    result.rebuildStarted = true;
    result.imageDisplay = L"Windows 11 教育版 26H2";
    result.volumeLabel = L"WIN11_26H2";
    result.usbResult = warning ? L"U 盘 F: 尚未安全弹出，请稍后通过系统安全弹出。"
                               : L"U 盘 F: 已安全弹出，可以拔下。";
    result.isoResult = L"已弹出本次使用的 Windows ISO。";
    if (warning)
        result.ejectWarning =
            L"此设备节点不支持当前安全弹出请求。（CM 23，veto 8）"
            L"U 盘尚未安全弹出，请稍后通过系统安全弹出。";
    return result;
}
void Preparing(Ui &ui, int stage, const std::wstring &phase) {
    ui.busy = true;
    ui.finished = false;
    ui.phase = phase;
    ui.progress = {stage, -1, 0, phase, L"", 0};
    ui.percentText = L"…";
    ui.fileText = stage == 2 ? L"安装镜像和目标设备已检查，等待格式化确认。"
                            : L"文件大小和内容校验通过。";
    ui.State();
}
std::wstring FormatDetails(const Request &request) {
    return L"安装系统：Windows 11 教育版 26H2\r\n卷标：WIN11_26H2\r\n\r\n"
           L"目标磁盘：磁盘 3  |  Kingston DataTraveler 3.0  |  57.8 GiB\r\n"
           L"文件系统：" + request.fs + L"\r\n\r\nISO 文件：\r\n" + request.iso +
           L"\r\n\r\n格式化后会删除这支 U 盘的所有旧分区（包括 EFI、隐藏分区和微 PE 分区），"
           L"并建立一个占用全容量的 " + request.fs + L" 主分区。\r\n\r\n"
           L"请确认已备份，并再次核对目标磁盘编号、设备名称和容量。";
}
void ShowScene(Ui &ui, Case selected) {
    Ready(ui, selected == Case::StartNtfs || selected == Case::FormatNtfs ? L"NTFS" : L"FAT32");
    Request request = Snapshot(ui);
    if (selected == Case::Ready)
        return;
    if (selected == Case::Running) {
        ui.Simulate();
        ui.Log(L"[只读验收] 此处为稳定的制作中示例，不执行复制。");
        return;
    }
    ProductDialog dialog;
    switch (selected) {
    case Case::Start:
    case Case::StartNtfs:
        dialog = StartDialog(request);
        break;
    case Case::FormatFat32:
    case Case::FormatNtfs:
        Preparing(ui, 2, L"将整支 U 盘重建为一个全容量 " + request.fs + L" 分区");
        dialog = WorkflowDialog(Prompt::Format, FormatDetails(request), request);
        break;
    case Case::Cleanup:
        Preparing(ui, 6, L"清理本次临时 SWM");
        dialog = WorkflowDialog(Prompt::Cleanup,
                                L"是否删除本次生成的临时 SWM？\r\n"
                                L"仅清理本次任务的 staging 目录，不影响 U 盘。", request);
        break;
    case Case::EjectUsb:
        Preparing(ui, 6, L"清理本次临时 SWM");
        dialog = WorkflowDialog(Prompt::EjectUsb,
                                L"设备：Kingston DataTraveler 3.0\r\nF:\\", request);
        break;
    case Case::EjectIso:
        Preparing(ui, 6, L"清理本次临时 SWM");
        dialog = WorkflowDialog(Prompt::EjectIso,
                                L"是否弹出本次使用的 ISO？\r\n" + request.iso, request);
        break;
    case Case::Success:
    case Case::EjectWarning: {
        auto result = Completed(selected == Case::EjectWarning);
        ui.busy = false;
        ui.finished = true;
        ui.phase = selected == Case::EjectWarning ? L"制作完成 · U 盘未弹出" : L"制作完成";
        ui.progress = {6, 100, 100, ui.phase, L"校验完成", 0};
        ui.percentText = L"已完成";
        ui.fileText = selected == Case::EjectWarning ? L"文件大小和内容校验通过；U 盘尚未安全弹出"
                                                    : L"文件大小和内容校验通过";
        ui.Log(L"[只读验收] 文件大小和内容校验通过（示例）。");
        ui.Log(L"[只读验收] " + result.usbResult);
        ui.Log(L"[只读验收] " + result.isoResult);
        ui.State();
        dialog = ResultDialog(request, result);
        break;
    }
    case Case::Stop:
        ui.Simulate();
        dialog = StopDialog(false);
        break;
    default:
        return;
    }
    // The user's choice only closes this presentation fixture. It is never
    // passed to a worker, command runner, formatter, mount API, or eject API.
    bool accepted = ShowProductDialog(ui.window, dialog);
    ui.Log(std::wstring(L"[只读验收] 弹窗已关闭，选择：") +
           (accepted ? L"主要按钮" : L"取消/返回") + L"。未执行任何制作操作。");
}
LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM a, LPARAM b) {
    auto ui = reinterpret_cast<Ui *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_CREATE) {
            LRESULT result = WindowProc(window, message, a, b);
            HMENU menu = GetSystemMenu(window, FALSE);
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            for (const auto &scene : Scenes)
                AppendMenuW(menu, MF_STRING, static_cast<UINT>(scene.id), scene.menu);
            return result;
        }
        if (ui && message == ShowCaseMessage) {
            ShowScene(*ui, static_cast<Case>(a));
            return 0;
        }
        if (ui && message == WM_SYSCOMMAND)
            for (const auto &scene : Scenes)
                if ((a & 0xfff0) == static_cast<UINT>(scene.id)) {
                    ShowScene(*ui, scene.id);
                    return 0;
                }
    } catch (const Error &error) {
        MessageBoxW(window, error.text.c_str(), L"只读界面验收", MB_OK | MB_ICONERROR);
        return 0;
    }
    return WindowProc(window, message, a, b);
}
Case RequestedCase() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring value = argc > 1 ? argv[1] : L"ready";
    if (value == L"--case" && argc > 2)
        value = argv[2];
    LocalFree(argv);
    if (value == L"format-ntfs")
        value = L"ntfs";
    if (value == L"format-fat32")
        value = L"format";
    for (const auto &scene : Scenes)
        if (value == scene.argument)
            return scene.id;
    throw Error(L"未知只读验收场景：" + value +
                L"\r\n可用：ready / running / start / start-ntfs / format / ntfs / cleanup / "
                L"usb / iso / success / warning / stop");
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    try {
        Case selected = RequestedCase();
        ProductGraphicsSession graphics;
        INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        Ui ui(instance, true, false, false);
        auto getDpi = (UINT(WINAPI *)())GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
        ui.dpi = getDpi ? (int)getDpi() : 96;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = FixtureProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"Win11Usb.Native.VisualQA";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
        if (!RegisterClassExW(&wc))
            throw Error(L"无法创建只读界面验收类：" + WinError());
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int width = std::min(ui.S(DefaultWidth), (int)(work.right - work.left));
        int height = std::min(ui.S(DefaultHeight), (int)(work.bottom - work.top));
        HWND window = CreateWindowExW(0, wc.lpszClassName,
                                      L"Windows 安装 U 盘制作工具 · 界面验收预览（只读）",
                                      MainWindowStyle, work.left + (work.right - work.left - width) / 2,
                                      work.top + (work.bottom - work.top - height) / 2, width, height,
                                      nullptr, nullptr, instance, &ui);
        if (!window)
            throw Error(L"无法创建只读界面验收窗口：" + WinError());
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        PostMessageW(window, ShowCaseMessage, static_cast<WPARAM>(selected), 0);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!DispatchMainKey(window, message) && !IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        return (int)message.wParam;
    } catch (const Error &error) {
        MessageBoxW(nullptr, error.text.c_str(), L"只读界面验收 · 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    } catch (const std::exception &error) {
        MessageBoxA(nullptr, error.what(), "Visual QA", MB_OK | MB_ICONERROR);
        return 1;
    }
}
