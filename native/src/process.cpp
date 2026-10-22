#include "platform.hpp"
#include <algorithm>

namespace wu {
// This is Windows argv quoting, not a shell command. No cmd.exe or script host.
std::wstring QuoteArg(const std::wstring &s) {
    std::wstring r = L"\"";
    size_t slash = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            slash++;
            continue;
        }
        if (c == L'\"') {
            r.append(slash * 2 + 1, L'\\');
            r += c;
        } else {
            r.append(slash, L'\\');
            r += c;
        }
        slash = 0;
    }
    r.append(slash * 2, L'\\');
    return r + L"\"";
}
void CommandRunner::Kill() noexcept {
    // Force-exit must not wait on a CreateProcess call held up by a system filter.
    // The job also has KILL_ON_JOB_CLOSE, so process exit is the final backstop.
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() && job.valid())
        TerminateJobObject(job, ERROR_CANCELLED);
}
DWORD CommandRunner::Run(const std::wstring &program, const std::vector<std::wstring> &args,
                         const std::wstring &cwd, const std::wstring &logfile, const Hooks &h,
                         std::atomic<bool> &stop, DWORD timeout, const std::string &input) {
    CheckCancel(stop);
    if (!FileExists(program))
        throw Error(L"所需命令不存在：" + program + L"。尚未执行。 ");
    if (!cwd.empty()) {
        DWORD attributes = GetFileAttributesW(cwd.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw Error(L"命令工作目录不存在或类型无效，尚未执行：" + cwd);
    }
    Handle commandLog;
    if (!logfile.empty()) {
        commandLog.reset(CreateFileW(logfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!commandLog.valid())
            throw Error(L"无法预先创建命令日志，尚未执行：" + logfile + L"\r\n" + WinError());
    }
    struct CloseJob {
        std::mutex &mutex;
        Handle &job;
        ~CloseJob() {
            std::lock_guard<std::mutex> lock(mutex);
            job.reset();
        }
    } closeJob{mutex, job};
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE oraw, owraw, iraw, iwraw;
    if (!CreatePipe(&oraw, &owraw, &sa, 0))
        throw Error(WinError());
    Handle output(oraw), writer(owraw);
    if (!SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0))
        throw Error(WinError());
    if (!CreatePipe(&iraw, &iwraw, &sa, 0))
        throw Error(WinError());
    Handle in(iraw), inputWriter(iwraw);
    if (!SetHandleInformation(inputWriter, HANDLE_FLAG_INHERIT, 0))
        throw Error(WinError());
    SIZE_T count = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &count);
    std::vector<BYTE> attributes(count);
    auto list = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
    if (!InitializeProcThreadAttributeList(list, 1, 0, &count))
        throw Error(WinError());
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST p;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(p); }
    } cleanup{list};
    HANDLE inherit[] = {writer.h, in.h};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit,
                                   nullptr, nullptr))
        throw Error(WinError());
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = writer;
    si.StartupInfo.hStdError = writer;
    si.lpAttributeList = list;
    PROCESS_INFORMATION pi{};
    std::wstring line = QuoteArg(program);
    for (auto &a : args)
        line += L" " + QuoteArg(a);
    {
        std::lock_guard<std::mutex> lock(mutex);
        CheckCancel(stop);
        job.reset(CreateJobObjectW(nullptr, nullptr));
        if (!job.valid())
            throw Error(WinError());
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit))
            throw Error(WinError());
        if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                            cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi))
            throw Error(L"命令无法启动：" + WinError());
        Handle thread(pi.hThread);
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            DWORD e = GetLastError();
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess);
            throw Error(L"无法建立本次命令的退出管理：" + WinError(e));
        }
        if (ResumeThread(thread) == (DWORD)-1) {
            TerminateJobObject(job, 1);
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess);
            throw Error(L"命令无法恢复执行。");
        }
    }
    Handle process(pi.hProcess);
    // Retain real process handles while they are observable. A job's ActiveProcesses
    // count alone is not proof that an observed process has completed pending I/O.
    std::map<DWORD, Handle> members;
    auto rememberMembers = [&] {
        std::vector<BYTE> data(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 64 * sizeof(ULONG_PTR));
        for (;;) {
            if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, data.data(), (DWORD)data.size(),
                                          nullptr))
                break;
            if (GetLastError() != ERROR_MORE_DATA || data.size() >= 1024 * 1024)
                throw Error(L"无法核验本次命令的进程组：" + WinError());
            data.resize(data.size() * 2);
        }
        auto list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(data.data());
        for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
            DWORD pid = (DWORD)list->ProcessIdList[i];
            auto found = members.find(pid);
            if (found != members.end() && WaitForSingleObject(found->second, 0) != WAIT_OBJECT_0)
                continue;
            Handle member(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            if (!member.valid()) {
                if (GetLastError() == ERROR_INVALID_PARAMETER)
                    continue; // Already exited before its handle could be retained.
                throw Error(L"无法核验本次命令的子进程：" + WinError());
            }
            BOOL belongs = FALSE;
            if (!IsProcessInJob(member, job, &belongs))
                throw Error(L"无法复核子进程所属任务：" + WinError());
            if (belongs)
                members[pid] = std::move(member);
        }
    };
    bool reaped = false, stopAttempted = false;
    auto stopTree = [&] {
        if (reaped)
            return;
        stopAttempted = true;
        rememberMembers();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!TerminateJobObject(job, ERROR_CANCELLED))
                throw Error(L"无法结束本次命令的进程组：" + WinError());
        }
        auto deadline = GetTickCount64() + 10000;
        for (;;) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION state{};
            if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &state, sizeof state,
                                           nullptr))
                throw Error(L"无法复核命令退出状态：" + WinError());
            bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
            for (const auto &entry : members)
                exited = (WaitForSingleObject(entry.second, 0) == WAIT_OBJECT_0) && exited;
            if (exited && state.ActiveProcesses == 0) {
                reaped = true;
                return;
            }
            if (GetTickCount64() >= deadline)
                throw Error(L"命令的进程组未在 10 秒内确认退出，停止后续操作。日志：" + logfile);
            Sleep(20);
        }
    };
    writer.reset();
    in.reset();
    if (!input.empty()) {
        DWORD written = 0;
        WriteFile(inputWriter, input.data(), (DWORD)input.size(), &written, nullptr);
    }
    inputWriter.reset();
    std::string pending;
    uint64_t start = GetTickCount64(), heartbeat = start, lastFlush = start, lastMembers = 0;
    bool timedOut = false;
    auto emit = [&](std::string bytes) {
        if (bytes.empty())
            return;
        int n = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
        std::wstring s(n, 0);
        if (n)
            MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), (int)bytes.size(), s.data(), n);
        if (commandLog.valid()) {
            auto utf = Utf8(s + L"\r\n");
            DWORD written = 0;
            if (!WriteFile(commandLog, utf.data(), (DWORD)utf.size(), &written, nullptr) ||
                written != utf.size())
                throw Error(L"命令日志写入失败：" + logfile + L"\r\n" + WinError());
            if (GetTickCount64() - lastFlush >= 1000) {
                if (!FlushFileBuffers(commandLog))
                    throw Error(L"命令日志刷新失败：" + logfile + L"\r\n" + WinError());
                lastFlush = GetTickCount64();
            }
        }
        if (h.log)
            h.log(Trim(s));
    };
    try {
        for (;;) {
            CheckCancel(stop);
            uint64_t now = GetTickCount64();
            if (now - lastMembers >= 100) {
                rememberMembers();
                lastMembers = now;
            }
            if (now - start > timeout) {
                timedOut = true;
                break;
            }
            if (now - heartbeat >= 5000) {
                if (h.log)
                    h.log(BaseName(program) + L" 仍在执行 · 用时 " + std::to_wstring((now - start) / 1000) +
                          L" 秒");
                heartbeat = now;
            }
            DWORD avail = 0;
            if (PeekNamedPipe(output, nullptr, 0, nullptr, &avail, nullptr) && avail) {
                char buffer[8192];
                DWORD read = 0;
                if (ReadFile(output, buffer, std::min<DWORD>(sizeof buffer, avail), &read, nullptr) && read) {
                    pending.append(buffer, read);
                    size_t i = 0;
                    while ((i = pending.find_first_of("\r\n")) != std::string::npos) {
                        emit(pending.substr(0, i));
                        pending.erase(0, i + 1);
                    }
                    if (pending.size() > 32768) {
                        emit(pending);
                        pending.clear();
                    }
                    continue;
                }
            }
            if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
                break;
        }
        // Drain output already written before process exit.
        stopTree(); // End descendants and verify exit before handing files to the next stage.
        size_t drained = 0;
        for (; drained < 1024 * 1024;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(output, nullptr, 0, nullptr, &avail, nullptr) || !avail)
                break;
            char b[8192];
            DWORD n = 0;
            if (!ReadFile(output, b, std::min<DWORD>(avail, sizeof b), &n, nullptr) || !n)
                break;
            pending.append(b, n);
            drained += n;
        }
        emit(pending);
    } catch (...) {
        if (!stopAttempted)
            stopTree();
        try {
            emit(pending);
        } catch (...) {
        }
        if (commandLog.valid())
            FlushFileBuffers(commandLog);
        throw;
    }
    if (commandLog.valid() && !FlushFileBuffers(commandLog))
        throw Error(L"命令日志刷新失败：" + logfile + L"\r\n" + WinError());
    if (timedOut)
        throw Error(L"命令执行超时，已结束本次子进程。日志：" + logfile);
    CheckCancel(stop);
    DWORD code = 1;
    if (!GetExitCodeProcess(process, &code) || code == STILL_ACTIVE)
        throw Error(L"无法核验命令退出状态。");
    if (code)
        throw Error(BaseName(program) + L" 返回错误 " + std::to_wstring(code) + L"。请查看：" + logfile);
    return code;
}
} // namespace wu
