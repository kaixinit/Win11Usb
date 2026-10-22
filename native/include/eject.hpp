#pragma once
#include "win11usb.hpp"
#include <cfgmgr32.h>
#include <optional>

namespace wu {
enum class EjectKind { Disk, Storage, Usb, Volume, Other };
struct EjectNode {
    DEVINST id = 0;
    std::wstring instance, service;
    EjectKind kind = EjectKind::Other;
    DWORD capabilities = 0;
    bool capabilitiesKnown = false;
};
struct EjectReply {
    CONFIGRET code = CR_SUCCESS;
    PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
    std::wstring name;
};
// The same selection/verification sequence is used with native APIs and fixtures.
struct EjectOps {
    std::function<EjectNode(DEVINST)> read;
    std::function<std::optional<DEVINST>(DEVINST)> parent;
    std::function<std::vector<DEVINST>(DEVINST)> children;
    std::function<void()> validate;
    std::function<void()> prepare; // Must return with volume/disk handles closed.
    std::function<EjectReply(DEVINST)> request;
    std::function<void(DWORD)> wait;
};
EjectNode ReadEjectNode(DEVINST node);
std::vector<EjectNode> PlanUsbEject(DEVINST disk, const std::wstring &expectedInstance, const EjectOps &ops);
std::wstring EjectFailure(const EjectReply &reply);
void EjectUsbChecked(DEVINST disk, const std::wstring &expectedInstance, const EjectOps &ops,
                     std::atomic<bool> &stop);
void EjectUsbNative(const Disk &disk, const std::function<void()> &validate,
                    const std::function<void()> &prepare, std::atomic<bool> &stop);
} // namespace wu
