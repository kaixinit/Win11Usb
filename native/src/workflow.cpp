#include "platform.hpp"
#include <sstream>

namespace wu {
Result RunWorkflow(const Request &r, Backend &backend, const Hooks &h, std::atomic<bool> &stop) {
    struct FinishTask {
        Backend &backend;
        ~FinishTask() { backend.Finish(); }
    } finish{backend};
    Result result;
    Prepared p;
    auto log = [&](const std::wstring &s) {
        if (h.log)
            h.log(s);
    };
    auto stage = [&](int n, const std::wstring &s) {
        CheckCancel(stop);
        log(L"[" + std::to_wstring(n) + L"/6] " + s);
        if (h.progress)
            h.progress({n, -1, 0, s, L"", 0});
    };
    auto ask = [&](Prompt k, const std::wstring &s) {
        CheckCancel(stop);
        return h.ask && h.ask(k, s);
    };
    try {
        if (r.fs != L"FAT32" && r.fs != L"NTFS")
            throw Error(L"文件系统选项无效。");
        if (!r.disk.eligible() || r.disk.pnp.empty())
            throw Error(L"目标整盘身份未确认。");
        stage(1, L"确认 ISO 和目标 U 盘");
        Hooks preparationHooks = h;
        preparationHooks.workReady = [&](const std::wstring &path) {
            result.work = path;
            if (h.workReady)
                h.workReady(path);
        };
        p = backend.Prepare(r, preparationHooks, stop);
        result.work = p.work;
        backend.Validate(r, p, true);
        CheckCancel(stop);
        log(L"安装系统：" + p.image.display);
        log(L"映像版本：" + p.image.version);
        log(L"U 盘卷标：" + p.image.label);
        log(p.image.note);
        log(L"检查通过：" + r.disk.friendly + L" · " + r.disk.Metadata());
        log(L"本次临时目录：" + p.work);
        std::wstring details = L"安装系统：" + p.image.display + L"\r\n卷标：" + p.image.label +
                               L"\r\n\r\n磁盘 " + std::to_wstring(r.disk.number) + L" | " + r.disk.friendly +
                               L" | " + FormatSize(r.disk.size) + L"\r\n删除全部 " +
                               std::to_wstring(r.disk.partitions.size()) +
                               L" 个旧分区，包括 EFI、隐藏分区和 PE 分区。\r\n重建为一个全容量 " + r.fs +
                               L" 主分区。\r\n\r\nISO：\r\n" + r.iso;
        stage(2, L"将整支 U 盘重建为一个全容量 " + r.fs + L" 分区");
        if (!ask(Prompt::Format, details))
            throw Cancelled();
        backend.Validate(r, p, true);
        CheckCancel(stop);
        result.rebuildStarted = true;
        std::wstring target = backend.Format(r, p, h, stop);
        log(L"完成：" + target + L"，" + r.fs + L"，全容量，一个分区。");
        stage(3, L"检查 install.wim 大小并按需拆分");
        std::vector<File> swm;
        if (p.plan.split) {
            if (!ask(Prompt::Split, L"install.wim 超过 4,294,967,295 字节。\r\n将先在本机拆分为 "
                                    L"SWM，再复制到 U 盘。\r\nNTFS 也沿用相同拆分规则。"))
                throw Cancelled();
            backend.Validate(r, p, false);
            swm = backend.Split(p, h, stop);
            CheckSwm(swm);
            log(L"完成：本机生成 " + std::to_wstring(swm.size()) + L" 个 SWM 文件。");
        } else
            log(L"install.wim 未超过阈值或介质已提供合规 ESD/SWM，跳过拆分。");
        uint64_t total = ManifestBytes(InstallationFiles(p, swm));
        if (total > r.disk.size || r.disk.size - total < 128 * MiB)
            throw Error(L"拆分后容量不足，停止复制。");
        if (!ask(Prompt::Copy, L"将复制安装文件" + std::wstring(p.plan.split ? L"和全部 SWM 分卷" : L"") +
                                   L"。\r\n目标：" + r.disk.friendly + L"\r\n" + target))
            throw Cancelled();
        stage(4, L"复制安装文件");
        backend.Validate(r, p, false);
        backend.Copy(r, p, target, swm, h, stop);
        stage(5, L"检查复制结果");
        backend.Validate(r, p, false);
        backend.Verify(r, p, target, swm, h, stop);
        result.copied = true;
        log(L"安装文件大小和内容校验通过，efi\\boot\\bootx64.efi 已就绪。");
        stage(6, L"清理本次临时 SWM");
        if (swm.empty())
            log(L"本次未拆分映像，自动跳过 SWM 清理。");
        else if (ask(Prompt::Cleanup,
                     L"是否删除本次生成的临时 SWM？\r\n仅清理本次任务的 staging 目录，不影响 U 盘。")) {
            try {
                backend.Cleanup(p, swm);
                log(L"临时 SWM 已清理。");
            } catch (const Error &e) {
                log(L"制作已完成，临时文件清理失败：" + e.text);
            }
        } else
            log(L"临时 SWM 已保留：" + p.staging);
        if (ask(Prompt::EjectUsb, L"设备：" + r.disk.friendly + L"\r\n" + target)) {
            try {
                backend.EjectUsb(r, target);
                log(L"U 盘已安全弹出，可以拔下。");
            } catch (const Error &e) {
                result.ejectWarning = e.text;
                log(L"复制校验通过，但 U 盘尚未安全弹出：" + e.text);
            }
        }
        if (ask(Prompt::EjectIso, L"是否弹出本次使用的 ISO？\r\n" + r.iso)) {
            try {
                backend.EjectIso();
                log(L"已弹出 ISO。");
            } catch (const Error &e) {
                log(L"制作已完成，但 ISO 保留挂载：" + e.text);
            }
        }
        log(L"制作完成 | " + r.disk.friendly + L" | " + p.image.display + L" | " + r.fs + L" | " +
            p.image.label);
        if (h.progress)
            h.progress({6, 100, 100, L"制作完成", L"校验完成", 0});
    } catch (const Cancelled &e) {
        result.cancelled = true;
        result.error = e.text;
        backend.Abort(false);
        log(result.copied           ? L"安装文件已复制并校验，后续清理或弹出操作已停止。"
            : result.rebuildStarted ? L"制作已结束，目标盘可能需要重新制作。"
                                    : L"已取消，尚未清盘。");
    } catch (const Error &e) {
        result.error = e.text;
        backend.Abort(false);
        log(L"已停止：" + e.text);
    } catch (const std::exception &e) {
        result.error = Wide(e.what());
        backend.Abort(false);
        log(L"已停止：" + result.error);
    }
    return result;
}
} // namespace wu
