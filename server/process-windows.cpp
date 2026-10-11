// process-windows.cpp: the process layer on Windows: the command in a job
// object, so terminating the job ends every descendant

#if defined(_WIN32)

#    include "output.h"
#    include "process.h"
#    include "utf8.h"

#    include <chrono>
#    include <vector>

// the time the output of a job gets to close once it is terminated
static const DWORD DRAIN_MS = 2000;

// the milliseconds between two ticks
static const int TICK_MS = 1000;

// the bytes each pipe holds
static const DWORD PIPE_SIZE = 65536;

// the exit code a terminated job leaves
static const UINT KILLED = 1;

// the output of the command, read by a thread of its own until the pipe ends
struct reader {
    HANDLE          pipe;
    output_buffer * kept;
};

static DWORD WINAPI read_all(void * p) {
    const reader * r = (const reader *) p;
    char           buf[65536];
    DWORD          got = 0;
    while (ReadFile(r->pipe, buf, sizeof(buf), &got, NULL) && got > 0) {
        r->kept->add(buf, got);
    }
    return 0;
}

// the script on the stdin of the command, written by a thread of its own
// until it is all written or the command closes its end
struct writer {
    HANDLE              pipe;
    const std::string * input;
};

static DWORD WINAPI write_all(void * p) {
    const writer * w    = (const writer *) p;
    size_t         done = 0;
    DWORD          put  = 0;
    while (done < w->input->size()) {
        const size_t chunk = w->input->size() - done;
        if (!WriteFile(w->pipe, w->input->data() + done, (DWORD) (chunk < PIPE_SIZE ? chunk : PIPE_SIZE), &put, NULL)) {
            break;
        }
        done += put;
    }
    CloseHandle(w->pipe);
    return 0;
}

// the text of a run that is not UTF-8, read in the OEM code page of the
// machine, which some native programs write whatever the console says
static std::string oem_decode(const std::string & run) {
    const int wide = MultiByteToWideChar(GetOEMCP(), MB_ERR_INVALID_CHARS, run.data(), (int) run.size(), NULL, 0);
    if (wide <= 0) {
        return "";
    }
    std::wstring w((size_t) wide, L'\0');
    MultiByteToWideChar(GetOEMCP(), MB_ERR_INVALID_CHARS, run.data(), (int) run.size(), &w[0], wide);
    const int   size = WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, NULL, 0, NULL, NULL);
    std::string out((size_t) size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, &out[0], size, NULL, NULL);
    return out;
}

static std::string last_error() {
    wchar_t *   text = nullptr;
    const DWORD n =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, GetLastError(), 0, (LPWSTR) &text, 0, NULL);
    std::string out;
    if (n > 0) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text, (int) n, NULL, 0, NULL, NULL);
        out.resize((size_t) size);
        WideCharToMultiByte(CP_UTF8, 0, text, (int) n, &out[0], size, NULL, NULL);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == '.')) {
            out.pop_back();
        }
    }
    LocalFree(text);
    return out;
}

// a pipe whose child end alone is inheritable
static bool pipe_for_child(HANDLE & parent, HANDLE & child, bool child_reads) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE              r, w;
    if (!CreatePipe(&r, &w, &sa, PIPE_SIZE)) {
        return false;
    }
    parent = child_reads ? w : r;
    child  = child_reads ? r : w;
    SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0);
    return true;
}

process_result process_run(const std::string &           line,
                           const std::string &           cwd,
                           const std::string &           input,
                           int                           timeout_s,
                           size_t                        budget,
                           const std::function<bool()> & tick) {
    process_result    r;
    const std::string program = process_find(line);
    if (program.empty()) {
        r.error = "no such program";
        return r;
    }

    HANDLE in_parent, in_child, out_parent, out_child;
    if (!pipe_for_child(in_parent, in_child, true)) {
        r.error = last_error();
        return r;
    }
    if (!pipe_for_child(out_parent, out_child, false)) {
        r.error = last_error();
        CloseHandle(in_parent);
        CloseHandle(in_child);
        return r;
    }

    // the job ends what the command starts, once its last handle closes too
    HANDLE                               job    = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    // the two pipe ends of the command the only handles the command inherits, since tool
    // calls run side by side
    HANDLE inherited[2] = { in_child, out_child };
    SIZE_T size         = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    std::vector<char>            list(size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST) list.data();
    InitializeProcThreadAttributeList(attrs, 1, 0, &size);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), NULL, NULL);

    STARTUPINFOEXW si         = {};
    si.StartupInfo.cb         = sizeof(si);
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = in_child;
    si.StartupInfo.hStdOutput = out_child;
    si.StartupInfo.hStdError  = out_child;
    si.lpAttributeList        = attrs;

    // the command line as it is written, parsed by the program itself; a
    // console of its own, hidden, so no command touches the one of the server;
    // suspended until it belongs to the job
    const std::wstring  application = utf8_to_wide(program.c_str());
    std::wstring        command     = utf8_to_wide(line.c_str());
    const std::wstring  directory   = utf8_to_wide(cwd.c_str());
    PROCESS_INFORMATION pi          = {};
    const BOOL          spawned     = CreateProcessW(application.c_str(), &command[0], NULL, NULL, TRUE,
                                                     CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL,
                                                     directory.c_str(), &si.StartupInfo, &pi);
    if (spawned) {
        AssignProcessToJobObject(job, pi.hProcess);
    } else {
        r.error = last_error();
    }
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(in_child);
    CloseHandle(out_child);
    if (!spawned) {
        CloseHandle(in_parent);
        CloseHandle(out_parent);
        CloseHandle(job);
        return r;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    r.started = true;

    output_buffer kept(budget, oem_decode);
    reader        rd      = { out_parent, &kept };
    HANDLE        reading = CreateThread(NULL, 0, read_all, &rd, 0, NULL);
    writer        wr      = { in_parent, &input };
    HANDLE        writing = CreateThread(NULL, 0, write_all, &wr, 0, NULL);

    using clock         = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(timeout_s);
    while (WaitForSingleObject(pi.hProcess, TICK_MS) == WAIT_TIMEOUT) {
        if (clock::now() >= deadline) {
            r.timed_out = true;
            break;
        }
        if (!tick()) {
            r.cancelled = true;
            break;
        }
    }

    // the command exits or is ended: what it left running dies with it
    TerminateJobObject(job, KILLED);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (WaitForSingleObject(reading, DRAIN_MS) == WAIT_TIMEOUT) {
        CancelSynchronousIo(reading);
        WaitForSingleObject(reading, INFINITE);
    }
    CancelSynchronousIo(writing);
    WaitForSingleObject(writing, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = (int) code;
    r.output    = kept.text();
    r.total     = kept.total();
    r.truncated = kept.truncated();

    CloseHandle(reading);
    CloseHandle(writing);
    CloseHandle(out_parent);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return r;
}

#endif
