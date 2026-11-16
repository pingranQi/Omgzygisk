#include "socket_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>

#include "logging.hpp"
#include "omg_loader_core.h"

namespace socket_utils {

ssize_t xread(int fd, void* buf, size_t count) {
    const ssize_t result = omg_loader_core_read_exact(fd, static_cast<uint8_t*>(buf), count);
    if (result < 0 || static_cast<size_t>(result) != count) {
        PLOGE("read (%zd != %zu)", result, count);
    }
    return result;
}

size_t xwrite(int fd, const void* buf, size_t count) {
    const size_t result = omg_loader_core_write_exact(fd, static_cast<const uint8_t*>(buf), count);
    if (result != count) PLOGE("write (%zu != %zu)", result, count);
    return result;
}

ssize_t xrecvmsg(int sockfd, struct msghdr* msg, int flags) {
    int rec = recvmsg(sockfd, msg, flags);
    if (rec < 0) PLOGE("recvmsg");
    return rec;
}

void* recv_fds(int sockfd, char* cmsgbuf, size_t bufsz, int cnt) {
    // Create a throwaway buffer.
    // It must match the size Rust sends (sizeof(int) = 4 bytes).
    int dummy_data;

    iovec iov = {
        .iov_base = &dummy_data,
        .iov_len = sizeof(dummy_data),
    };
    msghdr msg = {.msg_name = nullptr,
                  .msg_namelen = 0,
                  .msg_iov = &iov,
                  .msg_iovlen = 1,
                  .msg_control = cmsgbuf,
                  .msg_controllen = bufsz,
                  .msg_flags = 0};

    ssize_t rec = xrecvmsg(sockfd, &msg, MSG_WAITALL);

    // --- IO Failed or Stream Desync ---
    if (rec != sizeof(dummy_data)) {
        PLOGE("recv_fds: IO failure. Read %zd bytes, expected %zu", rec, sizeof(dummy_data));
    }

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);

    // --- No headers received ---
    if (cmsg == nullptr) {
        LOGE("recv_fds: No control headers received. msg_controllen=%zu",
             (size_t) msg.msg_controllen);
        return nullptr;
    }

    // Check msg_controllen <= bufsz
    if (msg.msg_controllen != bufsz) {
        LOGW("recv_fds: Size mismatch. Buffer capacity: %zu, Received len: %zu", bufsz,
             (size_t) msg.msg_controllen);
    }

    // Check Header Length Field
    size_t expected_cmsg_len = CMSG_LEN(sizeof(int) * cnt);
    if (cmsg->cmsg_len != expected_cmsg_len) {
        LOGW("recv_fds: CMSG header len mismatch. Header says: %zu, Calculated: %zu (cnt=%d)",
             (size_t) cmsg->cmsg_len, expected_cmsg_len, cnt);
    }

    // Check Protocol details
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        LOGW("recv_fds: Protocol mismatch. Level: %d (exp %d), Type: %d (exp %d)", cmsg->cmsg_level,
             SOL_SOCKET, cmsg->cmsg_type, SCM_RIGHTS);
    }

    // Return data anyway so we can see if the FD is valid
    return CMSG_DATA(cmsg);
}

uint8_t read_u8(int fd) { return omg_loader_core_read_u8(fd); }

uint32_t read_u32(int fd) { return omg_loader_core_read_u32(fd); }

size_t read_usize(int fd) { return omg_loader_core_read_usize(fd); }

bool write_usize(int fd, size_t val) { return omg_loader_core_write_usize(fd, val); }

std::string read_string(int fd) {
    // Keep the C++ stream reader in lockstep with the Rust IPC codec. A
    // corrupt length must never trigger an unbounded allocation or len + 1
    // overflow in this long-lived process.
    constexpr size_t kMaxStringBytes = 1024 * 1024;
    const size_t len = read_usize(fd);
    if (len > kMaxStringBytes) {
        LOGE("read_string: rejecting oversized field (%zu bytes)", len);
        shutdown(fd, SHUT_RDWR);
        return {};
    }

    std::string value(len, '\\0');
    if (len != 0 && xread(fd, value.data(), len) != static_cast<ssize_t>(len)) {
        shutdown(fd, SHUT_RDWR);
        return {};
    }
    return value;
}

bool write_u8(int fd, uint8_t val) { return omg_loader_core_write_u8(fd, val); }

bool write_u32(int fd, uint32_t val) { return omg_loader_core_write_u32(fd, val); }

bool write_string(int fd, std::string_view str) {
    return write_usize(fd, str.size()) && str.size() == xwrite(fd, str.data(), str.size());
}

int recv_fd(int sockfd) {
    char cmsgbuf[CMSG_SPACE(sizeof(int))];

    void* data = recv_fds(sockfd, cmsgbuf, sizeof(cmsgbuf), 1);
    if (data == nullptr) return -1;

    int result;
    memcpy(&result, data, sizeof(int));
    return result;
}
}  // namespace socket_utils
