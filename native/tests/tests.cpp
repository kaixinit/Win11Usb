#include "platform.hpp"
#include <algorithm>
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
    Reject([&] { CopyFilesNative(files, root + L"\\target\\", h, stop); },
           "native copy refuses to overwrite unknown existing file");
    WriteUtf8(root + L"\\target\\boot.bin", L"corrupt length");
    Reject([&] { VerifyFilesNative(files, root + L"\\target\\", stop); }, "actual truncated target rejected");
    stop = true;
    Reject([&] { ListFiles(root + L"\\source\\", stop); }, "native file enumerate honors cancellation");
    std::cout << "PASS actual native file copy, Unicode paths, size verification, cancellation\n";
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
    auto d = ReadWimIdentity(path, L"windows_11_26h2.iso");
    Require(d.label == L"WIN11_26H2" && d.version == L"10.0.26300.9457" && d.arch == 9,
            "actual native WIMGAPI metadata roundtrip");
    std::cout << "PASS actual native WIM capture and read-only metadata roundtrip\n";
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
    std::cout << "PASS actual native process argv, timeout and owned-job cancellation\n";
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
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
        XmlTests();
        FileTests(root);
        if (argc == 4 && std::wstring(argv[3]) == L"--wim-roundtrip")
            WimApiTest(root);
        else
            std::cout << "NOT RUN WIM capture roundtrip: requires administrator backup privilege; use "
                         "--wim-roundtrip\n";
        ProcessTests(root);
        std::cout << "PASS " << checks << " assertions; no physical disks were written\n";
        WriteUtf8(root + L"\\result.txt",
                  L"PASS " + std::to_wstring(checks) + L" assertions; no physical disks written\r\n");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}
