#include "eject.hpp"
#include "platform.hpp"
#include <algorithm>
#include <iostream>
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
        nodes[2] = {2, L"USB\\VID_1234&PID_5678\\own", L"USBSTOR", EjectKind::Usb, CM_DEVCAP_REMOVABLE, true};
        nodes[3] = {3, L"USB\\ROOT_HUB30\\hub", L"USBHUB3", EjectKind::Usb, CM_DEVCAP_REMOVABLE, true};
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
    static_assert(PNP_VetoIllegalDeviceRequest == 8 && PNP_VetoOutstandingOpen == 5 && CR_REMOVE_VETOED == 23,
                  "SDK values from the user's report");
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
        const GUID diskClass = {0x4d36e967, 0xe325, 0x11ce, {0xbf, 0xc1, 8, 0, 0x2b, 0xe1, 3, 0x18}};
        HDEVINFO devices = SetupDiGetClassDevsW(&diskClass, nullptr, nullptr, DIGCF_PRESENT);
        Require(devices != INVALID_HANDLE_VALUE, "read-only native disk metadata fixture enumeration");
        struct CloseDevices {
            HDEVINFO devices;
            ~CloseDevices() { SetupDiDestroyDeviceInfoList(devices); }
        } close{devices};
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof data;
        if (SetupDiEnumDeviceInfo(devices, 0, &data)) {
            auto node = ReadEjectNode(data.DevInst);
            Require(node.kind == EjectKind::Disk && !node.instance.empty(),
                    "actual native CM registry class/instance adapter reads an existing disk");
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
            std::cout << "PASS actual read-only CM device metadata/locator; no removal requested\n";
        } else
            std::cout << "NOT RUN native CM metadata fixture: no disk class device\n";
    }
    std::cout << "PASS checked USB eject node selection, scope, bounded retry and accurate veto diagnostics "
                 "(PnP is a test double)\n";
    return checks;
}
