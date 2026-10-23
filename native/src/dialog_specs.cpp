#include "dialog_specs.hpp"

namespace wu {
namespace {
ProductDialog Message(const std::wstring &title, const std::wstring &heading, const std::wstring &message,
                      const std::wstring &details = L"", const std::wstring &hint = L"",
                      DialogKind kind = DialogKind::Info, const std::wstring &primary = L"确定",
                      const std::wstring &secondary = L"", bool defaultPrimary = true) {
    ProductDialog dialog;
    dialog.title = title;
    dialog.heading = heading;
    dialog.message = message;
    dialog.details = details;
    dialog.hint = hint;
    dialog.kind = kind;
    dialog.primary = primary;
    dialog.secondary = secondary;
    dialog.defaultPrimary = defaultPrimary;
    return dialog;
}
} // namespace

ProductDialog StartDialog(const Request &request) {
    std::wstring details = L"安装镜像\r\n" + request.iso + L"\r\n\r\n写入U盘\r\n" + request.disk.friendly +
                           L"\r\n" + request.disk.Metadata() + L"\r\n\r\n文件系统\r\n" + request.fs;
    if (request.fs == L"NTFS")
        details += L"\r\n部分 UEFI 固件可能不支持直接从 NTFS 启动。";
    return Message(L"开始制作", L"先检查本次制作目标", L"程序会自动挂载并检查所选 ISO，然后开始准备流程。",
                   details, L"清理磁盘前还会再次显示设备信息并单独确认。", DialogKind::Info, L"继续检查",
                   L"返回检查", false);
}

ProductDialog StopDialog(bool closing) {
    return Message(closing ? L"关闭制作工具" : L"强制结束制作", L"任务仍在运行",
                   L"强制结束会中断当前操作并关闭程序。",
                   L"如果此时正在清盘、格式化或复制，U 盘内容可能不完整，需要重新制作。"
                   L"自动挂载的 ISO 可能保留，可稍后在资源管理器中弹出。",
                   L"默认继续运行，避免误中断。", DialogKind::Warning,
                   closing ? L"强制结束并关闭" : L"强制结束", L"继续制作", false);
}

ProductDialog WorkflowDialog(Prompt prompt, const std::wstring &details, const Request &request) {
    switch (prompt) {
    case Prompt::Format: {
        auto dialog = Message(
            L"最后确认：整盘格式化", L"整盘格式化", L"确认后将删除目标 U 盘上的全部分区和文件。", details,
            L"默认取消 · 请核对磁盘编号、名称和容量", DialogKind::Warning, L"确认格式化", L"取消", false);
        dialog.format = true;
        dialog.ntfs = request.fs == L"NTFS";
        return dialog;
    }
    case Prompt::Cleanup:
        return Message(L"清理临时文件", L"释放本次制作的临时空间", L"是否删除本次生成的临时 SWM 文件？",
                       L"", L"这不会影响已制作完成的 U 盘。", DialogKind::Info, L"清理文件", L"保留文件");
    case Prompt::EjectUsb:
        return Message(L"安全弹出 U 盘", L"制作已完成", L"现在尝试安全弹出目标 U 盘吗？", details,
                       L"选择“保持连接”可稍后手动弹出。", DialogKind::Info, L"安全弹出", L"保持连接");
    case Prompt::EjectIso:
        return Message(L"弹出 Windows ISO", L"释放安装镜像", L"是否弹出本次制作使用的 Windows ISO？",
                       request.iso, L"选择“保留挂载”可继续使用此镜像。", DialogKind::Info, L"弹出 ISO",
                       L"保留挂载");
    default:
        // Split/copy are automatic. An unexpected prompt must not gain approval.
        throw Error(L"无法识别交互请求，未自动确认。");
    }
}

ProductDialog ResultDialog(const Request &request, const Result &result) {
    if (!result.error.empty() || !result.copied) {
        const std::wstring heading = result.cancelled ? L"已取消制作"
                                     : result.copied  ? L"复制已校验，后续步骤未完成"
                                                      : L"制作未完成，请查看日志";
        const std::wstring status = result.rebuildStarted
                                        ? L"目标 U 盘已经进入重建流程，请保留日志并重新制作。"
                                        : L"尚未清盘，目标 U 盘未被修改。";
        std::wstring details = result.error;
        if (!result.work.empty())
            details += (details.empty() ? L"" : L"\r\n\r\n") + std::wstring(L"日志目录：") + result.work;
        return Message(L"制作结果", heading,
                       result.copied ? L"文件复制、大小与内容校验已通过，但后续步骤出现问题。" : status,
                       details, L"请保留日志，核对后再重试。",
                       result.cancelled ? DialogKind::Info : DialogKind::Error, L"知道了");
    }
    if (!result.ejectWarning.empty())
        return Message(L"制作完成，U 盘尚未弹出", L"安装 U 盘已制作完成",
                       L"安装文件已复制并通过内容校验。U 盘尚未安全弹出，请根据以下原因处理。",
                       result.ejectWarning + L"\r\n\r\n" + result.isoResult, L"稍后通过系统安全弹出 U 盘。",
                       DialogKind::Warning, L"完成");

    std::wstring details = L"设备：" + request.disk.friendly + L"\r\n文件系统：" + request.fs +
                           L"\r\n系统：" + result.imageDisplay + L"\r\n卷标：" + result.volumeLabel +
                           L"\r\n\r\n" + result.usbResult + L"\r\n" + result.isoResult;
    return Message(L"制作结果", L"安装 U 盘已制作完成",
                   L"安装文件已复制并通过文件大小与内容校验。安装时请在电脑启动菜单中选择 UEFI: U盘名称。",
                   details, L"日志已保留，可用于排查。", DialogKind::Info, L"完成");
}
} // namespace wu
