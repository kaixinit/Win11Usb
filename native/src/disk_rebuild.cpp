#include "platform.hpp"

namespace wu {
namespace {
std::wstring IdentityConflict(const Disk &a, const Disk &b) {
    if (Lower(a.path) != Lower(b.path) || Lower(a.pnp) != Lower(b.pnp))
        return L"设备接口或 PnP 身份变化";
    if (b.numberKnown && a.number != b.number)
        return L"物理磁盘编号变化";
    if (b.capacityKnown && (a.size != b.size || a.sector != b.sector))
        return L"容量或逻辑扇区大小变化";
    if (b.descriptorKnown && (a.serial != b.serial || a.friendly != b.friendly))
        return L"设备序列号或型号变化";
    if (b.attributesKnown && (b.readOnly || b.offline))
        return L"磁盘变为只读或离线";
    return L"";
}
bool IdentityReady(const Disk &d) {
    return d.descriptorKnown && d.numberKnown && d.capacityKnown && d.attributesKnown &&
           d.probeErrors.empty() && !d.readOnly && !d.offline;
}
std::wstring State(const Disk &d, bool layout) {
    std::wstring s = L"编号=" + (d.numberKnown ? std::to_wstring(d.number) : L"未确认") + L"；描述符=" +
                     (d.descriptorKnown ? L"就绪" : L"未确认") + L"；容量=" +
                     (d.capacityKnown ? std::to_wstring(d.size) : L"未确认") + L"；扇区=" +
                     std::to_wstring(d.sector) + L"；属性=" +
                     (d.attributesKnown ? (d.offline    ? L"离线"
                                           : d.readOnly ? L"只读"
                                                        : L"可写/在线")
                                        : L"未确认");
    if (layout) {
        s += std::wstring(L"；布局=") +
             (d.layoutKnown ? (d.style == PARTITION_STYLE_MBR   ? L"MBR"
                               : d.style == PARTITION_STYLE_GPT ? L"GPT"
                                                                : L"RAW")
                            : L"未确认") +
             L"；返回槽位=" + std::to_wstring(d.layoutEntries) + L"；有效分区=" +
             std::to_wstring(d.partitions.size());
        if (d.layoutKnown && d.style == PARTITION_STYLE_MBR)
            s += L"；MBR 签名=" + d.tableId;
        for (size_t i = 0; i < d.partitions.size() && i < 16; i++) {
            const auto &p = d.partitions[i];
            s += L"\n  分区 " + std::to_wstring(p.number) + L"：偏移=" + std::to_wstring(p.offset) +
                 L"，长度=" + std::to_wstring(p.size) + L"，MBR 类型=" + std::to_wstring(p.mbrType);
        }
    }
    for (const auto &e : d.probeErrors)
        s += L"\n  查询错误：" + e;
    return s;
}
} // namespace
Disk InitializeEmptyMbr(const Disk &expected, DWORD signature, const DiskRebuildOps &ops, const Hooks &hooks,
                        std::atomic<bool> &stop) {
    if (!expected.eligible() || !expected.bootKnown || expected.system || expected.readOnly ||
        expected.offline || expected.path.empty() || expected.pnp.empty() || !signature || !ops.read ||
        !ops.refresh || !ops.initialize)
        throw Error(L"重建磁盘的身份、启动盘检查或初始化参数不完整，未建立新分区。");
    auto now = ops.now ? ops.now : std::function<uint64_t()>([] { return GetTickCount64(); });
    auto wait = ops.wait ? ops.wait : std::function<void(DWORD)>([](DWORD ms) { Sleep(ms); });
    auto log = [&](const std::wstring &s) {
        if (hooks.log)
            hooks.log(s);
    };
    auto poll = [&](bool layout) {
        const std::wstring stage = layout ? L"空 MBR 核验" : L"清盘后设备身份复核";
        const uint64_t started = now();
        uint64_t lastLog = started;
        unsigned stable = 0;
        std::wstring last, detail;
        for (;;) {
            CheckCancel(stop);
            std::wstring refreshError;
            try {
                ops.refresh();
            } catch (const Cancelled &) {
                throw;
            } catch (const Error &e) {
                refreshError = e.text;
            }
            CheckCancel(stop);
            bool ready = false;
            Disk d;
            bool observed = false;
            try {
                d = ops.read(layout);
                observed = true;
            } catch (const Cancelled &) {
                throw;
            } catch (const Error &e) {
                detail = L"读取设备失败：" + e.text;
            }
            CheckCancel(stop);
            if (observed) {
                auto conflict = IdentityConflict(expected, d);
                detail = State(d, layout);
                if (!conflict.empty()) {
                    log(stage + L"：" + detail);
                    throw Error(stage + L"发现" + conflict + L"，未建立新分区。\n" + detail);
                }
                ready = refreshError.empty() && IdentityReady(d) &&
                        (!layout || (SameIdentity(expected, d) && d.style == PARTITION_STYLE_MBR &&
                                     d.tableId == std::to_wstring(signature) && d.partitions.empty()));
                if (ready && ++stable == 2 && now() - started < 30000) {
                    log(stage + L"通过：" + detail);
                    return d;
                }
            }
            if (!ready)
                stable = 0;
            if (!refreshError.empty())
                detail += L"\n  刷新错误：" + refreshError;
            const uint64_t elapsed = now() - started;
            if (elapsed >= 30000) {
                log(stage + L"超时：" + detail);
                throw Error(stage + L"在 30 秒内未就绪，未建立新分区。旧分区可能已被删除。\n" + detail);
            }
            if (detail != last || now() - lastLog >= 5000) {
                log(stage + L"，等待驱动状态同步（" + std::to_wstring(elapsed / 1000) + L" 秒）：" + detail);
                last = detail;
                lastLog = now();
            }
            CheckCancel(stop);
            wait(200);
        }
    };
    // A deleted partition table is uninitialized. Do not make a successful
    // GET_DRIVE_LAYOUT_EX on that intermediate state a prerequisite to CREATE_DISK.
    // Identity and writable/online attributes are still required twice first.
    poll(false);
    CheckCancel(stop);
    log(L"设备身份已确认，初始化空 MBR 分区表（签名=" + std::to_wstring(signature) + L"，尚未建立新分区）…");
    try {
        ops.initialize(signature);
    } catch (const Cancelled &) {
        throw;
    } catch (const Error &e) {
        throw Error(L"空 MBR 初始化未完成，未建立新分区。旧分区可能已被删除。\n" + e.text);
    }
    CheckCancel(stop);
    return poll(true);
}
Volume WaitCreatedVolume(const Disk &expected, DWORD signature, BYTE type, uint64_t offset, uint64_t size,
                         const NewVolumeOps &ops, const Hooks &h, std::atomic<bool> &stop) {
    if (!expected.eligible() || !expected.bootKnown || expected.system || !signature ||
        (type != 0x0c && type != 0x07) || !ops.read || !ops.volumes || !ops.refresh)
        throw Error(L"新分区卷的核验参数不完整，未进行格式化。");
    auto now = ops.now ? ops.now : std::function<uint64_t()>([] { return GetTickCount64(); });
    auto wait = ops.wait ? ops.wait : std::function<void(DWORD)>([](DWORD ms) { Sleep(ms); });
    uint64_t started = now(), lastLog = started;
    std::wstring detail, previous, candidate;
    unsigned stable = 0;
    for (;;) {
        CheckCancel(stop);
        bool observed = false;
        Disk d;
        try {
            ops.refresh();
            d = ops.read(true);
            observed = true;
        } catch (const Cancelled &) {
            throw;
        } catch (const Error &e) {
            detail = L"读取新分区失败：" + e.text;
        }
        CheckCancel(stop);
        bool ready = false;
        Volume selected;
        if (observed) {
            detail = State(d, true);
            auto conflict = IdentityConflict(expected, d);
            if (!conflict.empty())
                throw Error(L"新分区核验发现" + conflict + L"，未进行格式化。\n" + detail);
            if (d.layoutKnown && d.style == PARTITION_STYLE_MBR && d.tableId == std::to_wstring(signature) &&
                !d.partitions.empty() &&
                (!CreatedPartitionReady(d, offset, size) || d.partitions[0].mbrType != type ||
                 !d.partitions[0].active || d.partitions[0].number != 1)) {
                // Incomplete probes may settle; a fully known wrong new layout is a hard failure.
                if (d.complete())
                    throw Error(L"新分区的数量、类型、活动标记或范围不符，未进行格式化。\n" + detail);
            }
            if (SameIdentity(expected, d) && CreatedPartitionReady(d, offset, size) &&
                d.tableId == std::to_wstring(signature) && d.partitions[0].mbrType == type &&
                d.partitions[0].active && d.partitions[0].number == 1) {
                try {
                    unsigned found = 0;
                    for (const auto &v : ops.volumes())
                        if (v.extents.size() == 1 && v.extents[0].disk == expected.number &&
                            v.extents[0].offset == offset && v.extents[0].size == size) {
                            selected = v;
                            found++;
                        }
                    if (found == 1 && !selected.guid.empty())
                        ready = true;
                    else
                        detail += L"；匹配的新卷数量=" + std::to_wstring(found);
                } catch (const Cancelled &) {
                    throw;
                } catch (const Error &e) {
                    detail += L"\n卷枚举错误：" + e.text;
                }
            }
        }
        CheckCancel(stop);
        if (ready) {
            if (Lower(candidate) != Lower(selected.guid))
                stable = 0;
            candidate = selected.guid;
            if (++stable == 2 && now() - started < 30000) {
                if (h.log)
                    h.log(L"新分区及卷 GUID 已连续核验：" + selected.guid + L"\n" + detail);
                return selected;
            }
        } else
            stable = 0;
        if (now() - started >= 30000)
            throw Error(L"等待新分区卷在 30 秒内就绪失败，未进行格式化。\n" + detail);
        if (detail != previous || now() - lastLog >= 5000) {
            if (h.log)
                h.log(L"等待新分区和卷 GUID 就绪：" + detail);
            previous = detail;
            lastLog = now();
        }
        wait(200);
    }
}
} // namespace wu
