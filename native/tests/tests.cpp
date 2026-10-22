#include "platform.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

using namespace wu;
namespace {
int checks = 0;
void Require(bool value, const char *message) {
    checks++;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Reject(F f, const char *msg) {
    bool failed = false;
    try {
        f();
    } catch (const Error &) {
        failed = true;
    }
    Require(failed, msg);
}
std::wstring Read(const std::wstring &p) {
    Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid())
        throw Error(WinError());
    std::string s((size_t)FileSize(p), 0);
    DWORD n = 0;
    if (!ReadFile(h, s.data(), (DWORD)s.size(), &n, nullptr) || n != s.size())
        throw Error(WinError());
    return Wide(s);
}
std::vector<std::wstring> Fields(const std::wstring &s) {
    std::vector<std::wstring> v;
    size_t a = 0, b;
    while ((b = s.find(L'\t', a)) != s.npos) {
        v.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    v.push_back(s.substr(a));
    return v;
}
File F(const wchar_t *relative, uint64_t size) { return {L"X:\\" + std::wstring(relative), relative, size}; }
Request TestRequest(const wchar_t *fs = L"FAT32") {
    Request r;
    r.fs = fs;
    r.disk.size = 64 * GiB;
    r.disk.pnp = L"fixture-not-a-physical-device";
    r.disk.friendly = L"TEST ONLY";
    return r;
}
struct Fake final : Backend {
    std::vector<std::wstring> calls;
    bool split = true;
    std::wstring fail;
    int checks = 0;
    void Call(const wchar_t *s) {
        calls.push_back(s);
        if (fail == s)
            throw Error(L"injected failure");
    }
    Prepared Prepare(const Request &, const Hooks &, std::atomic<bool> &) override {
        Call(L"prepare");
        Prepared p;
        p.work = L"TEST";
        p.plan.split = split;
        p.plan.total = 8 * GiB;
        p.plan.files = {F(L"sources\\install.wim", split ? 8 * GiB : 100)};
        return p;
    }
    void Validate(const Request &, const Prepared &, bool original) override {
        checks++;
        Call(original ? L"validate-original" : L"validate-new");
    }
    std::wstring Format(const Request &, const Prepared &, const Hooks &, std::atomic<bool> &) override {
        Call(L"format");
        return L"TEST:\\";
    }
    std::vector<File> Split(const Prepared &, const Hooks &, std::atomic<bool> &) override {
        Call(L"split");
        return {F(L"install.swm", 4000 * MiB), F(L"install2.swm", 3800 * MiB)};
    }
    void Copy(const Request &, const Prepared &, const std::wstring &, const std::vector<File> &,
              const Hooks &, std::atomic<bool> &) override {
        Call(L"copy");
    }
    void Verify(const Request &, const Prepared &, const std::wstring &, const std::vector<File> &,
                std::atomic<bool> &) override {
        Call(L"verify");
    }
    void Cleanup(const Prepared &, const std::vector<File> &) override { Call(L"cleanup"); }
    void EjectUsb(const Request &, const std::wstring &) override { Call(L"eject-usb"); }
    void EjectIso() override { Call(L"eject-iso"); }
    void Abort(bool) noexcept override { calls.push_back(L"abort"); }
    bool Has(const wchar_t *s) { return std::find(calls.begin(), calls.end(), s) != calls.end(); }
};
void IdentityTests(const std::wstring &fixtures) {
    std::wistringstream lines(Read(fixtures));
    std::wstring line;
    int count = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (line.empty())
            continue;
        auto f = Fields(line);
        Require(f.size() == 10, "legacy fixture field count");
        ImageIdentity d;
        d.name = f[0];
        d.description = f[1];
        d.edition = f[2];
        d.major = 10;
        d.build = std::stoi(f[3]);
        d.installation = f[8].empty() ? L"Client" : f[8];
        d.product = f[9].empty() ? L"WinNT" : f[9];
        d = ResolveIdentity(d, f[4]);
        if (d.label != f[5] || d.release != f[6] || (!f[7].empty() && d.status != f[7])) {
            std::wcerr << L"FAIL identity: " << f[0] << L" / " << f[4] << L" got " << d.label << L" / "
                       << d.release << L" / " << d.status << L"\n";
            throw std::runtime_error("legacy identity regression");
        }
        Require(d.label.size() <= 11, "FAT label length");
        if (d.status == L"IsoNameHint")
            Require(d.note.find(L"错误重命名") != d.note.npos, "filename hint disclaimer");
        count++;
    }
    Require(count == 39, "all 39 legacy fixture cases ran");
    std::cout << "PASS 39 unchanged legacy image identity cases\n";
}
void PlanTests() {
    auto p = PlanImages(L"X:\\", {F(L"sources\\install.wim", Fat32Limit)});
    Require(!p.split, "exact FAT32 max does not split");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 100), F(L"sources\\install2.swm", 100)}); },
           "orphan SWM mixed with WIM rejected before formatting");
    p = PlanImages(L"X:\\", {F(L"sources\\install.wim", Fat32Limit + 1)});
    Require(p.split, "one byte above FAT32 max splits");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.esd", Fat32Limit + 1)}); },
           "oversize ESD rejected");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 100), F(L"sources\\install.esd", 100)}); },
           "mixed image rejected");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 100), F(L"other.bin", Fat32Limit + 1)}); },
           "oversize non-WIM rejected even on NTFS");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 100), F(L"..\\escape", 1)}); },
           "path traversal rejected");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 0)}); }, "empty WIM rejected");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", 100), F(L"SOURCES\\INSTALL.WIM", 100)}); },
           "duplicate path rejected");
    Reject([] { PlanImages(L"X:\\", {F(L"sources\\install.wim", UINT64_MAX), F(L"x", 1)}); },
           "total overflow rejected");
    CheckSwm({F(L"install.swm", Fat32Limit), F(L"install2.swm", 1)});
    Reject([] { CheckSwm({F(L"install.swm", 1), F(L"install3.swm", 1)}); }, "missing SWM rejected");
    Reject([] { CheckSwm({F(L"install.swm", 1), F(L"install2.swm", Fat32Limit + 1)}); },
           "oversize SWM rejected");
    Reject([] { CheckSwm({F(L"install.swm", 1), F(L"install2.swm", 0)}); }, "empty SWM rejected");
    Reject([] { CheckSwm({F(L"install.swm", 1), F(L"INSTALL.SWM", 1)}); }, "duplicate SWM rejected");
    Reject([] { CheckSwm({F(L"install.swm", 1), F(L"install02.swm", 1)}); }, "non-canonical SWM rejected");
    auto swm = PlanImages(L"X:\\", {F(L"sources\\install.swm", 1), F(L"sources\\install2.swm", 2)});
    Require(!swm.split && swm.images.size() == 2, "existing SWM accepted");
    std::cout << "PASS size boundary, image and SWM preflight\n";
}
void WorkflowTests() {
    for (auto fs : {L"FAT32", L"NTFS"}) {
        Fake f;
        std::atomic<bool> s = false;
        std::vector<int> stages;
        Hooks h;
        h.ask = [](Prompt, const std::wstring &) { return true; };
        h.progress = [&](const Progress &p) { stages.push_back(p.stage); };
        auto r = RunWorkflow(TestRequest(fs), f, h, s);
        Require(r.copied && r.error.empty(), "successful workflow");
        Require(f.Has(L"split") && f.Has(L"copy") && f.Has(L"verify") && f.Has(L"cleanup") &&
                    f.Has(L"eject-usb") && f.Has(L"eject-iso"),
                "all stages including split on NTFS");
        Require(stages == std::vector<int>({1, 2, 3, 4, 5, 6, 6}), "six stage order");
        Require(f.checks >= 5, "disk rechecks at stage boundaries");
    }
    for (auto prompt : {Prompt::Format, Prompt::Split, Prompt::Copy}) {
        Fake f;
        std::atomic<bool> s = false;
        Hooks h;
        h.ask = [&](Prompt p, const std::wstring &) { return p != prompt; };
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(r.cancelled && f.Has(L"abort") && !f.Has(L"copy"), "prompt cancellation halts before copy");
        if (prompt == Prompt::Format)
            Require(!f.Has(L"format") && !r.rebuildStarted, "cancel format never calls destructive backend");
    }
    for (auto failure :
         {L"prepare", L"validate-original", L"format", L"validate-new", L"split", L"copy", L"verify"}) {
        Fake f;
        f.fail = failure;
        std::atomic<bool> s = false;
        Hooks h;
        h.ask = [](Prompt, const std::wstring &) { return true; };
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(!r.copied && !r.error.empty() && f.Has(L"abort"), "failure halts workflow");
        if (f.fail == L"prepare" || f.fail == L"validate-original")
            Require(!f.Has(L"format"), "preflight failure never formats");
        Require(!f.Has(L"cleanup") && !f.Has(L"eject-usb"), "failure never reports cleanup/eject success");
    }
    for (auto failure : {L"cleanup", L"eject-usb", L"eject-iso"}) {
        Fake f;
        f.fail = failure;
        std::atomic<bool> s = false;
        Hooks h;
        h.ask = [](Prompt, const std::wstring &) { return true; };
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(r.copied && r.error.empty(), "post-verify warning preserves successful copy");
    }
    {
        Fake f;
        f.split = false;
        std::atomic<bool> s = false;
        Hooks h;
        h.ask = [](Prompt, const std::wstring &) { return true; };
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(r.copied && !f.Has(L"split") && !f.Has(L"cleanup"), "small WIM skips split and cleanup");
    }
    {
        Fake f;
        std::atomic<bool> s = true;
        auto r = RunWorkflow(TestRequest(), f, {}, s);
        Require(r.cancelled && !f.Has(L"prepare") && !f.Has(L"format"), "early cancel has no disk calls");
    }
    {
        Fake f;
        std::atomic<bool> s = false;
        Hooks h;
        h.ask = [&](Prompt p, const std::wstring &) {
            if (p == Prompt::Format)
                s = true;
            return true;
        };
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(r.cancelled && !f.Has(L"format"), "cancel after affirmative format still prevents write");
    }
    {
        Fake f;
        std::atomic<bool> s = false;
        Hooks h;
        auto r = RunWorkflow(TestRequest(), f, h, s);
        Require(r.cancelled && !f.Has(L"format"), "missing confirmation callback defaults cancel");
    }
    {
        Fake f;
        std::atomic<bool> stop = false;
        std::wstring messages;
        Hooks h;
        h.ask = [](Prompt, const std::wstring &) { return true; };
        h.progress = [&](const Progress &p) {
            if (p.stage == 6)
                stop = true;
        };
        h.log = [&](const std::wstring &s) { messages += s; };
        auto r = RunWorkflow(TestRequest(), f, h, stop);
        Require(r.copied && r.cancelled && !f.Has(L"eject-usb"),
                "cancelling cleanup preserves verified installation files");
        Require(messages.find(L"可能需要重新制作") == messages.npos,
                "post-verification cancellation does not incorrectly report failed copy");
    }
    std::cout << "PASS full FAT32/NTFS state machine, prompts and injected failures\n";
}
void FileTests(const std::wstring &root) {
    std::atomic<bool> stop = false;
    MakeDirectory(root + L"\\source\\目录");
    MakeDirectory(root + L"\\target");
    WriteUtf8(root + L"\\source\\目录\\中文 文件.txt", L"native UTF-8 file\r\n");
    WriteUtf8(root + L"\\source\\boot.bin", std::wstring(128 * 1024, L'B'));
    auto files = ListFiles(root + L"\\source\\", stop);
    Require(files.size() == 2, "native recursive enumeration");
    int progress = 0;
    Hooks h;
    h.progress = [&](const Progress &) { progress++; };
    CopyFilesNative(files, root + L"\\target\\", h, stop);
    VerifyFilesNative(files, root + L"\\target\\", stop);
    Require(progress > 0, "actual CopyFileEx reports progress");
    Require(Read(root + L"\\target\\目录\\中文 文件.txt") == L"native UTF-8 file\r\n",
            "actual Unicode file copied");
    wchar_t volumeRoot[MAX_PATH], guid[MAX_PATH];
    Require(GetVolumePathNameW(root.c_str(), volumeRoot, MAX_PATH) &&
                GetVolumeNameForVolumeMountPointW(volumeRoot, guid, MAX_PATH),
            "resolve actual volume GUID for copy destination fixture");
    auto guidTarget = std::wstring(guid) + FullPath(root).substr(wcslen(volumeRoot)) + L"\\guid-copy\\";
    CopyFilesNative(files, guidTarget, h, stop);
    VerifyFilesNative(files, guidTarget, stop);
    Require(Read(root + L"\\guid-copy\\目录\\中文 文件.txt") == L"native UTF-8 file\r\n",
            "production copy and directory creation work through real volume-GUID paths");
    Reject([&] { CopyFilesNative(files, root + L"\\target\\", h, stop); },
           "native copy refuses to overwrite unknown existing file");
    WriteUtf8(root + L"\\target\\boot.bin", L"corrupt length");
    Reject([&] { VerifyFilesNative(files, root + L"\\target\\", stop); }, "actual truncated target rejected");
    stop = true;
    Reject([&] { ListFiles(root + L"\\source\\", stop); }, "native file enumerate honors cancellation");
    std::cout << "PASS actual native file copy, Unicode paths, size verification, cancellation\n";
}
void StorageCompatibilityTests() {
    Require(IsUsbStorage(BusTypeUsb, {}), "reported USB bus accepted");
    Require(IsUsbStorage(BusTypeScsi, {L"SCSI\\DISK&VEN_TEST", L"USB\\VID_1234&PID_5678\\test"}),
            "SCSI bridge needs actual USB parent");
    Require(IsUsbStorage(BusTypeUnknown, {L"USBSTOR\\DISK&VEN_TEST"}), "USBSTOR device node accepted");
    Require(IsUsbStorage(BusTypeNvme, {L"PCI\\OTHER", L"usb\\vid_1234&pid_5678"}),
            "USB parent proof is case independent");
    Require(!IsUsbStorage(BusTypeScsi, {L"SCSI\\DISK&VEN_USB", L"PCI\\VEN_1234"}),
            "USB product text cannot authorize internal SCSI disk");
    Require(!IsUsbStorage(BusTypeNvme, {L"PCI\\VEN_1234", L"USB\\ROOT_HUB30"}),
            "USB root hub is not proof of USB storage ancestry");
    Require(!IsUsbStorage(BusTypeUnknown, {}), "unknown transport remains excluded");
    DISK_GEOMETRY_EX geometry{};
    geometry.Geometry.BytesPerSector = 512;
    geometry.DiskSize.QuadPart = 64 * GiB;
    std::vector<BYTE> bytes(FIELD_OFFSET(DISK_GEOMETRY_EX, Data));
    std::memcpy(bytes.data(), &geometry, bytes.size());
    Disk d;
    d.capacityKnown = false;
    Require(bytes.size() < sizeof geometry,
            "geometry test actually excludes optional placeholder and padding");
    ParseDiskGeometry(d, bytes);
    Require(d.size == 64 * GiB && d.sector == 512 && d.capacityKnown,
            "minimum valid geometry header accepted");
    bytes.resize(sizeof geometry + 128);
    ParseDiskGeometry(d, bytes);
    Require(d.size == 64 * GiB, "extended geometry accepted too");
    bytes.resize(FIELD_OFFSET(DISK_GEOMETRY_EX, Data) - 1);
    Reject([&] { ParseDiskGeometry(d, bytes); }, "truncated geometry rejected");
    geometry.Geometry.BytesPerSector = 4096;
    geometry.DiskSize.QuadPart = 128 * GiB;
    bytes.resize(FIELD_OFFSET(DISK_GEOMETRY_EX, Data));
    std::memcpy(bytes.data(), &geometry, bytes.size());
    ParseDiskGeometry(d, bytes);
    Require(d.size == 128 * GiB && d.sector == 4096, "native 4Kn geometry handled");
    geometry.DiskSize.QuadPart = -1;
    std::memcpy(bytes.data(), &geometry, bytes.size());
    Reject([&] { ParseDiskGeometry(d, bytes); }, "negative geometry rejected");
    STORAGE_DEVICE_DESCRIPTOR descriptor{};
    descriptor.Version = sizeof descriptor;
    descriptor.Size = FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties);
    descriptor.BusType = BusTypeUsb;
    bytes.resize(descriptor.Size);
    std::memcpy(bytes.data(), &descriptor, bytes.size());
    d.friendly = L"PnP fallback model";
    ParseStorageDescriptor(d, bytes);
    Require(d.reportedBus == BusTypeUsb && d.friendly == L"PnP fallback model",
            "minimum descriptor preserves fallback name");
    bytes.resize(FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) - 1);
    Reject([&] { ParseStorageDescriptor(d, bytes); }, "short descriptor rejected");
    Disk ready;
    ready.number = 3;
    ready.size = 64 * GiB;
    ready.sector = 512;
    ready.path = L"device";
    ready.pnp = L"USBSTOR\\TEST";
    Disk partial = ready;
    partial.attributesKnown = false;
    Require(!partial.eligible() && !SameIdentity(ready, partial),
            "unknown disk attributes cannot pass write rechecks");
    partial = ready;
    partial.layoutKnown = false;
    Require(!partial.eligible() && partial.Metadata().find(L"分区数未确认") != std::wstring::npos,
            "failed layout is visible as unknown, not zero partitions");
    partial = ready;
    partial.capacityKnown = false;
    Require(!partial.eligible() && partial.Metadata().find(L"容量未确认") != std::wstring::npos,
            "unknown capacity does not become zero-size disk");
    partial = ready;
    partial.probeErrors.push_back(L"simulated device query failure");
    Require(!partial.eligible() && !SameIdentity(ready, partial),
            "partial results never authorize disk writes");
    Catalog c;
    c.interfaces = 3;
    c.nonUsb = 1;
    c.disks = {ready, partial};
    auto json = DiskJson(c);
    Require(json.find(L"serial") == std::wstring::npos && json.find(ready.pnp) == std::wstring::npos,
            "device report excludes raw serial and PnP IDs");
    Require(json.find(L"probe_errors") != std::wstring::npos &&
                json.find(L"interface_count\":3") != std::wstring::npos,
            "diagnostics retain scan stages and partial failures");
    Fake backend;
    std::atomic<bool> stop = false;
    auto request = TestRequest();
    request.disk.probeErrors = {L"access denied"};
    auto result = RunWorkflow(request, backend, {}, stop);
    Require(!result.copied && !backend.Has(L"prepare") && !backend.Has(L"format"),
            "unknown device state stops before preparation or formatting");
    std::cout << "PASS USB ancestry, variable-length storage headers, partial-query safety and diagnostics\n";
}
void LayoutReadTests() {
    const size_t header = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
    std::vector<BYTE> bytes(header + 4 * sizeof(PARTITION_INFORMATION_EX));
    auto layout = (DRIVE_LAYOUT_INFORMATION_EX *)bytes.data();
    layout->PartitionStyle = PARTITION_STYLE_MBR;
    layout->Mbr.Signature = 123456;
    layout->PartitionCount = 4;
    Disk d;
    d.size = 64 * GiB;
    // Unused entries may contain stale lengths. A type-zero slot is not a partition.
    layout->PartitionEntry[0].PartitionLength.QuadPart = d.size;
    ParseDriveLayout(d, bytes);
    Require(d.layoutKnown && d.partitions.empty() && d.layoutEntries == 4 && d.tableId == L"123456",
            "production layout parser recognizes unused MBR slots with stale lengths");
    auto &p = layout->PartitionEntry[0];
    p.PartitionStyle = PARTITION_STYLE_MBR;
    p.Mbr.PartitionType = 0x0c;
    p.StartingOffset.QuadPart = MiB;
    p.PartitionLength.QuadPart = d.size - 2 * MiB;
    p.PartitionNumber = 1;
    ParseDriveLayout(d, bytes);
    Require(d.partitions.size() == 1 && d.partitions[0].offset == MiB,
            "production parser preserves a real MBR partition");
    p.StartingOffset.QuadPart = -1;
    Reject([&] { ParseDriveLayout(d, bytes); }, "negative actual partition offset is not silently empty");
    Require(!d.layoutKnown && d.partitions.empty(), "failed parser never leaves a verified layout");
    p.StartingOffset.QuadPart = d.size;
    Reject([&] { ParseDriveLayout(d, bytes); }, "out-of-disk actual partition extent rejected");
    layout->PartitionCount = 5;
    Reject([&] { ParseDriveLayout(d, bytes); }, "truncated partition array rejected");
    layout->PartitionCount = 0;
    bytes.resize(header);
    ParseDriveLayout(d, bytes);
    Require(d.layoutKnown && d.style == PARTITION_STYLE_MBR && d.partitions.empty(),
            "empty MBR accepts exactly its header without placeholder partition");
    layout = (DRIVE_LAYOUT_INFORMATION_EX *)bytes.data();
    layout->PartitionStyle = PARTITION_STYLE_RAW;
    ParseDriveLayout(d, bytes);
    Require(d.layoutKnown && d.style == PARTITION_STYLE_RAW && d.partitions.empty(),
            "raw zero-entry disk remains readable");
    layout->PartitionStyle = (PARTITION_STYLE)99;
    Reject([&] { ParseDriveLayout(d, bytes); }, "unknown disk layout style rejected");
    bytes.resize(header - 1);
    Reject([&] { ParseDriveLayout(d, bytes); }, "short layout header rejected");
    bytes.assign(header + sizeof(PARTITION_INFORMATION_EX), 0);
    layout = (DRIVE_LAYOUT_INFORMATION_EX *)bytes.data();
    layout->PartitionStyle = PARTITION_STYLE_GPT;
    layout->PartitionCount = 1;
    auto &gpt = layout->PartitionEntry[0];
    gpt.PartitionStyle = PARTITION_STYLE_GPT;
    gpt.PartitionLength.QuadPart = d.size;
    ParseDriveLayout(d, bytes);
    Require(d.layoutKnown && d.partitions.empty(), "zero GPT type denotes unused slot");
    std::cout << "PASS actual partition-buffer parsing, empty MBR/RAW and unused slots\n";
}
struct RebuildFixture {
    Disk expected;
    DWORD signature = 987654;
    uint64_t tick = 0;
    unsigned identityReads = 0, layoutReads = 0, initializations = 0, refreshes = 0;
    bool newPartition = false;
    std::atomic<bool> stop{false};
    std::wstring messages;
    DiskRebuildOps ops;
    Hooks hooks;
    RebuildFixture() {
        expected.number = 3;
        expected.size = 64 * GiB;
        expected.sector = 512;
        expected.path = L"fixture-only-interface";
        expected.pnp = L"USBSTOR\\FIXTURE-ONLY";
        expected.friendly = L"TEST USB";
        expected.serial = L"private-fixture-serial";
        expected.bootKnown = true;
        expected.style = PARTITION_STYLE_MBR;
        expected.tableId = L"111";
        Partition p;
        p.number = 1;
        p.offset = MiB;
        p.size = expected.size - 2 * MiB;
        p.mbrType = 0x0c;
        expected.partitions.push_back(p);
        ops.now = [&] { return tick; };
        ops.wait = [&](DWORD ms) { tick += ms; };
        ops.refresh = [&] { refreshes++; };
        ops.read = [&](bool layout) {
            if (layout) {
                layoutReads++;
                if (!initializations)
                    throw Error(L"test: raw layout query before initialization");
            } else
                identityReads++;
            return Snapshot(layout);
        };
        ops.initialize = [&](DWORD value) {
            if (value != signature || identityReads < 2)
                throw Error(L"test: initialization before verified identity");
            initializations++;
        };
        hooks.log = [&](const std::wstring &s) { messages += s + L"\n"; };
    }
    Disk Snapshot(bool layout) const {
        auto d = expected;
        d.partitions.clear();
        d.style = layout ? PARTITION_STYLE_MBR : PARTITION_STYLE_RAW;
        d.layoutKnown = layout;
        d.tableId = layout ? std::to_wstring(signature) : L"";
        return d;
    }
    Disk Run() {
        auto d = InitializeEmptyMbr(expected, signature, ops, hooks, stop);
        // Represents the caller's subsequent SET_DRIVE_LAYOUT_EX; never reached on failure.
        newPartition = true;
        return d;
    }
};
void RebuildTests(const std::wstring &root) {
    {
        RebuildFixture f;
        auto d = f.Run();
        Require(f.initializations == 1 && f.identityReads == 2 && f.layoutReads == 2 && f.newPartition &&
                    d.partitions.empty() && d.tableId == std::to_wstring(f.signature),
                "shared production sequence initializes before querying raw layout and verifies empty MBR");
        Require(f.messages.find(f.expected.serial) == f.messages.npos &&
                    f.messages.find(f.expected.pnp) == f.messages.npos,
                "rebuild logs omit raw serial and PnP identifiers");
        WriteUtf8(root + L"\\rebuild-normal.log", f.messages);
    }
    {
        RebuildFixture f;
        f.ops.read = [&](bool layout) {
            if (layout)
                f.layoutReads++;
            else
                f.identityReads++;
            auto d = f.Snapshot(layout);
            if (!layout && f.identityReads <= 3) {
                d.descriptorKnown = false;
                d.friendly = L"temporary device-node fallback name";
                d.probeErrors.push_back(L"device descriptor temporarily unavailable (21)");
            }
            if (layout && f.layoutReads <= 3) {
                d.layoutKnown = false;
                d.probeErrors.push_back(L"layout temporarily unavailable (21)");
            }
            return d;
        };
        f.Run();
        Require(f.identityReads == 5 && f.layoutReads == 5 && f.initializations == 1,
                "incomplete identity and layout observations retry and require two consecutive good reads");
        Require(f.messages.find(L"查询错误") != f.messages.npos, "transient probe reasons reach saved logs");
    }
    {
        RebuildFixture f;
        f.ops.read = [&](bool layout) {
            if (!layout) {
                f.identityReads++;
                return f.Snapshot(false);
            }
            f.layoutReads++;
            if (f.layoutReads == 1)
                return f.expected; // Complete, but stale old signature and partition.
            auto d = f.Snapshot(true);
            if (f.layoutReads == 2)
                d.tableId = L"111";
            if (f.layoutReads == 3)
                d.partitions = f.expected.partitions;
            return d;
        };
        f.Run();
        Require(f.layoutReads == 5 && f.initializations == 1,
                "old cached signature and remaining actual partitions never permit a new partition");
    }
    for (bool afterInitialize : {false, true})
        for (int change = 0; change < 9; change++) {
            RebuildFixture f;
            f.ops.read = [&](bool layout) {
                if (!layout)
                    f.identityReads++;
                auto d = f.Snapshot(layout);
                if (layout == afterInitialize) {
                    switch (change) {
                    case 0:
                        d.number++;
                        break;
                    case 1:
                        d.size -= MiB;
                        break;
                    case 2:
                        d.sector = 4096;
                        break;
                    case 3:
                        d.path = L"other interface";
                        break;
                    case 4:
                        d.pnp = L"other device";
                        break;
                    case 5:
                        d.serial = L"other serial";
                        break;
                    case 6:
                        d.friendly = L"other model";
                        break;
                    case 7:
                        d.readOnly = true;
                        break;
                    case 8:
                        d.offline = true;
                        break;
                    }
                    // A known conflict must abort even if another query is incomplete.
                    d.layoutKnown = false;
                    d.probeErrors.push_back(L"unrelated incomplete query");
                }
                return d;
            };
            Reject([&] { f.Run(); }, "known device conflict aborts instead of retrying into another write");
            Require(!f.newPartition && f.initializations == (afterInitialize ? 1u : 0u) &&
                        f.tick == (afterInitialize ? 200u : 0u),
                    "device conflict stops immediately before any subsequent write");
        }
    {
        RebuildFixture f;
        f.ops.read = [&](bool layout) {
            auto d = f.Snapshot(layout);
            d.descriptorKnown = false;
            d.probeErrors.push_back(L"persistent descriptor failure (21)");
            return d;
        };
        std::wstring error;
        try {
            f.Run();
        } catch (const Error &e) {
            error = e.text;
        }
        Require(!f.newPartition && !f.initializations && f.tick == 30000 &&
                    error.find(L"persistent descriptor failure (21)") != error.npos,
                "persistent incomplete identity times out without initializing and preserves exact error");
        WriteUtf8(root + L"\\rebuild-timeout.log", f.messages);
    }
    for (bool raw : {false, true}) {
        RebuildFixture f;
        f.ops.read = [&](bool layout) {
            if (!layout)
                f.identityReads++;
            auto d = f.Snapshot(layout);
            if (layout) {
                d.style = raw ? PARTITION_STYLE_RAW : PARTITION_STYLE_MBR;
                d.tableId = raw ? L"" : L"wrong-signature";
            }
            return d;
        };
        Reject([&] { f.Run(); }, "post-initialize raw or wrong-signature layout is never accepted");
        Require(f.initializations == 1 && !f.newPartition && f.tick == 30200,
                "empty-MBR timeout never retries initialization or establishes a partition");
    }
    {
        RebuildFixture f;
        f.ops.refresh = [&] {
            if (++f.refreshes < 4)
                throw Error(L"refresh temporarily busy (170)");
        };
        f.Run();
        Require(f.refreshes == 7 && f.initializations == 1 &&
                    f.messages.find(L"refresh temporarily busy (170)") != f.messages.npos,
                "refresh failure retries without allowing a write until successful refresh and identity");
    }
    {
        RebuildFixture f;
        f.ops.refresh = [] { throw Error(L"refresh not available (1)"); };
        Reject([&] { f.Run(); }, "persistent refresh failure cannot be silently ignored");
        Require(!f.initializations && !f.newPartition && f.tick == 30000,
                "refresh failure is bounded and prevents all following writes");
    }
    {
        RebuildFixture f;
        f.ops.read = [&](bool layout) {
            if (!layout)
                f.identityReads++;
            else if (++f.layoutReads < 31)
                throw Error(L"device temporarily not ready (21)");
            return f.Snapshot(layout);
        };
        f.Run();
        Require(f.layoutReads == 32 && f.tick < 10000 && f.messages.find(L"5 秒") != f.messages.npos,
                "throwing driver query recovers and logs a five-second heartbeat");
    }
    for (bool afterInitialize : {false, true}) {
        RebuildFixture f;
        if (afterInitialize)
            f.ops.initialize = [&](DWORD) {
                f.initializations++;
                f.stop = true;
            };
        else
            f.ops.wait = [&](DWORD ms) {
                f.tick += ms;
                f.stop = true;
            };
        bool cancelled = false;
        try {
            f.Run();
        } catch (const Cancelled &) {
            cancelled = true;
        }
        Require(cancelled && !f.newPartition && f.initializations == (afterInitialize ? 1u : 0u),
                "cancel before/after MBR initialization prevents any further partition creation");
    }
    {
        RebuildFixture f;
        f.ops.initialize = [&](DWORD) {
            f.initializations++;
            throw Error(L"initialize denied (5)");
        };
        Reject([&] { f.Run(); }, "failed disk initialization is not retried or bypassed");
        Require(f.initializations == 1 && !f.layoutReads && !f.newPartition,
                "initialization failure prevents later layout queries and partition writes");
    }
    {
        RebuildFixture f;
        f.expected.bootKnown = false;
        Reject([&] { f.Run(); }, "missing initial boot-disk mapping refuses initialization");
        Require(!f.initializations && !f.refreshes,
                "invalid starting identity fails before device operations");
    }
    std::cout << "PASS shared disk rebuild sequence: identity, empty MBR, driver delays, abort and timeout\n";
}
void SnapshotTests() {
    Disk a;
    a.number = 3;
    a.size = 64 * GiB;
    a.sector = 512;
    a.pnp = L"USBSTOR\\TEST";
    a.path = L"device-interface";
    a.serial = L"serial-1";
    a.friendly = L"TEST USB";
    a.style = PARTITION_STYLE_MBR;
    a.tableId = L"12345";
    Partition p;
    p.number = 1;
    p.offset = MiB;
    p.size = a.size - 2 * MiB;
    p.mbrType = 0x0c;
    p.active = true;
    a.partitions.push_back(p);
    Disk b = a;
    Require(SameIdentity(a, b), "identical device snapshot");
    b.number++;
    Require(!SameIdentity(a, b), "disk number reuse blocked");
    b = a;
    b.size++;
    Require(!SameIdentity(a, b), "capacity change blocked");
    b = a;
    b.sector = 4096;
    Require(!SameIdentity(a, b), "sector change blocked");
    b = a;
    b.pnp = L"OTHER";
    Require(!SameIdentity(a, b), "PnP change blocked");
    b = a;
    b.serial = L"OTHER";
    Require(!SameIdentity(a, b), "serial change blocked");
    b = a;
    b.path = L"OTHER";
    Require(!SameIdentity(a, b), "interface change blocked");
    b = a;
    b.friendly = L"OTHER";
    Require(!SameIdentity(a, b), "model change blocked");
    b = a;
    b.tableId = L"54321";
    Require(LayoutSignature(a) != LayoutSignature(b), "MBR disk signature change detected");
    b = a;
    b.partitions[0].offset += MiB;
    Require(LayoutSignature(a) != LayoutSignature(b), "partition extent change detected");
    b = a;
    b.partitions[0].active = false;
    Require(LayoutSignature(a) != LayoutSignature(b), "active flag change detected");
    std::cout << "PASS physical identity and partition snapshot guards\n";
}
void XmlTests() {
    auto d = ParseWimXml(
        L"<IMAGE INDEX=\"1\"><NAME>Windows 11 Education</NAME><DESCRIPTION>教育版 &amp; "
        L"test</DESCRIPTION><WINDOWS><ARCH>9</ARCH><EDITIONID>Education</EDITIONID><INSTALLATIONTYPE>Client</"
        L"INSTALLATIONTYPE><PRODUCTTYPE>WinNT</PRODUCTTYPE><VERSION><MAJOR>10</MAJOR><MINOR>0</"
        L"MINOR><BUILD>26300</BUILD><SPBUILD>9457</SPBUILD></VERSION></WINDOWS></IMAGE>",
        L"windows_11_26h2.iso");
    Require(d.label == L"WIN11_26H2" && d.version == L"10.0.26300.9457" && d.arch == 9,
            "native metadata policy");
    Require(d.description == L"教育版 & test", "native XML entities");
    Reject([] { ParseWimXml(L"<IMAGE><NAME>Windows 11</NAME></IMAGE>", L"windows_11_26h2.iso"); },
           "filename cannot bypass invalid metadata");
    std::cout << "PASS native WIM metadata parsing\n";
}
void WimReadTests(const std::wstring &root, const std::wstring &fixture) {
    auto source = root + L"\\WIM source 中文";
    auto image = source + L"\\install.wim";
    auto scratch = root + L"\\WIM task 中文\\wim-temp";
    MakeDirectory(source);
    Require(CopyFileW(fixture.c_str(), image.c_str(), TRUE) != FALSE, "copy synthetic WIM fixture");
    Require(SetFileAttributesW(image.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
            "WIM metadata source is read-only like mounted ISO");
    std::vector<std::wstring> messages;
    Hooks h;
    h.log = [&](const std::wstring &s) { messages.push_back(s); };
    auto d = ReadWimIdentity(image, L"windows_11_26h2.iso", scratch, h);
    Require(d.label == L"WIN11_26H2" && d.version == L"10.0.26300.9457" && d.arch == 9,
            "real WIMGAPI index 1 metadata read succeeds using task scratch");
    Require(!messages.empty() && messages[0].find(scratch) != std::wstring::npos,
            "actual WIM scratch directory logged");
    Handle exclusive(CreateFileW(image.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    Require(exclusive.valid(), "WIM metadata handles closed after successful read");
    exclusive.reset();
    auto notDirectory = root + L"\\scratch-is-a-file.txt";
    WriteUtf8(notDirectory, L"not a directory");
    Reject([&] { ReadWimIdentity(image, L"windows_11_26h2.iso", notDirectory); },
           "WIM scratch file rejected before loading image");
    Reject([&] { ReadWimIdentity(image, L"windows_11_26h2.iso", L""); },
           "WIM scratch cannot fall back to default current directory");
    auto invalidImage = root + L"\\invalid.wim";
    WriteUtf8(invalidImage, L"not a WIM image");
    Reject([&] { ReadWimIdentity(invalidImage, L"windows_11_26h2.iso", scratch); },
           "invalid WIM stays a preflight error");
    // A per-process missing TEMP must not override the explicitly selected WIM scratch.
    wchar_t oldTemp[32768];
    DWORD oldLength = GetEnvironmentVariableW(L"TEMP", oldTemp, 32768);
    struct RestoreTemp {
        const wchar_t *value;
        ~RestoreTemp() { SetEnvironmentVariableW(L"TEMP", value); }
    } restore{oldLength && oldLength < 32768 ? oldTemp : nullptr};
    Require(SetEnvironmentVariableW(L"TEMP", (root + L"\\missing-temp").c_str()) != FALSE,
            "set missing TEMP only inside test process");
    d = ReadWimIdentity(image, L"windows_11_26h2.iso", scratch);
    Require(d.label == L"WIN11_26H2", "explicit WIM scratch works with unusable process TEMP");
    std::atomic<bool> stop = false;
    auto files = ListFiles(scratch + L"\\", stop);
    Require(std::none_of(files.begin(), files.end(),
                         [](const File &f) { return BaseName(f.full).rfind(L"write-check-", 0) == 0; }),
            "WIM scratch write-check files do not remain after return or errors");
    std::cout
        << "PASS real read-only WIMGAPI fixture, Unicode task scratch, invalid scratch and missing TEMP\n";
}
void WimTaskTests(const std::wstring &root, const std::wstring &fixture) {
    auto work = root + L"\\fresh task 中文";
    auto scratch = work + L"\\wim-temp";
    MakeDirectory(work);
    Require(GetFileAttributesW(scratch.c_str()) == INVALID_FILE_ATTRIBUTES,
            "task integration begins without a WIM scratch directory");
    Disk otherDisk;
    otherDisk.number = MAXDWORD; // No physical writes; only actual path-to-volume queries.
    auto d = ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", work, otherDisk);
    Require(d.label == L"WIN11_26H2" && d.version == L"10.0.26300.9457",
            "production task preflight creates and verifies scratch before real WIM read");
    DWORD attributes = GetFileAttributesW(scratch.c_str());
    Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY),
            "task WIM scratch was created on first run");
    auto disks = PathDisks(work);
    Require(!disks.empty() && PathDisks(scratch) == disks,
            "created scratch actual physical volume matches task volume");
    auto blockedWork = root + L"\\task-on-target";
    MakeDirectory(blockedWork);
    Disk sameDisk;
    sameDisk.number = disks[0];
    Reject([&] { ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", blockedWork, sameDisk); },
           "task preflight refuses target-disk scratch before creating it");
    Require(GetFileAttributesW((blockedWork + L"\\wim-temp").c_str()) == INVALID_FILE_ATTRIBUTES,
            "target-disk rejection has no scratch creation side effect");
    Reject([&] { ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", L"", otherDisk); },
           "task preflight rejects an unspecified work directory");
    Reject([&] { ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", root + L"\\missing-task", otherDisk); },
           "task preflight requires an existing validated parent");
    auto blockedChild = root + L"\\task-child-is-file";
    MakeDirectory(blockedChild);
    WriteUtf8(blockedChild + L"\\wim-temp", L"not a directory");
    Reject([&] { ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", blockedChild, otherDisk); },
           "task preflight rejects a non-directory scratch child");
    d = ReadTaskWimIdentity(fixture, L"windows_11_26h2.iso", work, otherDisk);
    Require(d.label == L"WIN11_26H2", "task preflight can reuse its own existing scratch directory");
    std::cout << "PASS production task WIM preflight from absent scratch through actual volume checks and "
                 "WIMGAPI\n";
}
void WimApiTest(const std::wstring &root) {
    HANDLE rawToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &rawToken))
        throw Error(WinError());
    Handle token(rawToken);
    for (auto name : {SE_BACKUP_NAME, SE_RESTORE_NAME}) {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid))
            throw Error(WinError());
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof privileges, nullptr, nullptr) ||
            GetLastError() == ERROR_NOT_ALL_ASSIGNED)
            throw Error(L"WIM fixture test needs administrator backup/restore privileges.");
    }
    Module wim(L"wimgapi.dll");
    auto create = wim.Proc<HANDLE(WINAPI *)(PCWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD)>("WIMCreateFile");
    auto capture = wim.Proc<HANDLE(WINAPI *)(HANDLE, PCWSTR, DWORD)>("WIMCaptureImage");
    auto temp = wim.Proc<BOOL(WINAPI *)(HANDLE, PCWSTR)>("WIMSetTemporaryPath");
    auto info = wim.Proc<BOOL(WINAPI *)(HANDLE, PVOID, DWORD)>("WIMSetImageInformation");
    auto close = wim.Proc<BOOL(WINAPI *)(HANDLE)>("WIMCloseHandle");
    DWORD disposition = 0;
    auto path = root + L"\\fixture.wim";
    HANDLE file = create(path.c_str(), GENERIC_READ | GENERIC_WRITE, CREATE_NEW, 0, 0, &disposition);
    if (!file || file == INVALID_HANDLE_VALUE)
        throw Error(L"Fixture WIM create: " + WinError());
    if (!temp(file, root.c_str())) {
        close(file);
        throw Error(L"Fixture WIM temp: " + WinError());
    }
    HANDLE image = capture(file, (root + L"\\source").c_str(), 0);
    if (!image || image == INVALID_HANDLE_VALUE) {
        auto e = GetLastError();
        close(file);
        throw Error(L"Fixture WIM capture: " + WinError(e));
    }
    std::wstring xml = L"\xFEFF<IMAGE INDEX=\"1\"><NAME>Windows 11 Education</NAME><DESCRIPTION>Native test "
                       L"fixture</DESCRIPTION><WINDOWS><ARCH>9</ARCH><EDITIONID>Education</"
                       L"EDITIONID><INSTALLATIONTYPE>Client</INSTALLATIONTYPE><PRODUCTTYPE>WinNT</"
                       L"PRODUCTTYPE><VERSION><MAJOR>10</MAJOR><MINOR>0</MINOR><BUILD>26300</"
                       L"BUILD><SPBUILD>9457</SPBUILD></VERSION></WINDOWS></IMAGE>";
    BOOL ok = info(image, xml.data(), (DWORD)((xml.size() + 1) * sizeof(wchar_t)));
    DWORD error = GetLastError();
    close(image);
    close(file);
    if (!ok)
        throw Error(L"Fixture WIM metadata: " + WinError(error));
    auto d = ReadWimIdentity(path, L"windows_11_26h2.iso", root + L"\\capture-read-temp");
    Require(d.label == L"WIN11_26H2" && d.version == L"10.0.26300.9457" && d.arch == 9,
            "actual native WIMGAPI metadata roundtrip");
    std::cout << "PASS actual native WIM capture and read-only metadata roundtrip\n";
}
void WorkspaceTests(const std::wstring &root) {
    Disk outside;
    outside.number = MAXDWORD;
    auto task = TaskWorkspace::Create(root, outside);
    auto staging = task->Directory(L"staging");
    auto wim = task->Directory(L"wim-temp");
    auto scratch = task->Directory(L"dism-temp");
    Require(PathDisks(staging) == PathDisks(task->Root()) && PathDisks(wim) == PathDisks(scratch),
            "all created task directories map to the verified physical volume");
    Require(!MoveFileW(task->Root().c_str(), (task->Root() + L"-moved").c_str()),
            "live task root is pinned against rename");
    Require(!MoveFileW(staging.c_str(), (staging + L"-moved").c_str()),
            "live task child is pinned against rename");
    for (auto name : {L"../escape", L"..\\escape", L"C:\\escape", L"a\\b", L""})
        Reject([&] { task->Directory(name); }, "task child traversal or absolute name rejected");
    Reject([&] { CheckWritableDirectory(staging, UINT64_MAX); }, "insufficient scratch capacity rejected");
    std::atomic<bool> stop = false;
    Require(ListFiles(staging + L"\\", stop).empty(), "write probes do not contaminate SWM staging");
    auto source = root + L"\\split-input.wim";
    WriteUtf8(source, L"synthetic split input");
    Prepared p;
    p.work = task->Root();
    p.staging = staging;
    p.plan.wim = source;
    auto args = PrepareSplitArguments(p, *task);
    Require(std::find(args.begin(), args.end(), L"/ScratchDir:" + scratch) != args.end(),
            "production DISM arguments use existing dedicated local scratch");
    Require(std::find(args.begin(), args.end(), L"/FileSize:4000") != args.end(),
            "audited split retains original 4000 MiB DISM setting");
    WriteUtf8(staging + L"\\unexpected.txt", L"keep");
    Reject([&] { PrepareSplitArguments(p, *task); }, "staging contamination stops before DISM launch");
    Require(DeleteFileW((staging + L"\\unexpected.txt").c_str()) != FALSE, "remove own test marker");
    auto other = TaskWorkspace::Create(root, outside);
    auto otherStage = other->Directory(L"staging");
    WriteUtf8(staging + L"\\install.swm", L"first");
    WriteUtf8(staging + L"\\install2.swm", L"second");
    WriteUtf8(otherStage + L"\\install2.swm", L"protect other job");
    std::vector<File> swm = {{staging + L"\\install.swm", L"install.swm", 5},
                             {staging + L"\\install2.swm", L"install2.swm", 6}};
    auto mixed = swm;
    mixed[1].full = otherStage + L"\\install2.swm";
    mixed[1].size = FileSize(mixed[1].full);
    Reject([&] { task->CleanupSwm(mixed); }, "SWM from another task cannot be deleted");
    Require(FileExists(swm[0].full) && FileExists(mixed[1].full),
            "entire cleanup manifest is validated before any file deletion");
    auto changed = swm;
    changed[1].size++;
    Reject([&] { task->CleanupSwm(changed); }, "changed temporary SWM is not deleted");
    Require(FileExists(swm[0].full), "size mismatch preserves previously validated SWM too");
    task->CleanupSwm(swm);
    Require(!FileExists(swm[0].full) && !FileExists(swm[1].full) && FileExists(mixed[1].full),
            "handle-based SWM cleanup removes only selected task files");
    task->Validate();
    Disk same;
    same.number = PathDisks(root)[0];
    auto before = ListFiles(root + L"\\", stop).size();
    Reject([&] { TaskWorkspace::Create(root, same); },
           "work base on target disk rejected before task creation");
    Require(before == ListFiles(root + L"\\", stop).size(), "target-disk rejection creates no probe files");
    Disk d;
    d.size = 64 * GiB;
    d.sector = 512;
    Require(PlannedPartitionSize(d) == d.size - 2 * MiB,
            "full capacity partition includes alignment reserve");
    d.size = 2 * 1024 * GiB;
    Require(PlannedPartitionSize(d) < d.size, "2 TiB MBR limit is checked before destructive calls");
    d.sector = 4096;
    Require(PlannedPartitionSize(d) % d.sector == 0, "4Kn full partition remains sector aligned");
    d.sector = 0;
    Reject([&] { PlannedPartitionSize(d); }, "unknown sector size rejected before partition arithmetic");
    d.sector = 512;
    d.size = 4 * GiB;
    Reject([&] { PlannedPartitionSize(d); }, "unsupported capacity rejected before clearing disk");
    d.size = 64 * GiB;
    d.style = PARTITION_STYLE_MBR;
    Require(!CreatedPartitionReady(d, MiB, 62 * GiB), "zero partition cache is not new volume readiness");
    Partition partition;
    partition.offset = MiB;
    partition.size = 62 * GiB;
    d.partitions.push_back(partition);
    Require(CreatedPartitionReady(d, MiB, 62 * GiB), "exact created partition permits volume lookup");
    d.partitions[0].size++;
    Require(!CreatedPartitionReady(d, MiB, 62 * GiB), "changed partition extent is rejected");
    std::cout
        << "PASS actual task directory pins, DISM scratch, strict SWM cleanup and partition readiness\n";
}
struct FileWorkflow final : Backend {
    std::wstring base, source, output;
    bool split, formatted = false, copied = false, ejected = false, failPreflight = false;
    std::unique_ptr<TaskWorkspace> task;
    FileWorkflow(std::wstring b, std::wstring s, bool split)
        : base(std::move(b)), source(std::move(s)), split(split) {
        output = base + L"\\output\\";
    }
    Prepared Prepare(const Request &r, const Hooks &h, std::atomic<bool> &stop) override {
        task = TaskWorkspace::Create(base, r.disk);
        Prepared p;
        p.work = task->Root();
        if (h.workReady)
            h.workReady(p.work);
        p.staging = task->Directory(L"staging");
        task->Directory(L"wim-temp");
        p.source = source;
        p.plan = PlanImages(source, ListFiles(source, stop));
        p.image = ReadTaskWimIdentity(p.plan.images[0].full, r.iso, p.work, r.disk, h);
        // Test-only split branch: DISM and physical format are replaced; file operations are real.
        p.plan.split = split;
        if (split) {
            auto args = PrepareImageInfoArguments(p, *task);
            args.insert(args.begin(), {L"--child-args", p.work + L"\\preflight-args.json"});
            if (failPreflight)
                args = {L"--child-fail"};
            CommandRunner command;
            command.Run(ExePath(), args, p.work, p.work + L"\\dism-preflight.log", h, stop, 10000);
        }
        return p;
    }
    void Validate(const Request &, const Prepared &, bool) override { task->Validate(); }
    std::wstring Format(const Request &, const Prepared &, const Hooks &, std::atomic<bool> &) override {
        formatted = true;
        MakeDirectory(output);
        return output;
    }
    std::vector<File> Split(const Prepared &p, const Hooks &h, std::atomic<bool> &stop) override {
        auto args = PrepareSplitArguments(p, *task);
        args.insert(args.begin(), {L"--child-split-fixture", p.staging});
        CommandRunner command;
        command.Run(ExePath(), args, p.work, p.work + L"\\split-command.log", h, stop, 10000);
        auto swm = ListFiles(p.staging + L"\\", stop);
        CheckSwm(swm);
        return swm;
    }
    void Copy(const Request &, const Prepared &p, const std::wstring &, const std::vector<File> &swm,
              const Hooks &h, std::atomic<bool> &stop) override {
        CopyFilesNative(InstallationFiles(p, swm), output, h, stop);
        copied = true;
    }
    void Verify(const Request &, const Prepared &p, const std::wstring &, const std::vector<File> &swm,
                std::atomic<bool> &stop) override {
        VerifyFilesNative(InstallationFiles(p, swm), output, stop);
    }
    void Cleanup(const Prepared &, const std::vector<File> &swm) override { task->CleanupSwm(swm); }
    void EjectUsb(const Request &, const std::wstring &) override { ejected = true; }
    void EjectIso() override {}
    void Abort(bool) noexcept override {}
};
void FileWorkflowTests(const std::wstring &root, const std::wstring &fixture) {
    for (auto fs : {L"FAT32", L"NTFS"})
        for (bool split : {false, true}) {
            auto base = root + L"\\file-flow-" + fs + (split ? L"-split" : L"-small");
            auto source = base + L"\\media\\";
            MakeDirectory(source + L"sources");
            MakeDirectory(source + L"efi\\boot");
            WriteUtf8(source + L"setup.exe", L"test setup");
            WriteUtf8(source + L"sources\\boot.wim", L"test boot");
            WriteUtf8(source + L"efi\\boot\\bootx64.efi", L"test EFI");
            Require(CopyFileW(fixture.c_str(), (source + L"sources\\install.wim").c_str(), TRUE) != FALSE,
                    "prepare real metadata fixture for six-stage file integration");
            auto request = TestRequest(fs);
            request.disk.number = MAXDWORD;
            request.iso = L"windows_11_26h2.iso";
            FileWorkflow backend(base, source, split);
            std::atomic<bool> stop = false;
            Hooks h;
            h.ask = [](Prompt, const std::wstring &) { return true; };
            auto result = RunWorkflow(request, backend, h, stop);
            Require(result.copied && result.error.empty() && backend.ejected,
                    "six-stage file integration completes with shared production path functions");
            Require(FileExists(backend.output + L"sources\\install.wim") != split &&
                        FileExists(backend.output + L"sources\\install.swm") == split,
                    "shared production copy set replaces WIM only for split branch on FAT32 and NTFS");
            std::atomic<bool> scan = false;
            Require(ListFiles(backend.task->Directory(L"staging") + L"\\", scan).empty(),
                    "post-copy cleanup clears own SWM while copied installation files remain");
        }
    auto failureBase = root + L"\\file-flow-preflight-failure";
    MakeDirectory(failureBase);
    FileWorkflow failure(failureBase, root + L"\\file-flow-FAT32-small\\media\\", true);
    failure.failPreflight = true;
    auto request = TestRequest();
    request.disk.number = MAXDWORD;
    request.iso = L"windows_11_26h2.iso";
    Hooks hooks;
    hooks.ask = [](Prompt, const std::wstring &) { return true; };
    std::atomic<bool> stop = false;
    auto result = RunWorkflow(request, failure, hooks, stop);
    Require(!result.error.empty() && !failure.formatted && !failure.copied,
            "failed component preflight command stops before any format or copy call");
    Require(FileSize(result.work + L"\\dism-preflight.log") > 0,
            "preflight command failure preserves its diagnostic output");
    std::cout << "PASS six-stage FAT32/NTFS file integration; format, DISM data and eject are test doubles\n";
}
void JunctionTests(const std::wstring &root) {
    auto work = root + L"\\junction-scope";
    auto protectedDirectory = root + L"\\junction-protected";
    MakeDirectory(work);
    MakeDirectory(protectedDirectory);
    WriteUtf8(protectedDirectory + L"\\install.swm", L"keep outside task");
    Disk outside;
    outside.number = MAXDWORD;
    TaskWorkspace task(work, outside);
    auto staging = task.Directory(L"staging");
    std::wstring print = FullPath(protectedDirectory), substitute = L"\\??\\" + print;
    size_t firstBytes = substitute.size() * sizeof(wchar_t), printBytes = print.size() * sizeof(wchar_t);
    std::vector<BYTE> buffer(16 + firstBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t));
    auto tag = (DWORD *)buffer.data();
    *tag = IO_REPARSE_TAG_MOUNT_POINT;
    auto fields = (USHORT *)(buffer.data() + 4);
    fields[0] = (USHORT)(buffer.size() - 8);
    fields[2] = 0;
    fields[3] = (USHORT)firstBytes;
    fields[4] = (USHORT)(firstBytes + sizeof(wchar_t));
    fields[5] = (USHORT)printBytes;
    std::memcpy(buffer.data() + 16, substitute.data(), firstBytes);
    std::memcpy(buffer.data() + 16 + firstBytes + sizeof(wchar_t), print.data(), printBytes);
    Handle directory(CreateFileW(staging.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                 nullptr));
    DWORD returned = 0;
    if (!directory.valid() || !DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                               (DWORD)buffer.size(), nullptr, 0, &returned, nullptr)) {
        std::cout << "NOT RUN junction-mutation fixture: " << Utf8(WinError()) << '\n';
        return;
    }
    directory.reset();
    Reject([&] { task.Validate(); }, "live task rejects a child changed into an actual NTFS junction");
    Reject([&] { task.CleanupSwm({{staging + L"\\install.swm", L"install.swm", 17}}); },
           "junction cannot redirect cleanup outside this task");
    Require(Read(protectedDirectory + L"\\install.swm") == L"keep outside task",
            "real junction fixture preserves file outside task");
    std::cout << "PASS real NTFS junction replacement and cleanup scope guard\n";
}
void ProcessTests(const std::wstring &root) {
    CommandRunner run;
    std::atomic<bool> stop = false;
    Hooks h;
    auto report = root + L"\\argv.json";
    std::vector<std::wstring> args = {L"--child-args", report,  L"中文 path with spaces",
                                      L"trailing\\",   L"a\"b", L"$() & | `literal`"};
    run.Run(ExePath(), args, root, root + L"\\child.log", h, stop, 10000);
    std::wstring expected = L"[";
    for (size_t i = 2; i < args.size(); i++) {
        if (i != 2)
            expected += L",";
        expected += Json(args[i]);
    }
    expected += L"]";
    Require(Read(report) == expected, "native command quoting preserves literal argv, no shell evaluation");
    auto tick = GetTickCount64();
    Reject([&] { run.Run(ExePath(), {L"--child-sleep"}, root, L"", h, stop, 200); },
           "native child command timeout");
    Require(GetTickCount64() - tick < 5000, "native timeout is bounded");
    stop = false;
    std::thread cancel([&] {
        Sleep(200);
        stop = true;
    });
    Reject([&] { run.Run(ExePath(), {L"--child-sleep"}, root, L"", h, stop, 10000); },
           "native job cancellation");
    cancel.join();
    stop = false;
    auto descendantReport = root + L"\\descendant.pid";
    Reject([&] { run.Run(ExePath(), {L"--child-tree", descendantReport}, root, L"", h, stop, 1000); },
           "native process tree timeout");
    DWORD descendant = (DWORD)std::stoul(Read(descendantReport));
    Handle descendantHandle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, descendant));
    Require(!descendantHandle.valid() || WaitForSingleObject(descendantHandle, 2000) == WAIT_OBJECT_0,
            "Job cancellation leaves no descendant process");
    auto marker = root + L"\\should-not-start.txt";
    Reject([&] { run.Run(ExePath(), {L"--child-mark", marker}, root, root, h, stop, 10000); },
           "invalid log destination stops before launching command");
    Require(!FileExists(marker), "log preflight failure never starts child");
    auto liveLog = root + L"\\live-command.log";
    bool sawLiveLog = false;
    h.log = [&](const std::wstring &) {
        sawLiveLog = FileExists(liveLog) && FileSize(liveLog) > 0;
        stop = true;
    };
    Reject([&] { run.Run(ExePath(), {L"--child-noisy"}, root, liveLog, h, stop, 10000); },
           "continuous command output still accepts cancellation");
    Require(sawLiveLog && FileSize(liveLog) > 0,
            "command log is visible before exit and survives cancellation");
    stop = false;
    tick = GetTickCount64();
    Reject(
        [&] { run.Run(ExePath(), {L"--child-noisy"}, root, root + L"\\noisy-timeout.log", {}, stop, 200); },
        "continuous command output still enforces timeout");
    Require(GetTickCount64() - tick < 5000, "busy output cannot starve command timeout checks");
    std::cout << "PASS actual native process argv, timeout and owned-job cancellation\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--child-fail") {
            std::cout << "injected component preflight error\n";
            return 7;
        }
        if (argc >= 3 && std::wstring(argv[1]) == L"--child-split-fixture") {
            WriteUtf8(std::wstring(argv[2]) + L"\\install.swm", L"synthetic first SWM");
            WriteUtf8(std::wstring(argv[2]) + L"\\install2.swm", L"synthetic second SWM");
            std::cout << "test split output ready\n";
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--child-mark") {
            WriteUtf8(argv[2], L"started");
            return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--child-noisy") {
            for (;;) {
                std::cout << std::string(4095, 'N') << '\n' << std::flush;
                Sleep(1);
            }
        }
        if (argc >= 3 && std::wstring(argv[1]) == L"--child-args") {
            std::wstring s = L"[";
            for (int i = 3; i < argc; i++) {
                if (i > 3)
                    s += L",";
                s += Json(argv[i]);
            }
            WriteUtf8(argv[2], s + L"]");
            return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--child-sleep") {
            Sleep(30000);
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--child-tree") {
            STARTUPINFOW startup{sizeof startup};
            PROCESS_INFORMATION process{};
            std::wstring line = QuoteArg(ExePath()) + L" --child-sleep";
            if (!CreateProcessW(ExePath().c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                nullptr, nullptr, &startup, &process))
                return 1;
            Handle child(process.hProcess), thread(process.hThread);
            WriteUtf8(argv[2], std::to_wstring(process.dwProcessId));
            Sleep(30000);
            return 0;
        }
        if (argc != 3 && argc != 4)
            throw std::runtime_error("usage: tests fixtures.tsv unique-output-directory [--wim-roundtrip]");
        std::wstring root = FullPath(argv[2]);
        if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("test directory must not already exist");
        MakeDirectory(root);
        IdentityTests(argv[1]);
        PlanTests();
        WorkflowTests();
        SnapshotTests();
        StorageCompatibilityTests();
        LayoutReadTests();
        RebuildTests(root);
        XmlTests();
        FileTests(root);
        WimReadTests(root, Parent(FullPath(argv[1])) + L"\\fixtures\\metadata.wim");
        WimTaskTests(root, Parent(FullPath(argv[1])) + L"\\fixtures\\metadata.wim");
        WorkspaceTests(root);
        FileWorkflowTests(root, Parent(FullPath(argv[1])) + L"\\fixtures\\metadata.wim");
        if (argc == 4 && std::wstring(argv[3]) == L"--wim-roundtrip")
            WimApiTest(root);
        else
            std::cout << "NOT RUN WIM capture roundtrip: requires administrator backup privilege; use "
                         "--wim-roundtrip\n";
        ProcessTests(root);
        JunctionTests(root);
        std::cout << "PASS " << checks << " assertions; no physical disks were written\n";
        WriteUtf8(root + L"\\result.txt",
                  L"PASS " + std::to_wstring(checks) + L" assertions; no physical disks written\r\n");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}
