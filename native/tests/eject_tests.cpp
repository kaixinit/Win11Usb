#include "eject.hpp"
#include "platform.hpp"
#include <algorithm>
#include <devguid.h>
#include <iostream>
#include <objbase.h>
#include <setupapi.h>

using namespace wu;
namespace {
int checks = 0;
void Require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::wstring Reject(F function, const char *message) {
    try {
        function();
    } catch (const Error &e) {
        ++checks;
        return e.text;
    }
    Require(false, message);
    return {};
}
std::wstring ClassText(const GUID &guid) {
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, 40))
        throw std::runtime_error("SDK class GUID conversion failed");
    return text;
}
void Classify(EjectNode &node, const GUID &guid) {
    node.classGuid = ClassText(guid);
    node.kind = ClassifyEjectClass(node.classGuid);
}
std::optional<DEVINST> NativeParent(DEVINST id) {
    DEVINST parent;
    auto code = CM_Get_Parent(&parent, id, 0);
    if (code == CR_NO_SUCH_DEVNODE)
        return {};
    if (code != CR_SUCCESS)
        throw Error(L"read-only ancestor query failed");
    return parent;
}
std::vector<DEVINST> NativeChildren(DEVINST id) {
    std::vector<DEVINST> result;
    DEVINST child;
    auto code = CM_Get_Child(&child, id, 0);
    if (code == CR_NO_SUCH_DEVNODE)
        return result;
    if (code != CR_SUCCESS)
        throw Error(L"read-only child query failed");
    for (;;) {
        if (result.size() >= 256 || std::find(result.begin(), result.end(), child) != result.end())
            throw Error(L"read-only child query exceeded tree limit");
        result.push_back(child);
        DEVINST sibling;
        code = CM_Get_Sibling(&sibling, child, 0);
        if (code == CR_NO_SUCH_DEVNODE)
            return result;
        if (code != CR_SUCCESS)
            throw Error(L"read-only sibling query failed");
        child = sibling;
    }
}
struct CloseDevices {
    HDEVINFO devices;
    ~CloseDevices() { SetupDiDestroyDeviceInfoList(devices); }
};
struct Fixture {
    std::map<DEVINST, EjectNode> nodes;
    std::map<DEVINST, DEVINST> parents;
    std::map<DEVINST, std::vector<DEVINST>> children;
    EjectOps ops;
    std::atomic<bool> stop{false};
    std::vector<DEVINST> requests;
    unsigned prepared = 0, validated = 0, waits = 0;
    std::vector<EjectReply> replies;
    Fixture() {
        nodes[1] = {1, L"USBSTOR\\DISK&VEN_TEST\\own", L"disk", EjectKind::Disk, 0, true};
        nodes[2] = {2,   L"USB\\VID_1234&PID_5678\\own", L"USBSTOR", EjectKind::Usb, CM_DEVCAP_REMOVABLE,
                    true};
        nodes[3] = {3, L"USB\\ROOT_HUB30\\hub", L"USBHUB3", EjectKind::Usb, CM_DEVCAP_REMOVABLE, true};
        // Raw setup-class GUIDs enter through the same classifier as the CM
        // adapter. A preassigned Usb enum cannot hide a wrong GUID mapping.
        Classify(nodes[1], GUID_DEVCLASS_DISKDRIVE);
        Classify(nodes[2], GUID_DEVCLASS_USB);
        Classify(nodes[3], GUID_DEVCLASS_USB);
        parents[1] = 2;
        parents[2] = 3;
        children[2] = {1};
        children[3] = {2};
        ops.read = [&](DEVINST id) { return nodes.at(id); };
        ops.parent = [&](DEVINST id) -> std::optional<DEVINST> {
            if (parents.count(id))
                return parents.at(id);
            return {};
        };
        ops.children = [&](DEVINST id) { return children[id]; };
        ops.validate = [&] { ++validated; };
        ops.prepare = [&] { ++prepared; };
        ops.request = [&](DEVINST id) {
            requests.push_back(id);
            Require(prepared == requests.size() && validated >= prepared,
                    "validate and close volume handles before each PnP request");
            return requests.size() <= replies.size() ? replies[requests.size() - 1] : EjectReply{};
        };
        ops.wait = [&](DWORD milliseconds) {
            Require(milliseconds == 500, "bounded eject retry delay");
            ++waits;
        };
    }
    void Run() { EjectUsbChecked(1, L"USBSTOR\\DISK&VEN_TEST\\own", ops, stop); }
};
} // namespace
int EjectTests(const std::wstring &root) {
    static_assert(PNP_VetoIllegalDeviceRequest == 8 && PNP_VetoOutstandingOpen == 5 &&
                      CR_REMOVE_VETOED == 23,
                  "SDK values from the user's report");
    for (const auto &[guid, expected] : std::vector<std::pair<const GUID *, EjectKind>>{
             {&GUID_DEVCLASS_DISKDRIVE, EjectKind::Disk},
             {&GUID_DEVCLASS_SCSIADAPTER, EjectKind::Storage},
             {&GUID_DEVCLASS_HDC, EjectKind::Storage},
             {&GUID_DEVCLASS_USB, EjectKind::Usb},
             {&GUID_DEVCLASS_SYSTEM, EjectKind::Other},
             {&GUID_DEVCLASS_VOLUME, EjectKind::Volume},
             {&GUID_DEVCLASS_VOLUMESNAPSHOT, EjectKind::Volume}}) {
        auto text = ClassText(*guid);
        Require(ClassifyEjectClass(text) == expected, "SDK setup-class GUID maps to correct eject kind");
        Require(ClassifyEjectClass(Lower(text)) == expected, "GUID classification is case-insensitive");
    }
    const GUID unknown = {0xc7f366c2, 0x6eb6, 0x471c, {0xa5, 0xe7, 0x4b, 0x72, 0x66, 0x99, 0xc0, 0x2a}};
    Require(ClassifyEjectClass(ClassText(unknown)) == EjectKind::Other,
            "well-formed unknown class cannot authorize storage removal");
    for (const auto &bad : {L"", L"USB", L"not-a-guid", L"{36fc9e60-c465-11cf-8056-44455354000G}",
                            L"{36fc9e60-c465-11cf-8056-444553540000}suffix"})
        Reject([&] { ClassifyEjectClass(bad); }, "malformed or missing class GUID is rejected");
    {
        Fixture f;
        auto plan = PlanUsbEject(1, f.nodes[1].instance, f.ops);
        Require(plan.size() == 1 && plan[0].id == 2,
                "skip non-removable disk child; select own USB storage parent");
        f.Run();
        Require(f.requests == std::vector<DEVINST>{2}, "never request hub eject");
    }
    {
        Fixture f;
        f.nodes[1].instance = L"SCSI\\DISK&VEN_TEST&PROD_UASP\\own";
        f.nodes[2].service = L"UASPStor";
        Classify(f.nodes[2], GUID_DEVCLASS_SCSIADAPTER);
        auto plan = PlanUsbEject(1, f.nodes[1].instance, f.ops);
        Require(plan.size() == 1 && plan[0].id == 2 && plan[0].kind == EjectKind::Storage,
                "UASP adapter class retains independently checked physical USB boundary");
        std::atomic<bool> stop = false;
        EjectUsbChecked(1, f.nodes[1].instance, f.ops, stop);
        Require(f.requests == std::vector<DEVINST>{2}, "UASP request only reaches own storage parent");
    }
    {
        Fixture f;
        Classify(f.nodes[2], GUID_DEVCLASS_SYSTEM);
        Require(f.nodes[2].kind == EjectKind::Other,
                "System setup class is never relabeled as USB even with USBSTOR service");
        Reject([&] { f.Run(); }, "System-class parent cannot authorize storage ejection");
        Require(f.requests.empty() && f.prepared == 0,
                "misleading USBSTOR service cannot bypass unknown class boundary");
    }
    {
        Fixture f;
        f.nodes[1].capabilities = CM_DEVCAP_REMOVABLE;
        f.replies = {{CR_REMOVE_VETOED, PNP_VetoIllegalDeviceRequest, L"private-instance"}, {}};
        f.Run();
        Require(f.requests == std::vector<DEVINST>({1, 2}),
                "reported veto8 falls back only to checked USB parent");
    }
    {
        Fixture f;
        f.nodes[1].capabilities = CM_DEVCAP_REMOVABLE;
        f.Run();
        Require(f.requests == std::vector<DEVINST>{1},
                "supported removable disk child does not require parent eject");
    }
    {
        Fixture f;
        f.replies = {{CR_REMOVE_VETOED, PNP_VetoOutstandingOpen, L""}, {}};
        f.Run();
        Require(f.requests == std::vector<DEVINST>({2, 2}) && f.waits == 1,
                "temporary outstanding handle retries same checked node");
    }
    for (auto veto : {PNP_VetoOutstandingOpen, PNP_VetoPendingClose}) {
        Fixture f;
        f.nodes[1].capabilities = CM_DEVCAP_REMOVABLE;
        f.replies.assign(3, {CR_REMOVE_VETOED, veto, L""});
        auto text = Reject([&] { f.Run(); }, "persistent busy eject stops");
        Require(f.requests == std::vector<DEVINST>({1, 1, 1}) && f.waits == 2,
                "busy retries are bounded and cannot escalate to parent");
        Require(text.find(veto == PNP_VetoOutstandingOpen ? L"句柄" : L"等待关闭") != text.npos,
                "busy and pending-close explanations differ");
    }
    for (auto veto : {PNP_VetoWindowsApp, PNP_VetoWindowsService, PNP_VetoDriver, PNP_VetoNonDisableable,
                      PNP_VetoInsufficientRights, PNP_VetoTypeUnknown}) {
        Fixture f;
        f.nodes[1].capabilities = CM_DEVCAP_REMOVABLE;
        f.replies = {{CR_REMOVE_VETOED, veto, L"blocker"}};
        Reject([&] { f.Run(); }, "non-transient veto is reported");
        Require(f.requests == std::vector<DEVINST>{1} && f.waits == 0,
                "application, service, driver, rights and unknown veto cannot trigger parent fallback");
    }
    {
        Fixture f;
        f.nodes[4] = {4,   L"USBSTOR\\DISK&VEN_OTHER\\other", L"disk", EjectKind::Disk, CM_DEVCAP_REMOVABLE,
                      true};
        f.children[2].push_back(4);
        Reject([&] { f.Run(); }, "shared multi-LUN USB parent rejected");
        Require(f.requests.empty() && f.prepared == 0,
                "no volume operation when parent includes another disk");
        f.nodes[1].capabilities = CM_DEVCAP_REMOVABLE;
        f.replies = {{CR_REMOVE_VETOED, PNP_VetoIllegalDeviceRequest, L""}};
        Reject([&] { f.Run(); }, "unsupported single-LUN request cannot fall back to shared parent");
        Require(f.requests == std::vector<DEVINST>{1}, "other LUN never included by fallback");
    }
    for (auto service : {L"USBHUB3", L"usbccgp", L"USBXHCI", L"VendorHub", L"WinUSB", L""}) {
        Fixture f;
        f.nodes[2].service = service;
        Reject([&] { f.Run(); }, "hub, controller or composite USB boundary rejected");
        Require(f.requests.empty(), "no request to hub/controller/composite device");
    }
    {
        Fixture f;
        f.nodes[4] = {4, L"HID\\VID_OTHER\\keyboard", L"kbdhid", EjectKind::Other, 0, true};
        f.children[2].push_back(4);
        Reject([&] { f.Run(); }, "other USB function cannot be removed with selected disk");
        Require(f.requests.empty(), "no request for mixed-function USB subtree");
    }
    for (auto capabilities :
         {CM_DEVCAP_DOCKDEVICE | CM_DEVCAP_REMOVABLE, CM_DEVCAP_NONDYNAMIC | CM_DEVCAP_REMOVABLE, 0}) {
        Fixture f;
        f.nodes[2].capabilities = capabilities;
        Reject([&] { f.Run(); }, "non-removable, dock and non-dynamic candidates rejected");
        Require(f.requests.empty(), "unsupported capability does not reach PnP request");
    }
    {
        Fixture f;
        f.nodes[2].capabilitiesKnown = false;
        Reject([&] { f.Run(); }, "missing capabilities cannot authorize eject");
        Require(f.requests.empty(), "incomplete capability query causes no removal request");
    }
    {
        Fixture f;
        f.nodes[1].instance += L"-replaced";
        Reject([&] { f.Run(); }, "changed original disk instance stops eject");
        Require(f.requests.empty(), "wrong disk identity cannot reach volume or eject operation");
    }
    {
        Fixture f;
        f.parents[1] = 1;
        Reject([&] { f.Run(); }, "ancestor cycle rejected");
        Require(f.requests.empty(), "no removal for incomplete/cyclic ancestry");
    }
    {
        Fixture f;
        f.children[2].push_back(2);
        Reject([&] { f.Run(); }, "subtree cycle rejected");
        Require(f.requests.empty(), "no removal for cyclic subtree");
    }
    {
        Fixture f;
        f.ops.children = [](DEVINST) -> std::vector<DEVINST> { throw Error(L"query failure"); };
        Reject([&] { f.Run(); }, "failed subtree query rejected");
        Require(f.requests.empty(), "failed subtree query cannot authorize removal");
    }
    {
        Fixture f;
        f.ops.validate = [&] {
            if (++f.validated == 2)
                f.nodes[2].instance += L"-changed";
        };
        Reject([&] { f.Run(); }, "candidate identity changes after planning");
        Require(f.requests.empty() && f.prepared == 0, "replaced candidate stops before volume handoff");
    }
    {
        Fixture f;
        f.ops.validate = [&] { throw Error(L"new disk identity"); };
        Reject([&] { f.Run(); }, "failed physical identity check stops eject");
        Require(f.requests.empty() && f.prepared == 0, "physical mismatch reaches no volume operation");
    }
    {
        Fixture f;
        f.ops.prepare = [] { throw Error(L"flush failed"); };
        Reject([&] { f.Run(); }, "failed cache flush stops eject");
        Require(f.requests.empty(), "flush/lock failure reaches no PnP request");
    }
    {
        Fixture f;
        f.stop = true;
        Reject([&] { f.Run(); }, "cancel before eject");
        Require(f.validated == 0 && f.requests.empty(), "pre-cancelled eject touches no device");
    }
    {
        Fixture f;
        f.replies = {{CR_REMOVE_VETOED, PNP_VetoPendingClose, L""}};
        f.ops.wait = [&](DWORD) { f.stop = true; };
        Reject([&] { f.Run(); }, "cancel during retry");
        Require(f.requests.size() == 1, "cancelled retry cannot start another removal");
    }
    {
        Fixture f;
        auto file = root + L"\\eject-handoff.bin";
        WriteUtf8(file, L"test cache handoff");
        f.ops.prepare = [&] {
            Handle handle(CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      0, nullptr));
            Require(handle.valid() && FlushFileBuffers(handle),
                    "prepare holds and flushes real exclusive file");
            ++f.prepared;
        };
        f.ops.request = [&](DEVINST id) {
            Handle exclusive(CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                                         OPEN_EXISTING, 0, nullptr));
            Require(exclusive.valid(),
                    "prepare returns with actual file handle released before mock PnP request");
            f.requests.push_back(id);
            return EjectReply{};
        };
        f.Run();
    }
    auto unsupported = EjectFailure({23, (PNP_VETO_TYPE)8, L"USB\\SERIAL_PRIVATE"});
    Require(unsupported.find(L"不支持") != unsupported.npos &&
                unsupported.find(L"句柄") == unsupported.npos &&
                unsupported.find(L"SERIAL_PRIVATE") == unsupported.npos,
            "reported veto8 is unsupported operation and reveals no instance/serial");
    auto busy = EjectFailure({23, (PNP_VETO_TYPE)5, L"USB\\SERIAL_PRIVATE"});
    Require(busy.find(L"打开的句柄") != busy.npos, "veto5 correctly identifies outstanding handles");
    auto driver = EjectFailure({23, PNP_VetoDriver, L"C:\\private-user\\driver.sys"});
    Require(driver.find(L"驱动拒绝") != driver.npos && driver.find(L"driver.sys") != driver.npos &&
                driver.find(L"private-user") == driver.npos,
            "driver hint has useful basename without user path");
    auto denied = EjectFailure({CR_ACCESS_DENIED, PNP_VetoTypeUnknown, L""});
    Require(denied.find(L"权限不足") != denied.npos && denied.find(L"句柄") == denied.npos,
            "configuration-manager access denied is not mislabeled as busy");
    {
        HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
        Require(devices != INVALID_HANDLE_VALUE, "read-only actual USB setup-class enumeration");
        CloseDevices close{devices};
        unsigned usbNodes = 0;
        for (DWORD index = 0;; ++index) {
            SP_DEVINFO_DATA data{};
            data.cbSize = sizeof data;
            if (!SetupDiEnumDeviceInfo(devices, index, &data)) {
                Require(GetLastError() == ERROR_NO_MORE_ITEMS, "actual USB node enumeration is complete");
                break;
            }
            auto node = ReadEjectNode(data.DevInst);
            Require(node.kind == EjectKind::Usb && !node.instance.empty(),
                    "actual native USB setup-class adapter cannot confuse USB with System");
            Require(Lower(node.classGuid) == Lower(ClassText(GUID_DEVCLASS_USB)),
                    "actual native adapter retains USB class GUID from CM property");
            ++usbNodes;
        }
        if (usbNodes)
            std::cout << "PASS actual read-only USB-class metadata: " << usbNodes
                      << " nodes; no removal requested\n";
        else
            std::cout << "NOT RUN actual USB-class metadata: no present USB setup-class nodes\n";
    }
    {
        HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISKDRIVE, nullptr, nullptr, DIGCF_PRESENT);
        Require(devices != INVALID_HANDLE_VALUE, "read-only native disk metadata fixture enumeration");
        CloseDevices close{devices};
        unsigned disks = 0, usbStorageDisks = 0, planned = 0;
        for (DWORD index = 0;; ++index) {
            SP_DEVINFO_DATA data{};
            data.cbSize = sizeof data;
            if (!SetupDiEnumDeviceInfo(devices, index, &data)) {
                Require(GetLastError() == ERROR_NO_MORE_ITEMS, "actual disk node enumeration is complete");
                break;
            }
            auto node = ReadEjectNode(data.DevInst);
            Require(node.kind == EjectKind::Disk && !node.instance.empty(),
                    "actual native CM registry class/instance adapter reads an existing disk");
            if (!disks++) {
                Disk disk;
                disk.pnp = node.instance;
                bool validated = false, prepared = false;
                std::atomic<bool> stop = false;
                Reject(
                    [&] {
                        EjectUsbNative(
                            disk,
                            [&] {
                                validated = true;
                                throw Error(L"read-only fixture stop");
                            },
                            [&] { prepared = true; }, stop);
                    },
                    "read-only native CM locator reaches mandatory identity guard");
                Require(validated && !prepared, "native adapter cannot eject when identity guard stops it");
            }
            EjectOps metadata;
            metadata.read = ReadEjectNode;
            metadata.parent = NativeParent;
            metadata.children = NativeChildren;
            auto forbidden = [] {
                throw std::runtime_error("read-only native planner invoked device action");
            };
            metadata.validate = forbidden;
            metadata.prepare = forbidden;
            metadata.request = [&](DEVINST) -> EjectReply {
                forbidden();
                return {};
            };
            metadata.wait = [&](DWORD) { forbidden(); };
            std::set<DEVINST> seen;
            auto current = std::optional<DEVINST>(data.DevInst);
            try {
                for (unsigned depth = 0; current && depth < 32; ++depth) {
                    if (!seen.insert(*current).second)
                        throw Error(L"read-only native ancestor cycle");
                    auto ancestor = metadata.read(*current);
                    auto instance = Lower(ancestor.instance), service = Lower(ancestor.service);
                    auto hardware = instance.substr(0, instance.find(L'\\', 4));
                    bool physicalStorage = hardware.rfind(L"usb\\vid_", 0) == 0 &&
                                           hardware.find(L"&pid_") != hardware.npos &&
                                           hardware.find(L"&mi_") == hardware.npos &&
                                           (service == L"usbstor" || service == L"uaspstor");
                    if (physicalStorage) {
                        ++usbStorageDisks;
                        auto plan = PlanUsbEject(data.DevInst, node.instance, metadata);
                        Require(!plan.empty(),
                                "actual isolated USB storage yields a checked read-only plan");
                        for (const auto &candidate : plan)
                            Require(candidate.id == data.DevInst || candidate.id == ancestor.id ||
                                        candidate.kind == EjectKind::Storage,
                                    "actual plan remains inside selected USB storage ancestry");
                        ++planned;
                        break;
                    }
                    if (ancestor.kind == EjectKind::Other)
                        break;
                    current = metadata.parent(*current);
                }
            } catch (const Error &) {
                // Conservative scope/capability guards can legitimately reject
                // a present multi-LUN or composite device. Do not claim it passed.
                std::cout << "NOT RUN actual USB storage plan for disk index " << index
                          << ": native metadata or conservative scope/capability guard rejected\n";
            }
        }
        if (!disks)
            std::cout << "NOT RUN native CM metadata fixture: no disk class device\n";
        else
            std::cout << "PASS actual read-only CM disk metadata/locator: " << disks
                      << " disks; no removal requested\n";
        if (!usbStorageDisks)
            std::cout
                << "NOT RUN actual USB storage ancestor plan: no confirmed USBSTOR/UASP physical parent\n";
        else if (planned)
            std::cout << "PASS actual read-only USB storage ancestor plan: " << planned << " of "
                      << usbStorageDisks << " physical storage parents; NO eject API requested\n";
    }
    {
        Catalog empty;
        auto report = UsbEjectReport(empty);
        Require(report.find(L"未提交任何安全弹出请求") != report.npos,
                "empty eject diagnostic explicitly reports no device removal attempt");
        Catalog blocked;
        Disk disk;
        disk.number = 3;
        disk.size = 62008590336;
        disk.serial = L"PRIVATE_SERIAL";
        disk.path = L"PRIVATE_DEVICE_PATH";
        disk.friendly = L"Fixture PRIVATE_SERIAL PRIVATE_DEVICE_PATH";
        disk.blocked = L"Fixture restriction PRIVATE_SERIAL";
        blocked.disks.push_back(disk);
        report = UsbEjectReport(blocked);
        Require(report.find(L"PRIVATE_SERIAL") == report.npos &&
                    report.find(L"PRIVATE_DEVICE_PATH") == report.npos,
                "read-only eject diagnostics redact private identity even from model/restriction strings");
        Require(report.find(L"原磁盘设备实例身份为空") != report.npos,
                "unidentified diagnostic disk is reported without querying or removing any device");
    }
    std::cout
        << "PASS checked USB eject node selection, scope, bounded retry and accurate veto diagnostics "
           "(PnP is a test double)\n";
    return checks;
}
