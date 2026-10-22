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
            CloseHandle(pi.hProcess);
            throw Error(L"无法建立本次命令的退出管理：" + WinError(e));
        }
        if (ResumeThread(thread) == (DWORD)-1) {
            TerminateJobObject(job, 1);
            CloseHandle(pi.hProcess);
            throw Error(L"命令无法恢复执行。");
        }
    }
    Handle process(pi.hProcess);
    writer.reset();
    in.reset();
    if (!input.empty()) {
        DWORD written = 0;
        WriteFile(inputWriter, input.data(), (DWORD)input.size(), &written, nullptr);
    }
    inputWriter.reset();
    std::string raw, pending;
    uint64_t start = GetTickCount64(), heartbeat = start;
    bool timedOut = false;
    auto emit = [&](std::string bytes) {
        if (bytes.empty())
            return;
        int n = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
        std::wstring s(n, 0);
        if (n)
            MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), (int)bytes.size(), s.data(), n);
        if (h.log)
            h.log(Trim(s));
    };
    try {
        for (;;) {
            DWORD avail = 0;
            if (PeekNamedPipe(output, nullptr, 0, nullptr, &avail, nullptr) && avail) {
                char buffer[8192];
                DWORD read = 0;
                if (ReadFile(output, buffer, std::min<DWORD>(sizeof buffer, avail), &read, nullptr) && read) {
                    raw.append(buffer, read);
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
                    if (raw.size() > 16 * 1024 * 1024)
                        raw.erase(0, raw.size() - 16 * 1024 * 1024);
                    continue;
                }
            }
            if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
                break;
            if (stop) {
                Kill();
                throw Cancelled();
            }
            uint64_t now = GetTickCount64();
            if (now - start > timeout) {
                timedOut = true;
                Kill();
                break;
            }
            if (now - heartbeat >= 5000) {
                if (h.log)
                    h.log(BaseName(program) + L" 仍在执行 · 用时 " + std::to_wstring((now - start) / 1000) +
                          L" 秒");
                heartbeat = now;
            }
        }
        // Drain output already written before process exit.
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(output, nullptr, 0, nullptr, &avail, nullptr) || !avail)
                break;
            char b[8192];
            DWORD n = 0;
            if (!ReadFile(output, b, std::min<DWORD>(avail, sizeof b), &n, nullptr) || !n)
                break;
            raw.append(b, n);
            pending.append(b, n);
        }
        emit(pending);
    } catch (...) {
        Kill();
        WaitForSingleObject(process, 2000);
        if (!logfile.empty()) {
            try {
                WriteUtf8(logfile, Wide(raw));
            } catch (...) {
            }
        }
        throw;
    }
    if (!logfile.empty()) {
        int n = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), nullptr, 0);
        std::wstring s(n, 0);
        if (n)
            MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), s.data(), n);
        WriteUtf8(logfile, s);
    }
    if (timedOut)
        throw Error(L"命令执行超时，已结束本次子进程。日志：" + logfile);
    CheckCancel(stop);
    DWORD code = 1;
    if (!GetExitCodeProcess(process, &code) || code == STILL_ACTIVE)
        throw Error(L"无法核验命令退出状态。");
    {
        std::lock_guard<std::mutex> lock(mutex);
        job.reset();
    }
    if (code)
        throw Error(BaseName(program) + L" 返回错误 " + std::to_wstring(code) + L"。请查看：" + logfile);
    return code;
}
} // namespace wu
