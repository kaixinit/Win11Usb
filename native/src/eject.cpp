#include "eject.hpp"
#include <algorithm>

namespace wu {
namespace {
bool Unsafe(const EjectNode &node) {
    auto service = Lower(node.service), instance = Lower(node.instance);
    // USB class alone also covers hubs and multifunction devices. Only known
    // mass-storage USB services may be removed through this automatic path.
    return (node.kind == EjectKind::Usb && service != L"usbstor" && service != L"uaspstor") ||
           service == L"usbccgp" || service.rfind(L"usbhub", 0) == 0 ||
           service.find(L"xhci") != service.npos || service.find(L"ehci") != service.npos ||
           instance.rfind(L"usb\\root_", 0) == 0;
}
bool PhysicalUsb(const EjectNode &node) {
    auto instance = Lower(node.instance);
    auto end = instance.find(L'\\', 4);
    auto hardware = instance.substr(0, end);
    return hardware.rfind(L"usb\\vid_", 0) == 0 && hardware.find(L"&pid_") != hardware.npos &&
           hardware.find(L"&mi_") == hardware.npos;
}
bool Removable(const EjectNode &node) {
    return node.capabilitiesKnown && (node.capabilities & (CM_DEVCAP_REMOVABLE | CM_DEVCAP_EJECTSUPPORTED)) &&
           !(node.capabilities & (CM_DEVCAP_DOCKDEVICE | CM_DEVCAP_NONDYNAMIC));
}
void CheckScope(DEVINST candidate, DEVINST disk, const std::wstring &expected, const EjectOps &ops) {
    std::vector<DEVINST> todo{candidate};
    std::set<DEVINST> seen;
    unsigned disks = 0;
    while (!todo.empty()) {
        auto id = todo.back();
        todo.pop_back();
        if (!seen.insert(id).second || seen.size() > 256)
            throw Error(L"弹出范围的设备树异常，已停止自动弹出。");
        auto node = ops.read(id);
        if (node.id != id || node.instance.empty() || Unsafe(node) || node.kind == EjectKind::Other)
            throw Error(L"弹出范围包含集线器、复合设备或其他功能，已停止自动弹出。");
        if (node.kind == EjectKind::Disk) {
            if (id != disk || Lower(node.instance) != Lower(expected))
                throw Error(L"上层设备还包含其他磁盘，不能仅按本次所选 U 盘自动弹出。");
            disks++;
        }
        auto children = ops.children(id);
        todo.insert(todo.end(), children.begin(), children.end());
    }
    if (disks != 1)
        throw Error(L"未能在弹出范围内唯一确认所选 U 盘，已停止自动弹出。");
}
std::wstring Property(DEVINST node, ULONG property, bool optional = false) {
    wchar_t text[4096]{};
    ULONG bytes = sizeof text, type = 0;
    auto result = CM_Get_DevNode_Registry_PropertyW(node, property, &type, text, &bytes, 0);
    if (optional && result == CR_NO_SUCH_VALUE)
        return L"";
    if (result != CR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof text ||
        bytes % sizeof(wchar_t) || text[bytes / sizeof(wchar_t) - 1] != 0)
        throw Error(L"无法完整读取弹出设备属性（CM " + std::to_wstring(result) + L"）。");
    return text;
}
std::wstring Hint(const std::wstring &value) {
    auto text = BaseName(Trim(value));
    text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t c) { return c < 32; }), text.end());
    return text.substr(0, 120);
}
} // namespace
EjectNode ReadEjectNode(DEVINST id) {
    EjectNode node;
    node.id = id;
    wchar_t instance[MAX_DEVICE_ID_LEN]{};
    auto result = CM_Get_Device_IDW(id, instance, MAX_DEVICE_ID_LEN, 0);
    if (result != CR_SUCCESS)
        throw Error(L"无法读取弹出设备身份（CM " + std::to_wstring(result) + L"）。");
    node.instance = instance;
    node.service = Property(id, CM_DRP_SERVICE, true);
    auto cls = Lower(Property(id, CM_DRP_CLASSGUID));
    if (cls == L"{4d36e967-e325-11ce-bfc1-08002be10318}")
        node.kind = EjectKind::Disk;
    else if (cls == L"{4d36e97b-e325-11ce-bfc1-08002be10318}" ||
             cls == L"{4d36e96a-e325-11ce-bfc1-08002be10318}")
        node.kind = EjectKind::Storage;
    else if (cls == L"{4d36e97d-e325-11ce-bfc1-08002be10318}")
        node.kind = EjectKind::Usb;
    else if (cls == L"{71a27cdd-812a-11d0-bec7-08002be2092f}" ||
             cls == L"{533c5b84-ec70-11d2-9505-00c04f79deaf}")
        node.kind = EjectKind::Volume;
    ULONG bytes = sizeof node.capabilities, type = 0;
    result = CM_Get_DevNode_Registry_PropertyW(id, CM_DRP_CAPABILITIES, &type, &node.capabilities, &bytes, 0);
    node.capabilitiesKnown = result == CR_SUCCESS && type == REG_DWORD && bytes == sizeof(DWORD);
    return node;
}
std::vector<EjectNode> PlanUsbEject(DEVINST disk, const std::wstring &expected, const EjectOps &ops) {
    std::vector<EjectNode> chain, candidates;
    std::set<DEVINST> seen;
    auto current = std::optional<DEVINST>(disk);
    bool usbBoundary = false;
    while (current && chain.size() < 32) {
        if (!seen.insert(*current).second)
            throw Error(L"弹出设备的父节点链存在循环。");
        auto node = ops.read(*current);
        if (node.id != *current || node.instance.empty() ||
            (chain.empty() &&
             (node.kind != EjectKind::Disk || expected.empty() || Lower(node.instance) != Lower(expected))))
            throw Error(L"安全弹出前所选磁盘身份发生变化。");
        if (Unsafe(node) || node.kind == EjectKind::Other)
            break;
        chain.push_back(node);
        if (PhysicalUsb(node)) {
            usbBoundary = true;
            break; // Never ascend to a hub, controller or another physical USB device.
        }
        current = ops.parent(*current);
    }
    if (!usbBoundary)
        throw Error(L"未能确认所选 U 盘对应的独立存储设备，请通过系统安全弹出。");
    std::wstring rejected;
    for (const auto &node : chain)
        if (Removable(node)) {
            // A blocked parent must not prohibit a safe disk-only candidate.
            try {
                CheckScope(node.id, disk, expected, ops);
                candidates.push_back(node);
            } catch (const Error &e) {
                if (node.id == disk)
                    throw;
                rejected = e.text;
            }
        }
    if (candidates.empty())
        throw Error(rejected.empty() ? L"未找到范围仅包含所选磁盘的可移除存储节点，请通过系统安全弹出。"
                                     : rejected);
    return candidates;
}
std::wstring EjectFailure(const EjectReply &reply) {
    std::wstring reason;
    if (reply.code != CR_REMOVE_VETOED) {
        if (reply.code == CR_ACCESS_DENIED)
            reason = L"设备管理器拒绝访问，当前权限不足。";
        else if (reply.code == CR_NO_SUCH_DEVNODE || reply.code == CR_INVALID_DEVNODE)
            reason = L"所选设备节点已消失或发生变化，无法确认安全弹出。";
        else if (reply.code == CR_CALL_NOT_IMPLEMENTED)
            reason = L"当前系统未提供所需的设备弹出接口。";
        else
            reason = L"设备管理器未能完成安全弹出请求。";
    } else {
        switch (reply.veto) {
        case PNP_VetoPendingClose:
            reason = L"设备正在等待关闭操作完成。";
            break;
        case PNP_VetoOutstandingOpen:
            reason = L"设备仍有打开的句柄，请关闭访问 U 盘的程序或文件窗口。";
            break;
        case PNP_VetoWindowsApp:
            reason = L"应用程序阻止了安全弹出。";
            break;
        case PNP_VetoWindowsService:
            reason = L"Windows 服务阻止了安全弹出。";
            break;
        case PNP_VetoIllegalDeviceRequest:
        case PNP_VetoLegacyDevice:
            reason = L"此设备节点不支持当前安全弹出请求。";
            break;
        case PNP_VetoDriver:
            reason = L"设备驱动拒绝了安全弹出请求。";
            break;
        case PNP_VetoLegacyDriver:
            reason = L"旧式驱动不支持所需的即插即用移除操作。";
            break;
        case PNP_VetoDevice:
            reason = L"设备拒绝了安全弹出请求。";
            break;
        case PNP_VetoNonDisableable:
            reason = L"该设备当前不允许动态移除。";
            break;
        case PNP_VetoInsufficientPower:
            reason = L"设备电源状态不满足移除条件。";
            break;
        case PNP_VetoInsufficientRights:
            reason = L"当前权限不足，无法安全弹出设备。";
            break;
        case PNP_VetoAlreadyRemoved:
            reason = L"设备已被移除，无法再次确认本次弹出结果。";
            break;
        default:
            reason = L"系统拒绝了安全弹出，未提供明确原因。";
            break;
        }
        // Device instance paths may contain serials. Only show application/service/driver hints.
        if (!reply.name.empty() &&
            (reply.veto == PNP_VetoWindowsApp || reply.veto == PNP_VetoWindowsService ||
             reply.veto == PNP_VetoDriver || reply.veto == PNP_VetoLegacyDriver))
            reason += L" 提示：" + Hint(reply.name) + L"。";
    }
    return reason + L"（CM " + std::to_wstring(reply.code) + L"，veto " + std::to_wstring(reply.veto) +
           L"）U 盘尚未安全弹出，请稍后通过系统安全弹出。";
}
void EjectUsbChecked(DEVINST disk, const std::wstring &expected, const EjectOps &ops,
                     std::atomic<bool> &stop) {
    CheckCancel(stop);
    ops.validate();
    auto plan = PlanUsbEject(disk, expected, ops);
    EjectReply last;
    for (const auto &candidate : plan) {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            CheckCancel(stop);
            ops.validate();
            auto fresh = PlanUsbEject(disk, expected, ops);
            auto found = std::find_if(fresh.begin(), fresh.end(), [&](const EjectNode &node) {
                return node.id == candidate.id && Lower(node.instance) == Lower(candidate.instance);
            });
            if (found == fresh.end())
                throw Error(L"安全弹出的设备身份、能力或影响范围发生变化，已停止自动弹出。");
            ops.prepare(); // Native adapter flushes, locks, dismounts and closes before requesting PnP.
            CheckCancel(stop);
            last = ops.request(candidate.id);
            if (last.code == CR_SUCCESS)
                return;
            bool transient = last.code == CR_REMOVE_VETOED &&
                             (last.veto == PNP_VetoPendingClose || last.veto == PNP_VetoOutstandingOpen);
            if (transient && attempt < 2) {
                ops.wait(500);
                continue;
            }
            bool unsupported = last.code == CR_REMOVE_VETOED && (last.veto == PNP_VetoIllegalDeviceRequest ||
                                                                 last.veto == PNP_VetoLegacyDevice);
            if (!unsupported)
                throw Error(EjectFailure(last));
            break; // Try only another independently checked candidate inside the same USB boundary.
        }
    }
    throw Error(EjectFailure(last));
}
void EjectUsbNative(const Disk &disk, const std::function<void()> &validate,
                    const std::function<void()> &prepare, std::atomic<bool> &stop) {
    std::wstring instance = disk.pnp;
    DEVINST node = 0;
    auto result = CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL);
    if (result != CR_SUCCESS)
        throw Error(EjectFailure({result, PNP_VetoTypeUnknown, L""}));
    EjectOps ops;
    ops.read = ReadEjectNode;
    ops.parent = [](DEVINST id) -> std::optional<DEVINST> {
        DEVINST parent;
        auto code = CM_Get_Parent(&parent, id, 0);
        if (code == CR_NO_SUCH_DEVNODE)
            return {};
        if (code != CR_SUCCESS)
            throw Error(L"无法读取弹出设备的父节点（CM " + std::to_wstring(code) + L"）。");
        return parent;
    };
    ops.children = [](DEVINST id) {
        std::vector<DEVINST> children;
        DEVINST child;
        auto code = CM_Get_Child(&child, id, 0);
        if (code == CR_NO_SUCH_DEVNODE)
            return children;
        if (code != CR_SUCCESS)
            throw Error(L"无法核对弹出设备的影响范围（CM " + std::to_wstring(code) + L"）。");
        for (;;) {
            if (children.size() >= 256 ||
                std::find(children.begin(), children.end(), child) != children.end())
                throw Error(L"弹出设备的子节点列表异常。");
            children.push_back(child);
            DEVINST sibling;
            code = CM_Get_Sibling(&sibling, child, 0);
            if (code == CR_NO_SUCH_DEVNODE)
                break;
            if (code != CR_SUCCESS)
                throw Error(L"无法完整核对弹出设备范围（CM " + std::to_wstring(code) + L"）。");
            child = sibling;
        }
        return children;
    };
    ops.validate = validate;
    ops.prepare = prepare;
    ops.request = [](DEVINST id) {
        EjectReply reply;
        wchar_t name[MAX_PATH]{};
        reply.code = CM_Request_Device_EjectW(id, &reply.veto, name, MAX_PATH, 0);
        name[MAX_PATH - 1] = 0;
        reply.name = name;
        return reply;
    };
    ops.wait = [](DWORD milliseconds) { Sleep(milliseconds); };
    EjectUsbChecked(node, disk.pnp, ops, stop);
}
} // namespace wu
