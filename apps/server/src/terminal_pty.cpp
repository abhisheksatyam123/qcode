#include "terminal_pty.h"

#include <cstdio>
#include <fcntl.h>
#include <random>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace qcode {
namespace server {

namespace {

std::mutex g_terminal_mutex;
std::unordered_map<std::string, std::shared_ptr<TerminalSession>> g_terminals;

std::string generate_terminal_id() {
    static thread_local std::mt19937 eng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    uint64_t v = dist(eng);
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return std::string(buf);
}

}  // namespace

std::shared_ptr<TerminalSession> create_terminal(const std::string& workspace) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return nullptr;
    grantpt(master);
    unlockpt(master);
    char* sname = ptsname(master);
    if (!sname) { close(master); return nullptr; }

    pid_t pid = fork();
    if (pid < 0) { close(master); return nullptr; }

    if (pid == 0) {
        // Child process
        setsid();
        int slave = open(sname, O_RDWR);
        if (slave < 0) _exit(1);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > 2) close(slave);
        close(master);
        if (!workspace.empty()) chdir(workspace.c_str());
        if (!getenv("LANG")) setenv("LANG", "en_US.UTF-8", 1);
        if (!getenv("LC_ALL")) setenv("LC_ALL", "en_US.UTF-8", 1);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        const char* shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";
        execlp(shell, shell, nullptr);
        _exit(1);
    }

    // Parent: master fd stays open
    auto session = std::make_shared<TerminalSession>();
    session->id = generate_terminal_id();
    session->master_fd = master;
    session->child_pid = pid;
    session->workspace = workspace;
    session->alive = true;

    std::lock_guard<std::mutex> lock(g_terminal_mutex);
    g_terminals[session->id] = session;
    return session;
}

void destroy_terminal(const std::string& id) {
    std::shared_ptr<TerminalSession> ts;
    {
        std::lock_guard<std::mutex> lock(g_terminal_mutex);
        auto it = g_terminals.find(id);
        if (it == g_terminals.end()) return;
        ts = it->second;
        g_terminals.erase(it);
    }
    if (ts->alive) {
        ts->alive = false;
        if (ts->master_fd >= 0) close(ts->master_fd);
        if (ts->child_pid > 0) {
            kill(ts->child_pid, SIGHUP);
            waitpid(ts->child_pid, nullptr, WNOHANG);
        }
    }
}


std::shared_ptr<TerminalSession> find_terminal(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_terminal_mutex);
    auto it = g_terminals.find(id);
    if (it == g_terminals.end()) return nullptr;
    return it->second;
}

}  // namespace server
}  // namespace qcode
