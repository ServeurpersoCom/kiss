// job-posix.cpp: the job layer of Linux and macOS: posix_spawn in a
// process group of its own, so SIGKILL to the group ends every descendant

#if !defined(_WIN32)

#    include "job.h"
#    include "output.h"

#    include <fcntl.h>
#    include <poll.h>
#    include <spawn.h>
#    include <sys/wait.h>
#    include <unistd.h>

#    include <cerrno>
#    include <chrono>
#    include <csignal>
#    include <cstring>

extern char ** environ;

// the time the output of a group gets to close once SIGKILL is sent
static const int DRAIN_MS = 2000;

// the milliseconds between two ticks
static const int TICK_MS = 1000;

// a pipe no other spawn inherits, since tool calls run side by side
static bool pipe_private(int fd[2]) {
#    if defined(__APPLE__)
    if (pipe(fd) != 0) {
        return false;
    }
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);
    return true;
#    else
    return pipe2(fd, O_CLOEXEC) == 0;
#    endif
}

job_result job_run(const std::string &           line,
                   const std::string &           cwd,
                   const std::string &           input,
                   int                           timeout_s,
                   size_t                        budget,
                   const std::function<bool()> & tick) {
    job_result                     r;
    const std::vector<std::string> argv = job_argv(line);
    if (argv.empty()) {
        r.error = "no command";
        return r;
    }
    int in[2], out[2];
    if (!pipe_private(in)) {
        r.error = strerror(errno);
        return r;
    }
    if (!pipe_private(out)) {
        r.error = strerror(errno);
        close(in[0]);
        close(in[1]);
        return r;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, out[1], 2);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());

    // a group of its own, the signals the server ignores back to their default
    // and none blocked, so a pipeline of the script dies of SIGPIPE as usual
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, pipe_signal;
    sigemptyset(&none);
    sigemptyset(&pipe_signal);
    sigaddset(&pipe_signal, SIGPIPE);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &pipe_signal);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    std::vector<char *> args;
    for (const std::string & a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);

    pid_t     pid   = 0;
    const int spawn = posix_spawnp(&pid, args[0], &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(in[0]);
    close(out[1]);
    if (spawn != 0) {
        close(in[1]);
        close(out[0]);
        r.error = strerror(spawn);
        return r;
    }
    r.started = true;

    fcntl(in[1], F_SETFL, fcntl(in[1], F_GETFL) | O_NONBLOCK);
    output_buffer kept(budget);
    size_t        written = 0;
    int           in_fd   = in[1];
    if (input.empty()) {
        close(in_fd);
        in_fd = -1;
    }

    using clock          = std::chrono::steady_clock;
    const auto deadline  = clock::now() + std::chrono::seconds(timeout_s);
    auto       next_tick = clock::now() + std::chrono::milliseconds(TICK_MS);
    bool       exited    = false;
    bool       killed    = false;
    auto       drain_end = clock::time_point::max();
    int        status    = 0;

    for (;;) {
        pollfd fds[2] = {
            { out[0], POLLIN,  0 },
            { in_fd,  POLLOUT, 0 },
        };
        const int n = poll(fds, in_fd >= 0 ? 2 : 1, 100);
        if (n < 0 && errno != EINTR) {
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char          buf[65536];
            const ssize_t got = read(out[0], buf, sizeof(buf));
            if (got == 0) {
                break;
            }
            if (got > 0) {
                kept.add(buf, (size_t) got);
            }
        }
        if (in_fd >= 0 && (fds[1].revents & (POLLOUT | POLLERR | POLLHUP))) {
            const ssize_t put = write(in_fd, input.data() + written, input.size() - written);
            if (put > 0) {
                written += (size_t) put;
            }
            if (put < 0 && errno != EAGAIN) {
                written = input.size();
            }
            if (written == input.size()) {
                close(in_fd);
                in_fd = -1;
            }
        }
        // the command exits: what it left running dies with it
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) {
            exited = true;
            kill(-pid, SIGKILL);
            drain_end = clock::now() + std::chrono::milliseconds(DRAIN_MS);
        }
        const auto now = clock::now();
        if (!killed && !exited && now >= deadline) {
            r.timed_out = true;
        }
        if (!killed && !exited && now >= next_tick) {
            next_tick = now + std::chrono::milliseconds(TICK_MS);
            if (!tick()) {
                r.cancelled = true;
            }
        }
        if (!killed && (r.timed_out || r.cancelled)) {
            killed = true;
            kill(-pid, SIGKILL);
            drain_end = now + std::chrono::milliseconds(DRAIN_MS);
        }
        if (now >= drain_end) {
            break;
        }
    }

    if (in_fd >= 0) {
        close(in_fd);
    }
    close(out[0]);
    if (!exited) {
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    r.output    = kept.text();
    r.total     = kept.total();
    r.truncated = kept.truncated();
    return r;
}

#endif
