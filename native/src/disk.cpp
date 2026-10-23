#include "win11usb.hpp"
#include <algorithm>
#include <cfgmgr32.h>
#include <initguid.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <setupapi.h>
#include <sstream>

namespace wu {
namespace {
struct NonUsb : Error {
    NonUsb(DWORD bus, const std::wstring &name)
        : Error(L"未确认 USB：" + name + L"；报告总线类型 " + std::to_wstring(bus) +
                L"，设备树中也未找到 USB 存储父设备。") {}
};
std::wstring Ansi(const char *s) {
    if (!s || !*s)
        return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_ACP, 0, s, -1, r.data(), n);
    r.resize(n - 1);
    return Trim(r);
}
std::wstring DescriptorText(const std::vector<BYTE> &b, DWORD offset) {
    if (!offset || offset >= b.size())
        return L"";
    auto p = (const char *)b.data() + offset;
    auto end = std::find(p, (const char *)b.data() + b.size(), '\0');
    return end == (const char *)b.data() + b.size() ? L"" : Ansi(p);
}
std::vector<std::wstring> Ancestors(const std::wstring &pnp) {
    std::vector<std::wstring> result;
    if (pnp.empty())
        return result;
    result.push_back(pnp);
    std::wstring id = pnp;
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, id.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return result;
    std::set<DEVINST> seen;
    for (unsigned depth = 0; depth < 32 && seen.insert(node).second; depth++) {
        wchar_t name[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDW(node, name, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
            result.push_back(name);
        DEVINST parent;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
            break;
        node = parent;
    }
    return result;
}
std::wstring NodeName(const std::wstring &pnp) {
    std::wstring id = pnp;
    DEVINST node = 0;
    if (id.empty() || CM_Locate_DevNodeW(&node, id.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return L"USB 磁盘";
    for (ULONG property : {CM_DRP_FRIENDLYNAME, CM_DRP_DEVICEDESC}) {
        wchar_t name[2048];
        ULONG bytes = sizeof name, type = 0;
        if (CM_Get_DevNode_Registry_PropertyW(node, property, &type, name, &bytes, 0) == CR_SUCCESS &&
            type == REG_SZ && bytes >= 2) {
            name[2047] = 0;
            auto s = Trim(name);
            if (!s.empty())
                return s;
        }
    }
    return L"USB 磁盘";
}
bool IsEfi(const Partition &p) {
    static const GUID efi = {0xc12a7328, 0xf81f, 0x11d2, {0xba, 0x4b, 0, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b}};
    return IsEqualGUID(p.gptType, efi) || p.mbrType == 0xef;
}
} // namespace
bool IsUsbStorage(DWORD bus, const std::vector<std::wstring> &ancestors) {
    if (bus == BusTypeUsb)
        return true;
    for (const auto &id : ancestors) {
        auto s = Lower(id);
        if (s.rfind(L"usbstor\\", 0) == 0 || s.rfind(L"usb\\vid_", 0) == 0)
            return true;
    }
    return false;
}
void ParseStorageDescriptor(Disk &d, const std::vector<BYTE> &bytes) {
    // The byte array is variable length; sizeof includes a one-byte placeholder
    // and padding, neither of which is part of the minimum descriptor header.
    if (bytes.size() < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties))
        throw Error(L"设备描述符数据不完整。");
    auto sd = (const STORAGE_DEVICE_DESCRIPTOR *)bytes.data();
    d.reportedBus = sd->BusType;
    auto name =
        Trim(DescriptorText(bytes, sd->VendorIdOffset) + L" " + DescriptorText(bytes, sd->ProductIdOffset));
    if (!name.empty())
        d.friendly = name;
    d.serial = DescriptorText(bytes, sd->SerialNumberOffset);
    d.descriptorKnown = true;
}
void ParseDiskGeometry(Disk &d, const std::vector<BYTE> &bytes) {
    if (bytes.size() < FIELD_OFFSET(DISK_GEOMETRY_EX, Data))
        throw Error(L"容量数据不完整。");
    auto g = (const DISK_GEOMETRY_EX *)bytes.data();
    if (g->DiskSize.QuadPart <= 0 || !g->Geometry.BytesPerSector ||
        g->DiskSize.QuadPart % g->Geometry.BytesPerSector)
        throw Error(L"容量或逻辑扇区数据无效。");
    d.size = g->DiskSize.QuadPart;
    d.sector = g->Geometry.BytesPerSector;
    d.capacityKnown = true;
}
std::wstring LayoutSignature(const Disk &d) {
    std::wstring s = std::to_wstring(d.style) + L":" + d.tableId;
    for (const auto &p : d.partitions) {
        s += L"|" + std::to_wstring(p.number) + L":" + std::to_wstring(p.offset) + L":" +
             std::to_wstring(p.size) + L":" + std::to_wstring(p.mbrType) + L":" + std::to_wstring(p.active);
        if (d.style == PARTITION_STYLE_GPT) {
            wchar_t b[40];
            StringFromGUID2(p.gptType, b, 40);
            s += b;
            StringFromGUID2(p.gptId, b, 40);
            s += b;
        }
    }
    return s;
}
void ParseDriveLayout(Disk &d, const std::vector<BYTE> &bytes) {
    d.layoutKnown = false;
    d.layoutEntries = 0;
    d.partitions.clear();
    d.tableId.clear();
    const size_t header = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
    if (bytes.size() < header)
        throw Error(L"分区信息不完整。");
    auto l = (const DRIVE_LAYOUT_INFORMATION_EX *)bytes.data();
    if (l->PartitionStyle != PARTITION_STYLE_MBR && l->PartitionStyle != PARTITION_STYLE_GPT &&
        l->PartitionStyle != PARTITION_STYLE_RAW)
        throw Error(L"分区表类型无效。");
    d.style = l->PartitionStyle;
    d.layoutEntries = l->PartitionCount;
    if (l->PartitionCount > (bytes.size() - header) / sizeof(PARTITION_INFORMATION_EX))
        throw Error(L"分区计数超出返回数据范围。");
    if (d.style == PARTITION_STYLE_MBR)
        d.tableId = std::to_wstring(l->Mbr.Signature);
    else if (d.style == PARTITION_STYLE_GPT) {
        wchar_t id[40];
        StringFromGUID2(l->Gpt.DiskId, id, 40);
        d.tableId = id;
    }
    std::vector<Partition> partitions;
    for (DWORD i = 0; i < l->PartitionCount; i++) {
        const auto &p = l->PartitionEntry[i];
        // GET_DRIVE_LAYOUT_EX includes unused slots. Their type, rather than
        // leftover length/offset bytes, determines whether they are partitions.
        if (p.PartitionStyle == PARTITION_STYLE_MBR && p.Mbr.PartitionType == PARTITION_ENTRY_UNUSED)
            continue;
        if (p.PartitionStyle == PARTITION_STYLE_GPT && IsEqualGUID(p.Gpt.PartitionType, GUID_NULL))
            continue;
        if (p.PartitionLength.QuadPart == 0)
            continue;
        if (p.PartitionStyle != l->PartitionStyle || d.style == PARTITION_STYLE_RAW ||
            p.PartitionLength.QuadPart < 0 || p.StartingOffset.QuadPart < 0)
            throw Error(L"分区类型、偏移或长度无效。");
        Partition v;
        v.number = p.PartitionNumber;
        v.offset = p.StartingOffset.QuadPart;
        v.size = p.PartitionLength.QuadPart;
        if (d.capacityKnown && (v.offset > d.size || v.size > d.size - v.offset))
            throw Error(L"分区范围超出物理磁盘容量。");
        if (d.style == PARTITION_STYLE_MBR) {
            v.mbrType = p.Mbr.PartitionType;
            v.active = p.Mbr.BootIndicator;
        } else {
            v.gptId = p.Gpt.PartitionId;
            v.gptType = p.Gpt.PartitionType;
        }
        partitions.push_back(v);
    }
    d.partitions = std::move(partitions);
    d.layout = LayoutSignature(d);
    d.layoutKnown = true;
}
bool SameIdentity(const Disk &a, const Disk &b) {
    return a.complete() && b.complete() && a.number == b.number && a.size == b.size && a.sector == b.sector &&
           !a.path.empty() && Lower(a.path) == Lower(b.path) && !a.pnp.empty() &&
           Lower(a.pnp) == Lower(b.pnp) && a.serial == b.serial && a.friendly == b.friendly;
}
std::wstring Disk::Metadata() const {
    std::wstring ls;
    for (const auto &s : letters) {
        if (!ls.empty())
            ls += L" / ";
        ls += s;
    }
    return (numberKnown ? L"磁盘 " + std::to_wstring(number) : L"磁盘编号未确认") + L" · " +
           (capacityKnown ? FormatSize(size) : L"容量未确认") + L" · " +
           (ls.empty() ? L"盘符未确认 / 未分配" : ls) + L" · " +
           (layoutKnown ? L"当前 " + std::to_wstring(partitions.size()) + L" 个分区" : L"分区数未确认");
}
Disk ReadDisk(const std::wstring &path, const std::wstring &pnp, bool includeLayout) {
    Disk d;
    d.path = path;
    d.pnp = pnp;
    d.number = MAXDWORD;
    d.descriptorKnown = d.numberKnown = d.capacityKnown = d.attributesKnown = d.layoutKnown = false;
    d.friendly = NodeName(pnp);
    auto ancestors = Ancestors(pnp);
    bool nodeUsb = IsUsbStorage(BusTypeUnknown, ancestors);
    Handle h(
        CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid()) {
        DWORD e = GetLastError();
        if (nodeUsb) {
            d.usbEvidence = L"USB 设备树";
            d.probeErrors.push_back(L"整盘设备打开失败：" + WinError(e));
            return d;
        }
        throw Error(L"整盘接口打开失败：" + WinError(e));
    }
    Handle read;
    bool triedRead = false;
    auto control = [&](DWORD code, void *in, DWORD inSize, void *out, DWORD outSize, DWORD &n) {
        if (DeviceIoControl(h, code, in, inSize, out, outSize, &n, nullptr))
            return true;
        DWORD e = GetLastError();
        // A few storage drivers require a read-capable handle for queries.
        // This is still read-only and never requests GENERIC_WRITE or elevation.
        if (e == ERROR_ACCESS_DENIED) {
            if (!triedRead) {
                triedRead = true;
                read.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, 0, nullptr));
            }
            if (read.valid()) {
                if (DeviceIoControl(read, code, in, inSize, out, outSize, &n, nullptr))
                    return true;
                e = GetLastError();
            }
        }
        SetLastError(e);
        return false;
    };
    auto query = [&](DWORD code, DWORD initial = 65536) {
        std::vector<BYTE> b(initial);
        DWORD n = 0;
        if (!control(code, nullptr, 0, b.data(), (DWORD)b.size(), n))
            throw Error(WinError());
        if (n > b.size())
            throw Error(L"返回数据超出缓冲区。");
        b.resize(n);
        return b;
    };
    auto probe = [&](const wchar_t *stage, const std::function<void()> &f) {
        try {
            f();
        } catch (const Error &e) {
            d.probeErrors.push_back(std::wstring(stage) + L"：" + e.text);
        }
    };
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> b(16384);
    DWORD n = 0;
    probe(L"存储设备描述符", [&] {
        if (!control(IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, b.data(), (DWORD)b.size(), n))
            throw Error(WinError());
        if (n > b.size())
            throw Error(L"描述符长度无效。");
        b.resize(n);
        ParseStorageDescriptor(d, b);
    });
    if (!IsUsbStorage(d.reportedBus, ancestors)) {
        if (!d.probeErrors.empty())
            throw Error(d.probeErrors.front() + L"；也未能确认 USB 父设备。");
        throw NonUsb(d.reportedBus, d.friendly);
    }
    d.usbEvidence = d.reportedBus == BusTypeUsb
                        ? L"USB 总线报告"
                        : L"USB 父设备链（桥接总线 " + std::to_wstring(d.reportedBus) + L"）";
    probe(L"物理磁盘编号", [&] {
        auto nb = query(IOCTL_STORAGE_GET_DEVICE_NUMBER, sizeof(STORAGE_DEVICE_NUMBER));
        auto nd = (STORAGE_DEVICE_NUMBER *)nb.data();
        if (nb.size() < sizeof(*nd) || nd->DeviceType != FILE_DEVICE_DISK)
            throw Error(L"不能确认物理磁盘编号。");
        d.number = nd->DeviceNumber;
        if (d.number == MAXDWORD)
            throw Error(L"返回了无效磁盘编号。");
        d.numberKnown = true;
    });
    probe(L"容量与扇区", [&] { ParseDiskGeometry(d, query(IOCTL_DISK_GET_DRIVE_GEOMETRY_EX)); });
    probe(L"离线与只读属性", [&] {
        GET_DISK_ATTRIBUTES a{};
        if (!control(IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &a, sizeof a, n))
            throw Error(WinError());
        if (n < sizeof a || a.Version < sizeof a)
            throw Error(L"属性数据不完整。");
        d.offline = (a.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0;
        d.readOnly = (a.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;
        d.attributesKnown = true;
    });
    if (includeLayout)
        probe(L"分区布局", [&] { ParseDriveLayout(d, query(IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 1024 * 1024)); });
    return d;
}
std::vector<Volume> ReadVolumes() {
    std::vector<Volume> result;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE)
        throw Error(L"无法读取卷列表：" + WinError());
    struct CloseVolumes {
        HANDLE find;
        ~CloseVolumes() { FindVolumeClose(find); }
    } closeVolumes{find};
    do {
        Volume v;
        v.guid = name;
        std::wstring bare = v.guid;
        if (bare.back() == L'\\')
            bare.pop_back();
        wchar_t dev[32768];
        if (QueryDosDeviceW(bare.substr(4).c_str(), dev, 32768))
            v.device = dev;
        Handle h(CreateFileW(bare.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (h.valid()) {
            std::vector<BYTE> b(65536);
            DWORD n = 0;
            if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, b.data(),
                                (DWORD)b.size(), &n, nullptr) &&
                n >= FIELD_OFFSET(VOLUME_DISK_EXTENTS, Extents)) {
                auto x = (VOLUME_DISK_EXTENTS *)b.data();
                if (x->NumberOfDiskExtents <=
                    (n - FIELD_OFFSET(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT))
                    for (DWORD i = 0; i < x->NumberOfDiskExtents; i++)
                        v.extents.push_back({x->Extents[i].DiskNumber,
                                             (uint64_t)x->Extents[i].StartingOffset.QuadPart,
                                             (uint64_t)x->Extents[i].ExtentLength.QuadPart});
            }
        }
        DWORD needed = 0;
        GetVolumePathNamesForVolumeNameW(name, nullptr, 0, &needed);
        if (needed) {
            std::vector<wchar_t> paths(needed);
            if (GetVolumePathNamesForVolumeNameW(name, paths.data(), needed, &needed))
                for (auto p = paths.data(); *p; p += wcslen(p) + 1)
                    v.paths.push_back(p);
        }
        result.push_back(v);
    } while (FindNextVolumeW(find, name, MAX_PATH));
    DWORD last = GetLastError();
    if (last != ERROR_NO_MORE_FILES)
        throw Error(L"卷列表枚举不完整：" + WinError(last));
    return result;
}
std::vector<DWORD> PathDisks(const std::wstring &input) {
    std::wstring p = FinalPath(input);
    wchar_t root[32768], guid[MAX_PATH];
    if (!GetVolumePathNameW(p.c_str(), root, 32768) ||
        !GetVolumeNameForVolumeMountPointW(root, guid, MAX_PATH))
        throw Error(L"无法确定路径所在物理磁盘：" + p + L"\n" + WinError());
    for (const auto &v : ReadVolumes())
        if (Lower(v.guid) == Lower(guid)) {
            std::vector<DWORD> r;
            for (auto &x : v.extents)
                r.push_back(x.disk);
            if (!r.empty())
                return r;
            break;
        }
    throw Error(L"路径卷无法映射到物理磁盘，停止写盘：" + p);
}
void AssertOutside(const std::wstring &p, const Disk &d, const std::wstring &why) {
    for (auto n : PathDisks(p))
        if (n == d.number)
            throw Error(why + L"位于目标 U 盘上，停止当前操作。");
}
Catalog ReadDisks() {
    Catalog out;
    std::vector<Volume> volumes;
    try {
        volumes = ReadVolumes();
    } catch (const Error &e) {
        out.diagnostics.push_back(L"卷映射查询失败；继续识别 USB 硬件，禁止据此写盘。" + e.text);
    }
    std::set<DWORD> protectedDisks;
    bool osKnown = false, bootKnown = false;
    wchar_t w[MAX_PATH];
    if (GetWindowsDirectoryW(w, MAX_PATH)) {
        try {
            for (auto n : PathDisks(w))
                protectedDisks.insert(n);
            osKnown = true;
        } catch (const Error &e) {
            out.diagnostics.push_back(e.text);
        }
    }
    wchar_t boot[32768];
    DWORD cb = sizeof boot;
    LONG rc = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition", RRF_RT_REG_SZ, nullptr,
                           boot, &cb);
    if (rc == ERROR_SUCCESS) {
        for (const auto &v : volumes)
            if (Lower(v.device) == Lower(boot) && !v.extents.empty()) {
                for (auto &e : v.extents)
                    protectedDisks.insert(e.disk);
                bootKnown = true;
            }
    }
    out.bootKnown = osKnown && bootKnown;
    if (!out.bootKnown)
        out.diagnostics.push_back(L"启动分区映射未完整确认；继续显示 USB 硬件信息，但禁止写盘。");
    HDEVINFO list = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE)
        throw Error(L"设备接口枚举失败：" + WinError());
    struct CloseDevices {
        HDEVINFO list;
        ~CloseDevices() { SetupDiDestroyDeviceInfoList(list); }
    } closeDevices{list};
    for (DWORD i = 0;; i++) {
        SP_DEVICE_INTERFACE_DATA di{};
        di.cbSize = sizeof di;
        if (!SetupDiEnumDeviceInterfaces(list, nullptr, &GUID_DEVINTERFACE_DISK, i, &di)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                out.diagnostics.push_back(L"磁盘接口列表枚举不完整。");
            break;
        }
        out.interfaces++;
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(list, &di, nullptr, 0, &need, nullptr);
        if (!need) {
            out.failed++;
            out.diagnostics.push_back(L"设备接口 " + std::to_wstring(i + 1) + L" 的查询长度失败：" +
                                      WinError());
            continue;
        }
        std::vector<BYTE> b(need);
        auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)b.data();
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof dev;
        if (!SetupDiGetDeviceInterfaceDetailW(list, &di, detail, need, nullptr, &dev)) {
            out.failed++;
            out.diagnostics.push_back(L"设备接口 " + std::to_wstring(i + 1) + L" 的读取失败：" + WinError());
            continue;
        }
        wchar_t id[MAX_DEVICE_ID_LEN];
        std::wstring pnp;
        if (CM_Get_Device_IDW(dev.DevInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
            pnp = id;
        try {
            Disk d = ReadDisk(detail->DevicePath, pnp);
            d.system = protectedDisks.count(d.number) != 0;
            d.bootKnown = out.bootKnown;
            for (auto &v : volumes)
                for (auto &e : v.extents)
                    if (d.numberKnown && e.disk == d.number)
                        for (auto &p : v.paths)
                            if (p.size() == 3)
                                d.letters.push_back(p);
            if (d.system)
                d.blocked = L"系统或启动磁盘";
            else if (!d.complete())
                d.blocked = L"设备查询不完整，仅可查看，不能制作";
            else if (d.pnp.empty())
                d.blocked = L"设备身份无法确认";
            else if (d.offline)
                d.blocked = L"磁盘离线";
            else if (d.readOnly)
                d.blocked = L"磁盘只读";
            else if (d.size < 8 * GiB)
                d.blocked = L"容量不足 8 GiB";
            else if (d.size > 2 * 1024 * GiB)
                d.blocked = L"容量超过 2 TiB";
            else if (d.sector != 512 && d.sector != 4096)
                d.blocked = L"逻辑扇区大小不受支持";
            else if (!osKnown)
                d.blocked = L"系统盘映射无法确认";
            else if (!bootKnown)
                d.blocked = L"启动分区映射未确认，暂不允许制作";
            out.disks.push_back(d);
        } catch (const NonUsb &e) {
            out.nonUsb++;
            out.diagnostics.push_back(L"设备接口 " + std::to_wstring(i + 1) + L"：" + e.text);
        } catch (const Error &e) {
            out.failed++;
            out.diagnostics.push_back(L"设备接口 " + std::to_wstring(i + 1) + L"：" + e.text);
        }
    }
    out.diagnostics.insert(out.diagnostics.begin(),
                           L"设备扫描：" + std::to_wstring(out.interfaces) + L" 个磁盘接口，" +
                               std::to_wstring(out.disks.size()) + L" 个 USB 整盘，" +
                               std::to_wstring(out.nonUsb) + L" 个非 USB，" + std::to_wstring(out.failed) +
                               L" 个接口未完成查询。" +
                               (IsAdmin() ? L"当前为管理员权限。" : L"当前为普通权限。"));
    std::sort(out.disks.begin(), out.disks.end(),
              [](const Disk &a, const Disk &b) { return a.number < b.number; });
    return out;
}
Disk RecheckDisk(const Disk &expected, bool layout) {
    auto c = ReadDisks();
    for (auto &d : c.disks)
        if (d.number == expected.number) {
            if (!d.eligible() || !SameIdentity(expected, d))
                throw Error(L"目标设备身份或可写状态变化，停止后续操作。");
            if (layout && d.layout != expected.layout)
                throw Error(L"确认后分区布局变化，尚未清盘。");
            return d;
        }
    throw Error(L"目标 USB 磁盘已移除。");
}
std::wstring DiskJson(const Catalog &c) {
    std::wstring s = L"{\"implementation\":\"C++/"
                     L"Win32\",\"read_only\":true,\"dotnet_required\":false,\"powershell_required\":false,"
                     L"\"application_version\":\"0.2.0-preview.13\",\"administrator\":" +
                     std::wstring(IsAdmin() ? L"true" : L"false") + L",\"interface_count\":" +
                     std::to_wstring(c.interfaces) + L",\"non_usb_count\":" + std::to_wstring(c.nonUsb) +
                     L",\"failed_interface_count\":" + std::to_wstring(c.failed) +
                     L",\"boot_mapping_confirmed\":" + std::wstring(c.bootKnown ? L"true" : L"false") +
                     L",\"disks\":[";
    bool first = true;
    for (auto &d : c.disks) {
        if (!first)
            s += L",";
        first = false;
        s += L"{\"number\":" + (d.numberKnown ? std::to_wstring(d.number) : L"null") + L",\"name\":" +
             Json(d.friendly) + L",\"size_bytes\":" + (d.capacityKnown ? std::to_wstring(d.size) : L"null") +
             L",\"logical_sector\":" + (d.capacityKnown ? std::to_wstring(d.sector) : L"null") +
             L",\"partition_count\":" + (d.layoutKnown ? std::to_wstring(d.partitions.size()) : L"null") +
             L",\"layout_entries\":" + std::to_wstring(d.layoutEntries) + L",\"layout_known\":" +
             (d.layoutKnown ? L"true" : L"false") + L",\"descriptor_known\":" +
             (d.descriptorKnown ? L"true" : L"false") + L",\"attributes_known\":" +
             (d.attributesKnown ? L"true" : L"false") + L",\"eligible\":" +
             (d.eligible() ? L"true" : L"false") + L",\"blocked_reason\":" + Json(d.blocked) +
             L",\"summary\":" + Json(d.Metadata()) + L",\"reported_bus\":" + std::to_wstring(d.reportedBus) +
             L",\"usb_evidence\":" + Json(d.usbEvidence) + L",\"probe_errors\":[";
        bool firstError = true;
        for (const auto &e : d.probeErrors) {
            if (!firstError)
                s += L",";
            firstError = false;
            s += Json(e);
        }
        s += L"]}";
    }
    s += L"],\"diagnostics\":[";
    first = true;
    for (auto &e : c.diagnostics) {
        if (!first)
            s += L",";
        first = false;
        s += Json(e);
    }
    return s + L"]}";
}
} // namespace wu
