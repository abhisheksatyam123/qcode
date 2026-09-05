#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <unordered_map>

namespace qcode {
namespace server {

struct TerminalSession {
    std::string id;
    int master_fd = -1;
    pid_t child_pid = -1;
    std::string workspace;
    bool alive = true;
};

std::shared_ptr<TerminalSession> create_terminal(const std::string& workspace);
void destroy_terminal(const std::string& id);
std::shared_ptr<TerminalSession> find_terminal(const std::string& id);
bool resize_terminal(const std::string& id, int cols, int rows);

}  // namespace server
}  // namespace qcode
