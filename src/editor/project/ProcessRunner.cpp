#include "ProcessRunner.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <cstdlib>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

void streamLines(FILE* pipe, const std::function<void(const std::string&)>& onLine)
{
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && onLine) onLine(line);
    }
}

#ifdef _WIN32
// _popen has no argv form, so the arguments are re-quoted here. Embedded quotes
// are escaped rather than dropped so they cannot terminate the argument early.
std::string quoteArg(const std::string& arg)
{
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}
#endif

} // namespace

int runProcessCapture(const std::vector<std::string>& argv,
                      const std::function<void(const std::string&)>& onLine)
{
    if (argv.empty()) return -1;

#ifdef _WIN32
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        cmd += quoteArg(argv[i]);
    }
    cmd += " 2>&1";

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    streamLines(pipe, onLine);
    return _pclose(pipe);
#else
    int fds[2];
    if (pipe(fds) != 0) return -1;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned != 0) {
        close(fds[0]);
        return -1;
    }

    FILE* out = fdopen(fds[0], "r");
    if (!out) {
        close(fds[0]);
        int discard = 0;
        waitpid(pid, &discard, 0);
        return -1;
    }
    streamLines(out, onLine);
    std::fclose(out);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}
