#include "daemon.hpp"

#include <linux/un.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logging.hpp"
#include "omg_loader_core.h"
#include "socket_utils.hpp"

namespace zygiskd {
static std::string TMP_PATH;

void Init(const char *path) {
    TMP_PATH = path;
}

std::string GetTmpPath() { return TMP_PATH; }

int Connect(uint8_t retry) {
    auto socket_path = GetTmpPath() + kCPSocketName;
    const int result = omg_loader_core_connect_unix(socket_path.c_str(), retry);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

bool PingHeartbeat() {
    UniqueFd fd = Connect(5);
    if (fd == -1) {
        PLOGE("connecting to zygiskd");
        return false;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::PingHeartbeat);
    return true;
}

uint32_t GetProcessFlags(uid_t uid) {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("GetProcessFlags");
        return 0;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetProcessFlags);
    socket_utils::write_u32(fd, uid);
    return socket_utils::read_u32(fd);
}

void CacheMountNamespace(pid_t pid) {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("CacheMountNamespace");
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::CacheMountNamespace);
    socket_utils::write_u32(fd, (uint32_t) pid);
}

// Returns the file descriptor >= 0 on success, or -1 on failure.
int UpdateMountNamespace(MountNamespace type) {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("UpdateMountNamespace");
        return -1;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::UpdateMountNamespace);
    socket_utils::write_u8(fd, (uint8_t) type);

    // Read Status Byte
    uint8_t status = socket_utils::read_u8(fd);
    // Handle Failure Case (Not Cached)
    if (status == 0) {
        // Daemon explicitly told us it doesn't have it.
        return -1;
    }
    // Handle Success Case
    int namespace_fd = socket_utils::recv_fd(fd);
    if (namespace_fd < 0) {
        PLOGE("UpdateMountNamespace: failed to receive fd");
        return -1;
    }

    return namespace_fd;
}

std::vector<Module> ReadModules() {
    std::vector<Module> modules;
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("ReadModules");
        return modules;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::ReadModules);
    size_t len = socket_utils::read_usize(fd);
    for (size_t i = 0; i < len; i++) {
        std::string name = socket_utils::read_string(fd);
        int module_fd = socket_utils::recv_fd(fd);
        modules.emplace_back(name, module_fd);
    }
    return modules;
}

std::vector<FnModule> ReadFnModules() {
    std::vector<FnModule> modules;
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("ReadFnModules");
        return modules;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::ReadFnModules);
    size_t len = socket_utils::read_usize(fd);
    for (size_t i = 0; i < len; i++) {
        std::string id = socket_utils::read_string(fd);
        std::string triggers = socket_utils::read_string(fd);
        uint8_t scope = socket_utils::read_u8(fd);
        std::string apps = socket_utils::read_string(fd);
        uint32_t priority = socket_utils::read_u32(fd);
        int module_fd = socket_utils::recv_fd(fd);
        modules.emplace_back(std::move(id), std::move(triggers), scope, std::move(apps), priority,
                             module_fd);
    }
    return modules;
}

int GetFnModuleDir(const std::string &id) {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("GetFnModuleDir");
        return -1;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetFnModuleDir);
    socket_utils::write_string(fd, id);
    return socket_utils::recv_fd(fd);
}

bool CheckZnTarget(const std::string &process_name, const std::string &process_path) {
    // The monitor may start the daemon at this first Native ZN lookup.
    UniqueFd fd = Connect(5);
    if (fd == -1) {
        PLOGE("CheckZnTarget");
        return false;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::CheckZnTarget);
    socket_utils::write_string(fd, process_name);
    socket_utils::write_string(fd, process_path);
    return socket_utils::read_u8(fd) == 1;
}

std::vector<ZnPlanEntry> GetZnPlan(const std::string &process_name,
                                   const std::string &process_path,
                                   uint8_t retry) {
    std::vector<ZnPlanEntry> modules;
    UniqueFd fd = Connect(retry);
    if (fd == -1) {
        PLOGE("GetZnPlan");
        return modules;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetZnPlan);
    socket_utils::write_string(fd, process_name);
    socket_utils::write_string(fd, process_path);
    const size_t count = socket_utils::read_usize(fd);
    if (count > 128) {
        LOGE("GetZnPlan: daemon returned an invalid module count: %zu", count);
        return modules;
    }
    for (size_t i = 0; i < count; ++i) {
        std::string path = socket_utils::read_string(fd);
        const bool companion = socket_utils::read_u8(fd) != 0;
        const int module_fd = socket_utils::recv_fd(fd);
        if (module_fd < 0) {
            LOGE("GetZnPlan: failed to receive library FD for %s", path.c_str());
            continue;
        }
        modules.emplace_back(std::move(path), companion, module_fd);
    }
    return modules;
}

int ConnectZnCompanion(const std::string &library_path) {
    int fd = Connect(1);
    if (fd == -1) {
        PLOGE("ConnectZnCompanion");
        return -1;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::RequestZnCompanionSocket);
    socket_utils::write_string(fd, library_path);
    if (socket_utils::read_u8(fd) == 1) return fd;
    close(fd);
    return -1;
}

void ReportZnModule(const std::string &library_path, bool injected) {
    UniqueFd fd = Connect(1);
    if (fd == -1) return;
    socket_utils::write_u8(fd, (uint8_t) SocketAction::ReportZnModule);
    socket_utils::write_string(fd, library_path);
    socket_utils::write_u8(fd, injected ? 1 : 0);
    (void) socket_utils::read_u8(fd);
}

int ConnectCompanion(size_t index) {
    int fd = Connect(1);
    if (fd == -1) {
        PLOGE("ConnectCompanion");
        return -1;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::RequestCompanionSocket);
    socket_utils::write_usize(fd, index);
    if (socket_utils::read_u8(fd) == 1) {
        return fd;
    } else {
        close(fd);
        return -1;
    }
}

int GetModuleDir(size_t index) {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("GetModuleDir");
        return -1;
    }
    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetModuleDir);
    socket_utils::write_usize(fd, index);
    return socket_utils::recv_fd(fd);
}

void ZygoteRestart() {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        if (errno == ENOENT) {
            LOGD("could not notify ZygoteRestart (maybe it hasn't been created)");
        } else {
            PLOGE("notify ZygoteRestart");
        }
        return;
    }
    if (!socket_utils::write_u8(fd, (uint8_t) SocketAction::ZygoteRestart)) {
        PLOGE("request ZygoteRestart");
    }
}

void SystemServerStarted() {
    UniqueFd fd = Connect(1);
    if (fd == -1) {
        PLOGE("report system server started");
    } else {
        if (!socket_utils::write_u8(fd, (uint8_t) SocketAction::SystemServerStarted)) {
            PLOGE("report system server started");
        }
    }
}
}  // namespace zygiskd
