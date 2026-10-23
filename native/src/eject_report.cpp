#include "eject.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace wu {
namespace {
std::wstring Field(std::wstring text, size_t limit = 160) {
    for (auto &c : text)
        if (c < 32 || c == 127)
            c = L' ';
    return text.substr(0, limit);
}
std::wstring Redacted(std::wstring text, const Disk &disk) {
    // Names and failure messages are useful, but registry/device values must
    // never accidentally expose the retained instance, path or device serial.
    for (const auto &secret : {disk.pnp, disk.path, disk.serial}) {
        if (secret.empty())
            continue;
        auto lowerSecret = Lower(secret);
        size_t at = 0;
        while ((at = Lower(text).find(lowerSecret, at)) != std::wstring::npos) {
            text.replace(at, secret.size(), L"[已隐藏]");
            at += 5;
        }
    }
    return Field(text, 480);
}
std::wstring Kind(EjectKind kind) {
    switch (kind) {
    case EjectKind::Disk:
        return L"Disk";
    case EjectKind::Storage:
        return L"Storage";
    case EjectKind::Usb:
        return L"USB";
    case EjectKind::Volume:
        return L"Volume";
    default:
        return L"Other";
    }
}
std::wstring Service(const std::wstring &text) {
    if (text.empty())
        return L"(空)";
    if (text.size() > 120 || !std::all_of(text.begin(), text.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                   c == L'_' || c == L'-' || c == L'.';
        }))
        return L"(异常服务名，未输出原始值)";
    return text;
}
std::wstring ClassGuid(const std::wstring &text) {
    if (text.empty())
        return L"(空)";
    if (text.size() != 38 || text.front() != L'{' || text.back() != L'}')
        return L"(异常类 GUID，未输出原始值)";
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        bool separator = i == 9 || i == 14 || i == 19 || i == 24;
        auto c = text[i];
        if (separator ? c != L'-'
                      : !((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
            return L"(异常类 GUID，未输出原始值)";
    }
    return Lower(text);
}
bool PhysicalUsb(const EjectNode &node) {
    auto instance = Lower(node.instance);
    auto end = instance.find(L'\\', 4);
    auto hardware = instance.substr(0, end);
    return hardware.rfind(L"usb\\vid_", 0) == 0 && hardware.find(L"&pid_") != hardware.npos &&
           hardware.find(L"&mi_") == hardware.npos;
}
std::wstring StopReason(const EjectNode &node) {
    auto service = Lower(node.service), instance = Lower(node.instance);
    if (service == L"usbccgp")
        return L"复合 USB 设备，禁止扩大弹出范围";
    if (service.rfind(L"usbhub", 0) == 0 || instance.rfind(L"usb\\root_", 0) == 0)
        return L"USB 集线器或根集线器，禁止继续向上规划";
    if (service.find(L"xhci") != service.npos || service.find(L"ehci") != service.npos)
        return L"USB 控制器，禁止继续向上规划";
    if (node.kind == EjectKind::Usb && service != L"usbstor" && service != L"uaspstor")
        return L"USB 类节点的服务不是允许的独立存储服务";
    if (node.kind == EjectKind::Other)
        return L"设备类不在存储弹出允许列表";
    if (PhysicalUsb(node))
        return L"已到达独立物理 USB 存储边界，不访问其上层集线器";
    return {};
}
std::optional<DEVINST> ParentNode(DEVINST node) {
    DEVINST parent = 0;
    auto code = CM_Get_Parent(&parent, node, 0);
    if (code == CR_NO_SUCH_DEVNODE)
        return {};
    if (code != CR_SUCCESS)
        throw Error(L"只读父节点查询失败（CM " + std::to_wstring(code) + L"）。");
    return parent;
}
std::vector<DEVINST> ChildNodes(DEVINST node) {
    std::vector<DEVINST> children;
    DEVINST child = 0;
    auto code = CM_Get_Child(&child, node, 0);
    if (code == CR_NO_SUCH_DEVNODE)
        return children;
    if (code != CR_SUCCESS)
        throw Error(L"只读子节点查询失败（CM " + std::to_wstring(code) + L"）。");
    for (;;) {
        if (children.size() >= 256 || std::find(children.begin(), children.end(), child) != children.end())
            throw Error(L"只读子节点列表存在循环或超过 256 项。");
        children.push_back(child);
        DEVINST sibling = 0;
        code = CM_Get_Sibling(&sibling, child, 0);
        if (code == CR_NO_SUCH_DEVNODE)
            return children;
        if (code != CR_SUCCESS)
            throw Error(L"只读同级节点查询失败（CM " + std::to_wstring(code) + L"）。");
        child = sibling;
    }
}
} // namespace

std::wstring UsbEjectReport(const Catalog &catalog) {
    std::wostringstream out;
    out << L"Win11Usb 0.2.0-preview.14 · USB 安全弹出只读诊断\r\n"
           L"实现：C++ / Win32；不依赖 .NET 或 PowerShell。\r\n"
           L"本报告只读取设备属性、父子关系并规划候选节点。\r\n"
           L"未清盘、未格式化、未锁定或卸载卷、未提交任何安全弹出请求。\r\n"
           L"规划通过不等于已经弹出，也不能预测系统是否会拒绝实际请求。\r\n"
           L"不包含原始设备实例路径、设备序列号或源文件路径；节点编号仅在本报告内有效。\r\n";
    out << L"权限：" << (IsAdmin() ? L"管理员" : L"普通用户") << L"\r\n";
    out << L"磁盘接口=" << catalog.interfaces << L"；已识别 USB 整盘=" << catalog.disks.size()
        << L"；非 USB=" << catalog.nonUsb << L"；未完成查询=" << catalog.failed << L"\r\n";

    std::map<DEVINST, size_t> ordinals;
    auto ordinal = [&](DEVINST id) {
        auto found = ordinals.find(id);
        if (found != ordinals.end())
            return found->second;
        auto value = ordinals.size() + 1;
        ordinals.emplace(id, value);
        return value;
    };
    for (const auto &disk : catalog.disks) {
        out << L"\r\n=== USB 整盘 " << (disk.numberKnown ? std::to_wstring(disk.number) : L"未确认")
            << L" ===\r\n";
        out << L"型号：" << Redacted(disk.friendly, disk) << L"\r\n";
        out << L"容量：" << (disk.capacityKnown ? FormatSize(disk.size) : L"未确认") << L"；制作可选="
            << (disk.eligible() ? L"是" : L"否") << L"；查询完整=" << (disk.complete() ? L"是" : L"否")
            << L"\r\n";
        if (!disk.blocked.empty())
            out << L"制作限制：" << Redacted(disk.blocked, disk) << L"\r\n";
        if (disk.pnp.empty()) {
            out << L"无法规划：原磁盘设备实例身份为空。\r\n";
            continue;
        }
        try {
            DEVINST start = 0;
            auto instance = disk.pnp;
            auto code = CM_Locate_DevNodeW(&start, instance.data(), CM_LOCATE_DEVNODE_NORMAL);
            if (code != CR_SUCCESS)
                throw Error(L"无法定位原磁盘设备节点（CM " + std::to_wstring(code) + L"）。");

            EjectOps ops;
            ops.read = ReadEjectNode;
            ops.parent = ParentNode;
            ops.children = ChildNodes;
            ops.validate = [] { throw Error(L"只读诊断禁止执行设备写入校验回调。"); };
            ops.prepare = [] { throw Error(L"只读诊断禁止锁定、卸载或刷新卷。"); };
            ops.request = [](DEVINST) -> EjectReply { throw Error(L"只读诊断禁止提交安全弹出请求。"); };
            ops.wait = [](DWORD) { throw Error(L"只读诊断禁止执行弹出重试。"); };

            out << L"祖先方向：所选磁盘 → 存储父节点 → 独立物理 USB 存储边界\r\n";
            std::set<DEVINST> seen;
            auto current = std::optional<DEVINST>(start);
            unsigned depth = 0;
            while (current && depth < 32) {
                if (!seen.insert(*current).second) {
                    out << L"停止：祖先链存在循环。\r\n";
                    break;
                }
                const auto node = ops.read(*current);
                out << L"  深度 " << depth << L" / 节点 #" << ordinal(*current) << L"：kind="
                    << Kind(node.kind) << L"；class=" << ClassGuid(node.classGuid) << L"；service="
                    << Service(node.service) << L"；capabilities=";
                if (node.capabilitiesKnown)
                    out << L"0x" << std::hex << std::uppercase << node.capabilities << std::dec;
                else
                    out << L"未知";
                out << L"；独立物理 USB=" << (PhysicalUsb(node) ? L"是" : L"否") << L"\r\n";
                auto reason = StopReason(node);
                if (!reason.empty()) {
                    out << L"  停止：" << reason << L"。\r\n";
                    break;
                }
                current = ops.parent(*current);
                ++depth;
                if (!current)
                    out << L"  停止：系统未提供更上层节点。\r\n";
            }
            if (current && depth >= 32)
                out << L"  停止：祖先链达到 32 节点安全上限。\r\n";

            try {
                auto plan = PlanUsbEject(start, disk.pnp, ops);
                out << L"只读规划：通过。" << plan.size() << L" 个候选节点，顺序：";
                for (size_t i = 0; i < plan.size(); ++i) {
                    if (i)
                        out << L" → ";
                    out << L"#" << ordinal(plan[i].id) << L" (" << Kind(plan[i].kind) << L")";
                }
                out << L"。\r\n只读规划已核对：唯一所选磁盘、可移除能力与安全子树范围。"
                       L"未尝试弹出。\r\n";
            } catch (const Error &e) {
                out << L"只读规划：未通过。" << Redacted(e.text, disk) << L"\r\n未尝试弹出。\r\n";
            }
        } catch (const Error &e) {
            out << L"只读查询未完成：" << Redacted(e.text, disk) << L"\r\n未尝试弹出。\r\n";
        } catch (const std::exception &) {
            out << L"只读查询未完成：内部查询异常；未输出可能包含设备身份的异常原文。\r\n"
                   L"未尝试弹出。\r\n";
        }
    }
    if (catalog.disks.empty())
        out << L"\r\n当前未识别到可报告的 USB 整盘。\r\n";
    return out.str();
}
} // namespace wu
