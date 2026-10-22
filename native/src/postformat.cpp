#include "platform.hpp"

namespace wu {
std::vector<std::wstring> FormatArguments(const Request &r, const Prepared &p, const std::wstring &volume,
                                          uint64_t offset, uint64_t size) {
    GUID id{};
    if (volume.size() != 49 || volume.substr(0, 11) != L"\\\\?\\Volume{" || volume.back() != L'\\' ||
        FAILED(CLSIDFromString(volume.substr(10, 38).c_str(), &id)) || p.image.label.empty() ||
        p.image.label.size() > 11 ||
        p.image.label.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::wstring::npos ||
        (r.disk.sector != 512 && r.disk.sector != 4096) || offset != MiB || !size || offset > r.disk.size ||
        size > r.disk.size - offset)
        throw Error(L"格式化参数、卷 GUID 或分区范围无效，未启动格式化工具。");
    if (r.fs == L"FAT32")
        return {L"--confirmed-volume",   volume.substr(0, 48),  std::to_wstring(r.disk.number),
                std::to_wstring(offset), std::to_wstring(size), std::to_wstring(r.disk.sector)};
    if (r.fs == L"NTFS")
        return {volume, L"/FS:NTFS", L"/Q", L"/X", L"/Y", L"/V:" + p.image.label};
    throw Error(L"格式化文件系统选项无效。");
}
VolumeState WaitFileSystem(const std::wstring &fs, const std::function<VolumeState()> &probe,
                           const std::function<void()> &validate, const Hooks &h, std::atomic<bool> &stop,
                           std::function<uint64_t()> now, std::function<void(DWORD)> wait) {
    if ((fs != L"FAT32" && fs != L"NTFS") || !probe || !validate)
        throw Error(L"文件系统核验参数无效。");
    if (!now)
        now = [] { return GetTickCount64(); };
    if (!wait)
        wait = [](DWORD ms) { Sleep(ms); };
    auto started = now(), lastLog = started;
    std::wstring previous, detail;
    for (;;) {
        CheckCancel(stop);
        validate(); // A changed target is a hard failure, never a format retry.
        bool observed = false;
        VolumeState state;
        try {
            state = probe();
            observed = true;
        } catch (const Cancelled &) {
            throw;
        } catch (const Error &e) {
            detail = e.text;
        }
        CheckCancel(stop);
        if (observed) {
            auto actual = Lower(state.fs);
            if (!actual.empty() && actual != L"raw" && actual != Lower(fs))
                throw Error(L"格式化后文件系统不符：预期 " + fs + L"，实际 " + state.fs + L"。停止复制。");
            if (actual == Lower(fs) && state.cluster >= 512 && state.cluster <= 2 * MiB &&
                !(state.cluster & (state.cluster - 1)) && state.total && state.free <= state.total &&
                now() - started < 30000) {
                if (h.log)
                    h.log(L"文件系统已就绪：" + fs + L"；分配单元=" + std::to_wstring(state.cluster) +
                          L" 字节；可用=" + FormatSize(state.free));
                return state;
            }
            detail = L"文件系统=" + (state.fs.empty() ? L"未识别" : state.fs) + L"；分配单元=" +
                     std::to_wstring(state.cluster) + L"；容量信息尚未就绪";
        }
        if (now() - started >= 30000)
            throw Error(L"格式化命令已结束，但文件系统在 30 秒内未就绪，停止复制。\n" + detail);
        if (detail != previous || now() - lastLog >= 5000) {
            if (h.log)
                h.log(L"等待格式化后的文件系统就绪：" + detail);
            previous = detail;
            lastLog = now();
        }
        wait(200);
    }
}
} // namespace wu
