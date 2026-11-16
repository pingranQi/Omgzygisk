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
    if (cmsgbuf == nullptr || cnt <= 0 ||
        static_cast<size_t>(cnt) > (bufsz / sizeof(int))) {
        return nullptr;
    }

    int payload = 0;
    iovec iov = {
        .iov_base = &payload,
        .iov_len = sizeof(payload),
    };
    msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = bufsz,
        .msg_flags = 0,
    };

    ssize_t received;
    do {
        received = recvmsg(sockfd, &msg, MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    const size_t expected_len = CMSG_LEN(sizeof(int) * static_cast<size_t>(cnt));
    const bool valid = received == static_cast<ssize_t>(sizeof(payload)) &&
                       (msg.msg_flags & MSG_CTRUNC) == 0 &&
                       cmsg != nullptr &&
                       cmsg->cmsg_level == SOL_SOCKET &&
                       cmsg->cmsg_type == SCM_RIGHTS &&
                       cmsg->cmsg_len == expected_len &&
                       CMSG_NXTHDR(&msg, cmsg) == nullptr;

    if (valid) return CMSG_DATA(cmsg);

    // Any SCM_RIGHTS descriptors received on a rejected message are already
    // installed in this process. Close them rather than leaking them.
    for (cmsghdr* item = CMSG_FIRSTHDR(&msg); item != nullptr;
         item = CMSG_NXTHDR(&msg, item)) {
        if (item->cmsg_level != SOL_SOCKET || item->cmsg_type != SCM_RIGHTS ||
            item->cmsg_len < CMSG_LEN(0)) {
            continue;
        }
        const size_t bytes = item->cmsg_len - CMSG_LEN(0);
        const size_t count = bytes / sizeof(int);
        const int* descriptors = reinterpret_cast<const int*>(CMSG_DATA(item));
        for (size_t i = 0; i < count; ++i) close(descriptors[i]);
    }

    if (received < 0) PLOGE("recv_fds: recvmsg");
    else LOGE("recv_fds: rejected malformed descriptor message");
    return nullptr;
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

    std::string value(len, '\0');
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
    alignas(cmsghdr) char cmsgbuf[CMSG_SPACE(sizeof(int))]{};

    void* data = recv_fds(sockfd, cmsgbuf, sizeof(cmsgbuf), 1);
    if (data == nullptr) return -1;

    int result;
    memcpy(&result, data, sizeof(int));
    return result;
}
}  // namespace socket_utils
