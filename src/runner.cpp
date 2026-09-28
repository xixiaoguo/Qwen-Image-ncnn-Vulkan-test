#include "runner.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>

static std::string shell_quote(const std::string &s) {
    bool need = s.empty();
    for (char c : s) {
        if (!(isalnum((unsigned char)c) || strchr("._-/:@=+,%~", c))) { need = true; break; }
    }
    if (!need) return s;
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string num(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::vector<std::string> build_argv(const std::string &exe, const GenOptions &opt) {
    std::vector<std::string> a;
    a.push_back(exe);
    if (!opt.prompt.empty()) { a.push_back("-p"); a.push_back(opt.prompt); }
    if (!opt.negative_prompt.empty()) { a.push_back("-n"); a.push_back(opt.negative_prompt); }

    if (opt.backend != Backend::ZImage) {
        // qwenimage-ncnn-vulkan: -w is the true CFG scale and is always sent,
        // because the negative prompt only takes effect when it is above 1.
        a.push_back("-w"); a.push_back(num(opt.cfg_scale));
        a.push_back("-o"); a.push_back(opt.output_path);
        for (const auto &in : opt.inputs) { a.push_back("-i"); a.push_back(in); }
        a.push_back("-s");
        a.push_back(std::to_string(opt.width) + "," + std::to_string(opt.height));
        a.push_back("-l"); a.push_back(std::to_string(opt.steps));
        a.push_back("-r"); a.push_back(std::to_string(opt.seed));
        a.push_back("-m"); a.push_back(opt.model_path);
        // -g: omit when auto
        if (opt.gpu_id != INT_MAX) { a.push_back("-g"); a.push_back(std::to_string(opt.gpu_id)); }
        a.push_back("-b"); a.push_back(std::to_string(opt.batch));
        return a;
    }

    // zimage-ncnn-vulkan.
    a.push_back("-o"); a.push_back(opt.output_path);
    for (const auto &in : opt.inputs) { a.push_back("-i"); a.push_back(in); }
    if (!opt.mask_path.empty())    { a.push_back("-k"); a.push_back(opt.mask_path); }
    if (!opt.outpaint.empty())     { a.push_back("-x"); a.push_back(opt.outpaint); }
    if (!opt.control_path.empty()) {
        a.push_back("-c"); a.push_back(opt.control_path);
        // -w is the ControlNet scale here and only means anything next to a
        // control image, so it rides along with -c instead of always being sent.
        a.push_back("-w"); a.push_back(num(opt.control_scale));
    }
    if (opt.tile_upscale) a.push_back("-t");
    a.push_back("-s");
    a.push_back(std::to_string(opt.width) + "," + std::to_string(opt.height));
    // steps == 0 means "auto": leave -l out and let the model pick.
    if (opt.steps > 0) { a.push_back("-l"); a.push_back(std::to_string(opt.steps)); }
    a.push_back("-r"); a.push_back(std::to_string(opt.seed));
    a.push_back("-m"); a.push_back(opt.model_path);
    if (opt.gpu_id != INT_MAX) { a.push_back("-g"); a.push_back(std::to_string(opt.gpu_id)); }
    a.push_back("-b"); a.push_back(std::to_string(opt.batch));
    return a;
}

std::string build_command_string(const std::string &exe, const GenOptions &opt) {
    std::ostringstream os;
    auto a = build_argv(exe, opt);
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) os << ' ';
        os << shell_quote(a[i]);
    }
    return os.str();
}

RunResult run_process(const std::vector<std::string> &argv,
                      const std::string &workdir,
                      void (*on_line)(const std::string &line, void *user),
                      void *user) {
    RunResult r;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        r.error = "pipe() failed: " + std::string(strerror(errno));
        r.launch_failed = true;
        return r;
    }

    pid_t pid = fork();
    if (pid < 0) {
        r.error = "fork() failed: " + std::string(strerror(errno));
        r.launch_failed = true;
        close(pipefd[0]); close(pipefd[1]);
        return r;
    }

    if (pid == 0) {
        // child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (!workdir.empty()) {
            if (chdir(workdir.c_str()) != 0) _exit(127);
        }
        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &s : argv) cargv.push_back(const_cast<char *>(s.c_str()));
        cargv.push_back(nullptr);
        execv(cargv[0], cargv.data());
        // exec failed
        fprintf(stderr, "cannot execute %s: %s\n", cargv[0], strerror(errno));
        _exit(127);
    }

    // parent
    close(pipefd[1]);

    // Read without blocking. The generator may leave a helper process holding
    // the inherited stdout open, in which case the pipe never reports EOF. If
    // we waited for EOF we would stay stuck forever even though the generator
    // itself is long gone, and the UI would never learn the run had finished.
    const int fdflags = fcntl(pipefd[0], F_GETFL, 0);
    if (fdflags != -1) fcntl(pipefd[0], F_SETFL, fdflags | O_NONBLOCK);

    int status = 0;
    bool reaped = false;
    int grace_ticks = 0;      // 50 ms ticks elapsed since the child went away
    std::string pending;
    char buf[4096];

    for (;;) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));

        if (n > 0) {
            grace_ticks = 0;   // output is still flowing, keep going
            pending.append(buf, (size_t)n);
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (on_line) on_line(line, user);
            }
            if (pending.size() > 8192) {
                if (on_line) on_line(pending, user);
                pending.clear();
            }
            continue;
        }

        if (n == 0) break;     // all writers closed: the normal end of stream

        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            break;             // genuine read error

        if (!reaped) {
            const pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid || (w < 0 && errno != EINTR)) reaped = true;
        }

        if (reaped) {
            // The generator is gone. Give the tail of its output a short moment
            // to arrive, then stop regardless of who else still holds the pipe.
            if (++grace_ticks >= 10) {      // ~500 ms
                if (!pending.empty() && on_line) on_line(pending, user);
                pending.clear();
                break;
            }
        }

        usleep(50000);
    }

    close(pipefd[0]);

    if (!reaped)
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    r.finished = true;
    if (WIFEXITED(status)) {
        r.exit_code = WEXITSTATUS(status);
        if (r.exit_code == 127)
            r.error = "failed to execute (exit 127): binary not found or not executable";
    } else if (WIFSIGNALED(status)) {
        r.exit_code = 128 + WTERMSIG(status);
        r.error = "killed by signal " + std::to_string(WTERMSIG(status));
    }
    return r;
}
