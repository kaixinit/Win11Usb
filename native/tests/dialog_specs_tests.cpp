#include "dialog_specs.hpp"
#include <iostream>

using namespace wu;
namespace {
int checks = 0;
void Require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
bool Contains(const std::wstring &text, const std::wstring &part) {
    return text.find(part) != text.npos;
}
void Has(const std::wstring &text, const std::wstring &part, const char *message) {
    Require(Contains(text, part), message);
}
template <class F> void Reject(F function, const char *message) {
    try {
        function();
    } catch (const Error &) {
        ++checks;
        return;
    }
    Require(false, message);
}
Request Fixture() {
    Request request;
    request.iso = L"D:\\Windows ISO\\zh-cn_windows_11_business_editions_version_26h2_x64.iso";
    request.fs = L"FAT32";
    request.disk.number = 3;
    request.disk.friendly = L"Kingston DataTraveler 3.0";
    request.disk.size = 62008590336;
    request.disk.letters = {L"F:\\"};
    request.disk.partitions.push_back({});
    return request;
}
void CheckStartAndStop() {
    auto request = Fixture();
    auto start = StartDialog(request);
    Require(start.title == L"开始制作" && start.heading == L"先检查本次制作目标", "beta.3 start title");
    Require(start.primary == L"继续检查" && start.secondary == L"返回检查" && !start.defaultPrimary,
            "start defaults to return check");
    Require(start.kind == DialogKind::Info && !start.format, "start is informational");
    Has(start.details, request.iso, "start retains complete ISO path");
    Has(start.details, request.disk.friendly, "start retains captured device");
    Has(start.details, request.disk.Metadata(), "start retains disk metadata");
    Require(!Contains(start.details, L"NTFS 启动"), "FAT32 start has no NTFS warning");
    request.fs = L"NTFS";
    auto ntfs = StartDialog(request);
    Has(ntfs.details, L"部分 UEFI 固件可能不支持直接从 NTFS 启动。", "NTFS start compatibility note");
    Has(ntfs.details, L"文件系统\r\nNTFS", "NTFS start filesystem");
    for (bool closing : {false, true}) {
        auto stop = StopDialog(closing);
        Require(stop.title == (closing ? L"关闭制作工具" : L"强制结束制作"), "context-specific stop title");
        Require(stop.heading == L"任务仍在运行", "beta.3 stop heading");
        Require(stop.message == L"强制结束会中断当前操作并关闭程序。", "beta.3 stop message");
        Require(stop.primary == (closing ? L"强制结束并关闭" : L"强制结束"), "context-specific stop action");
        Require(stop.secondary == L"继续制作" && !stop.defaultPrimary, "stop defaults to continuing");
        Require(stop.kind == DialogKind::Warning && !stop.format, "stop uses amber warning not red format");
        Require(stop.hint == L"默认继续运行，避免误中断。", "stop uses relevant hint");
        Has(stop.details, L"清盘、格式化或复制", "stop explains partial USB state");
        Has(stop.details, L"ISO 可能保留", "stop explains ISO mount state");
    }
}
void CheckWorkflow() {
    auto request = Fixture();
    const std::wstring details = L"安装系统：Windows 11 Education 26H2\r\n磁盘 3\r\nISO：" + request.iso;
    for (const auto &fs : {L"FAT32", L"NTFS"}) {
        request.fs = fs;
        auto format = WorkflowDialog(Prompt::Format, details, request);
        Require(format.title == L"最后确认：整盘格式化" && format.heading == L"整盘格式化", "format title");
        Require(format.format && format.kind == DialogKind::Warning, "format enables red format styling");
        Require(format.ntfs == (request.fs == L"NTFS"), "format has conditional NTFS note");
        Require(!format.defaultPrimary && format.secondary == L"取消" && format.primary == L"确认格式化",
                "format defaults to cancel");
        Require(format.details == details, "format preserves complete supplied details");
        Require(format.message == L"确认后将删除目标 U 盘上的全部分区和文件。", "format warning matches beta.3");
        Has(format.hint, L"默认取消", "format default is explicit");
    }
    auto cleanup = WorkflowDialog(Prompt::Cleanup, L"internal staging diagnostics", request);
    Require(cleanup.primary == L"清理文件" && cleanup.secondary == L"保留文件" && cleanup.defaultPrimary,
            "cleanup action defaults match beta.3");
    Require(cleanup.details.empty() && cleanup.kind == DialogKind::Info, "cleanup keeps beta.3 simple details");
    Require(cleanup.message == L"是否删除本次生成的临时 SWM 文件？", "cleanup prompt matches beta.3");
    auto usb = WorkflowDialog(Prompt::EjectUsb, L"设备：Kingston\r\nF:\\", request);
    Require(usb.primary == L"安全弹出" && usb.secondary == L"保持连接" && usb.defaultPrimary,
            "USB eject action defaults match beta.3");
    Require(usb.details == L"设备：Kingston\r\nF:\\", "USB eject retains actual target details");
    auto iso = WorkflowDialog(Prompt::EjectIso, L"ignored task-internal text", request);
    Require(iso.primary == L"弹出 ISO" && iso.secondary == L"保留挂载" && iso.defaultPrimary,
            "ISO eject action defaults match beta.3");
    Require(iso.details == request.iso, "ISO eject retains complete request path");
    Reject([&] { WorkflowDialog(Prompt::Split, L"", request); }, "split must automatically continue");
    Reject([&] { WorkflowDialog(Prompt::Copy, L"", request); }, "copy must automatically continue");
    Reject([&] { WorkflowDialog((Prompt)999, L"", request); }, "unknown requests must reject");
}
void CheckResults() {
    const auto request = Fixture();
    Result result;
    result.copied = true;
    result.imageDisplay = L"Windows 11 Education 26H2";
    result.volumeLabel = L"WIN11_26H2";
    result.usbResult = L"U 盘已安全弹出，可以拔下。";
    result.isoResult = L"已弹出 ISO。";
    auto success = ResultDialog(request, result);
    Require(success.title == L"制作结果" && success.heading == L"安装 U 盘已制作完成", "success title");
    Require(success.primary == L"完成" && success.secondary.empty() && success.defaultPrimary,
            "success is a single completion action");
    Require(success.kind == DialogKind::Info && !success.format, "success severity");
    Has(success.message, L"大小与内容校验", "success retains approved content verification");
    for (const auto &part : {request.disk.friendly, result.imageDisplay, result.volumeLabel,
                             result.usbResult, result.isoResult})
        Has(success.details, part, "success captures actual task results");
    auto refreshedRequest = request;
    refreshedRequest.disk.friendly = L"A different disk after refresh";
    Require(!Contains(success.details, refreshedRequest.disk.friendly), "result is a snapshot not refreshed catalog");
    result.ejectWarning = L"CM_Request_Device_Eject：设备请求不受支持（veto 8）。";
    auto warning = ResultDialog(request, result);
    Require(warning.kind == DialogKind::Warning && !warning.format, "eject failure is amber warning");
    Require(warning.primary == L"完成" && warning.secondary.empty(), "eject warning retains completed state");
    Has(warning.title, L"U 盘尚未弹出", "eject warning accurate title");
    Has(warning.details, result.ejectWarning, "eject warning retains exact native reason");
    Has(warning.details, result.isoResult, "eject warning retains actual ISO result");
    Require(!Contains(warning.message, L"占用") && !Contains(warning.details, L"占用"),
            "unsupported eject is not mislabeled as open handles");
    result.error = L"后续操作失败：测试错误";
    result.work = L"C:\\Users\\Tester\\AppData\\Local\\Win11Usb-Native-Task";
    auto postcopy = ResultDialog(request, result);
    Require(postcopy.kind == DialogKind::Error && postcopy.primary == L"知道了", "post-copy failure action and severity");
    Require(postcopy.heading == L"复制已校验，后续步骤未完成", "post-copy failure retains copy success");
    Has(postcopy.details, result.error, "errors outrank eject warning and retain original cause");
    Has(postcopy.details, result.work, "error includes retained log directory");
    result.copied = false;
    result.ejectWarning.clear();
    auto preclear = ResultDialog(request, result);
    Require(preclear.heading == L"制作未完成，请查看日志", "failure heading matches beta.3");
    Has(preclear.message, L"尚未清盘", "pre-clear error accurately identifies untouched disk");
    result.rebuildStarted = true;
    auto postclear = ResultDialog(request, result);
    Has(postclear.message, L"已经进入重建流程", "post-clear error does not claim untouched disk");
    result.cancelled = true;
    auto cancelled = ResultDialog(request, result);
    Require(cancelled.heading == L"已取消制作" && cancelled.kind == DialogKind::Info,
            "cancellation uses informational beta.3 state");
    result = {};
    auto incomplete = ResultDialog(request, result);
    Require(incomplete.kind == DialogKind::Error && !Contains(incomplete.heading, L"已制作完成"),
            "absent copy result cannot be presented as success");
}
} // namespace
int wmain() {
    try {
        CheckStartAndStop();
        CheckWorkflow();
        CheckResults();
        std::cout << "PASS beta.3 production dialog specifications: " << checks
                  << " assertions; no windows displayed or physical disks accessed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL dialog specifications: " << error.what() << "\n";
        return 1;
    }
}
