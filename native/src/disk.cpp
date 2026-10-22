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
std::vector<BYTE> Query(HANDLE h, DWORD ctl, DWORD initial = 65536) {
    std::vector<BYTE> b(initial);
    DWORD n = 0;
    if (!DeviceIoControl(h, ctl, nullptr, 0, b.data(), (DWORD)b.size(), &n, nullptr))
        throw Error(L"磁盘信息读取失败：" + WinError());
    b.resize(n);
    return b;
}
bool IsEfi(const Partition &p) {
    static const GUID efi = {0xc12a7328, 0xf81f, 0x11d2, {0xba, 0x4b, 0, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b}};
    return IsEqualGUID(p.gptType, efi) || p.mbrType == 0xef;
}
} // namespace
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
bool SameIdentity(const Disk &a, const Disk &b) {
    return a.number == b.number && a.size == b.size && a.sector == b.sector && !a.path.empty() &&
           Lower(a.path) == Lower(b.path) && !a.pnp.empty() && Lower(a.pnp) == Lower(b.pnp) &&
           a.serial == b.serial && a.friendly == b.friendly;
}
std::wstring Disk::Metadata() const {
    std::wstring ls;
    for (const auto &s : letters) {
        if (!ls.empty())
            ls += L" / ";
        ls += s;
    }
    return L"磁盘 " + std::to_wstring(number) + L" · " + FormatSize(size) + L" · " +
           (ls.empty() ? L"未分配盘符" : ls) + L" · 当前 " + std::to_wstring(partitions.size()) + L" 个分区";
}
Disk ReadDisk(const std::wstring &path, const std::wstring &pnp) {
    Handle h(
        CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid())
        throw Error(L"无法读取整盘设备：" + WinError());
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> b(16384);
    DWORD n = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, b.data(), (DWORD)b.size(), &n,
                         nullptr) ||
        n < sizeof(STORAGE_DEVICE_DESCRIPTOR))
        throw Error(L"设备身份查询失败：" + WinError());
    b.resize(n);
    auto sd = (STORAGE_DEVICE_DESCRIPTOR *)b.data();
    if (sd->BusType != BusTypeUsb)
        throw Error(L"此设备不是 USB 总线磁盘。");
    Disk d;
    d.path = path;
    d.pnp = pnp;
    d.friendly = Trim(DescriptorText(b, sd->VendorIdOffset) + L" " + DescriptorText(b, sd->ProductIdOffset));
    d.serial = DescriptorText(b, sd->SerialNumberOffset);
    if (d.friendly.empty())
        d.friendly = L"USB 磁盘";
    auto nb = Query(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, sizeof(STORAGE_DEVICE_NUMBER));
    auto nd = (STORAGE_DEVICE_NUMBER *)nb.data();
    if (nb.size() < sizeof(*nd) || nd->DeviceType != FILE_DEVICE_DISK)
        throw Error(L"不能确认物理磁盘编号。");
    d.number = nd->DeviceNumber;
    auto gb = Query(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX);
    if (gb.size() < sizeof(DISK_GEOMETRY_EX))
        throw Error(L"不能确认磁盘容量。");
    auto g = (DISK_GEOMETRY_EX *)gb.data();
    d.size = g->DiskSize.QuadPart;
    d.sector = g->Geometry.BytesPerSector;
    GET_DISK_ATTRIBUTES a{};
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &a, sizeof a, &n, nullptr))
        throw Error(L"不能确认离线和只读状态：" + WinError());
    d.offline = (a.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0;
    d.readOnly = (a.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;
    auto lb = Query(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 1024 * 1024);
    if (lb.size() < FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry))
        throw Error(L"分区信息不完整。");
    auto l = (DRIVE_LAYOUT_INFORMATION_EX *)lb.data();
    d.style = l->PartitionStyle;
    if (d.style == PARTITION_STYLE_MBR)
        d.tableId = std::to_wstring(l->Mbr.Signature);
    else if (d.style == PARTITION_STYLE_GPT) {
        wchar_t id[40];
        StringFromGUID2(l->Gpt.DiskId, id, 40);
        d.tableId = id;
    }
    if (l->PartitionCount > (lb.size() - FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) /
                                sizeof(PARTITION_INFORMATION_EX))
        throw Error(L"分区计数超出返回数据范围。");
    for (DWORD i = 0; i < l->PartitionCount; i++) {
        auto &p = l->PartitionEntry[i];
        if (p.PartitionLength.QuadPart <= 0 || p.StartingOffset.QuadPart < 0)
            continue;
        Partition v;
        v.number = p.PartitionNumber;
        v.offset = p.StartingOffset.QuadPart;
        v.size = p.PartitionLength.QuadPart;
        if (p.PartitionStyle == PARTITION_STYLE_MBR) {
            v.mbrType = p.Mbr.PartitionType;
            v.active = p.Mbr.BootIndicator;
        } else if (p.PartitionStyle == PARTITION_STYLE_GPT) {
            v.gptId = p.Gpt.PartitionId;
            v.gptType = p.Gpt.PartitionType;
        }
        d.partitions.push_back(v);
    }
    d.layout = LayoutSignature(d);
    return d;
}
std::vector<Volume> ReadVolumes() {
    std::vector<Volume> result;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE)
        throw Error(L"无法读取卷列表：" + WinError());
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
    FindVolumeClose(find);
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
            throw Error(why + L"位于目标 U 盘上，尚未清盘。");
}
Catalog ReadDisks() {
    Catalog out;
    auto volumes = ReadVolumes();
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
        out.diagnostics.push_back(L"启动分区映射未完整确认；包含活动/EFI 分区的 USB 盘不会被允许重建。");
    HDEVINFO list = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE)
        throw Error(L"设备接口枚举失败：" + WinError());
    for (DWORD i = 0;; i++) {
        SP_DEVICE_INTERFACE_DATA di{};
        di.cbSize = sizeof di;
        if (!SetupDiEnumDeviceInterfaces(list, nullptr, &GUID_DEVINTERFACE_DISK, i, &di)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                out.diagnostics.push_back(L"磁盘接口列表枚举不完整。");
            break;
        }
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(list, &di, nullptr, 0, &need, nullptr);
        if (!need)
            continue;
        std::vector<BYTE> b(need);
        auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)b.data();
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof dev;
        if (!SetupDiGetDeviceInterfaceDetailW(list, &di, detail, need, nullptr, &dev))
            continue;
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
                    if (e.disk == d.number)
                        for (auto &p : v.paths)
                            if (p.size() == 3)
                                d.letters.push_back(p);
            if (d.system)
                d.blocked = L"系统或启动磁盘";
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
            else if (!bootKnown && std::any_of(d.partitions.begin(), d.partitions.end(),
                                               [](const Partition &p) { return p.active || IsEfi(p); }))
                d.blocked = L"无法排除启动分区，请先修复系统启动分区映射";
            out.disks.push_back(d);
        } catch (const Error &e) {
            if (e.text != L"此设备不是 USB 总线磁盘。")
                out.diagnostics.push_back(e.text);
        }
    }
    SetupDiDestroyDeviceInfoList(list);
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
                     L"\"boot_mapping_confirmed\":" +
                     std::wstring(c.bootKnown ? L"true" : L"false") + L",\"disks\":[";
    bool first = true;
    for (auto &d : c.disks) {
        if (!first)
            s += L",";
        first = false;
        s += L"{\"number\":" + std::to_wstring(d.number) + L",\"name\":" + Json(d.friendly) +
             L",\"size_bytes\":" + std::to_wstring(d.size) + L",\"logical_sector\":" +
             std::to_wstring(d.sector) + L",\"partition_count\":" + std::to_wstring(d.partitions.size()) +
             L",\"eligible\":" + (d.eligible() ? L"true" : L"false") + L",\"blocked_reason\":" +
             Json(d.blocked) + L",\"summary\":" + Json(d.Metadata()) + L"}";
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
