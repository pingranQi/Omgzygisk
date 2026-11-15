#include <sys/sysmacros.h>  // For makedev
#include <mntent.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <algorithm>  // For std::sort
#include <cerrno>     // For errno
#include <cstdio>     // For sscanf
#include <cstring>    // For strerror
#include <fstream>    // For std::ifstream
#include <sstream>    // For std::stringstream
#include <string>
#include <vector>

#include "logging.hpp"
#include "module.hpp"
#include "zygisk.hpp"

static bool starts_with(const std::string& str, const std::string& prefix) {
    return str.rfind(prefix, 0) == 0;
}

std::vector<mount_info> parse_mount_info(const char* pid) {
    std::string path = "/proc/";
    path += pid;
    path += "/mountinfo";

    std::ifstream file(path);
    if (!file.is_open()) {
        PLOGE("open %s", path.c_str());
        return {};
    }

    std::vector<mount_info> result;
    std::string line;
    while (std::getline(file, line)) {
        // The " - " separator is the only guaranteed, unambiguous delimiter on a valid line.
        size_t separator_pos = line.find(" - ");
        if (separator_pos == std::string::npos) {
            LOGE("malformed line (no ' - ' separator): %s", line.c_str());
            continue;
        }

        // Split the line into the part before the separator and the part after.
        std::string part1_str = line.substr(0, separator_pos);
        std::string part2_str = line.substr(separator_pos + 3);

        std::stringstream p1_ss(part1_str);
        mount_info info = {};
        std::string device_str;

        // 1. Parse the fixed-format fields from the first part of the line.
        p1_ss >> info.id >> info.parent >> device_str >> info.root >> info.target;
        if (p1_ss.fail()) {
            LOGE("malformed line (failed parsing first section): %s", line.c_str());
            continue;
        }

        // 2. Parse the "major:minor" string.
        // sscanf is ideal for this fixed format and returns the number of items matched.
        unsigned int maj = 0, min = 0;
        if (sscanf(device_str.c_str(), "%u:%u", &maj, &min) != 2) {
            LOGE("malformed line (invalid device format): %s", line.c_str());
            continue;
        }
        info.device = makedev(maj, min);

        // 3. The remainder of the first part is the vfs_options.
        // We use getline to consume everything left in the stream.
        std::string remaining_vfs;
        std::getline(p1_ss, remaining_vfs);
        if (!remaining_vfs.empty() && remaining_vfs.front() == ' ') {
            info.vfs_options = remaining_vfs.substr(1);  // Trim leading space
        }

        // 4. Parse the second part of the line.
        std::stringstream p2_ss(part2_str);
        p2_ss >> info.type >> info.source;
        if (p2_ss.fail()) {
            LOGE("malformed line (failed parsing type/source): %s", line.c_str());
            continue;
        }

        // 5. The remainder of the second part is the fs_options.
        std::string remaining_fs;
        std::getline(p2_ss, remaining_fs);
        if (!remaining_fs.empty() && remaining_fs.front() == ' ') {
            info.fs_options = remaining_fs.substr(1);  // Trim leading space
        }

        info.raw_info = line;
        result.push_back(std::move(info));
    }
    return result;
}

static bool path_is_at_or_under(const std::string& path, const char* prefix) {
    const size_t length = strlen(prefix);
    return path.compare(0, length, prefix) == 0 &&
           (path.size() == length || path[length] == '/');
}

static bool abort_process_mount_revert(const std::vector<mount_info>& traces,
                                       uint32_t info_flags) {
    if (traces.empty()) {
        LOGV("no root mount traces found; falling back to the clean namespace");
        return true;
    }

    const bool is_magisk = (info_flags & PROCESS_ROOT_IS_MAGISK) != 0;
    for (const auto& trace : traces) {
        if (!path_is_at_or_under(trace.target, "/product")) continue;
        if (path_is_at_or_under(trace.target, "/product/bin")) continue;
        if (!is_magisk && trace.target != "/product") continue;

        LOGW("refusing in-place mount revert because of protected target: [%s]",
             trace.raw_info.c_str());
        return true;
    }
    return false;
}

// Revert root/module mounts only in the current process's private mount tree.
// The caller must first successfully unshare(CLONE_NEWNS). Any incomplete or
// unsafe revert returns false so the caller can switch to the cached Clean
// namespace rather than leaving a partially cleaned view.
bool revert_root_traces_here(uint32_t info_flags) {
    auto mount_infos = parse_mount_info("self");
    if (mount_infos.empty()) {
        LOGW("could not read current mountinfo; falling back to the clean namespace");
        return false;
    }

    const char* mount_source_name = nullptr;
    const bool is_apatch = (info_flags & PROCESS_ROOT_IS_APATCH) != 0;
    const bool is_kernelsu = (info_flags & PROCESS_ROOT_IS_KSU) != 0;

    if (is_apatch) {
        mount_source_name = "APatch";
    } else if (is_kernelsu) {
        mount_source_name = "KSU";
    } else if (info_flags & PROCESS_ROOT_IS_MAGISK) {
        mount_source_name = "magisk";
    } else {
        LOGW("could not determine root implementation; refusing in-place mount revert");
        return false;
    }

    std::string kernel_su_module_source;
    if (is_kernelsu) {
        for (const auto& info : mount_infos) {
            if (info.target == "/data/adb/modules" &&
                starts_with(info.source, "/dev/block/loop")) {
                kernel_su_module_source = info.source;
                LOGV("detected KernelSU loop device module source: %s",
                     kernel_su_module_source.c_str());
                break;
            }
        }
    }

    std::vector<mount_info> traces;
    for (const auto& info : mount_infos) {
        const bool should_revert =
            path_is_at_or_under(info.root, "/adb/modules") ||
            path_is_at_or_under(info.target, "/data/adb/modules") ||
            info.source == mount_source_name ||
            (is_apatch && info.source == "kpatch") ||
            (!kernel_su_module_source.empty() && info.source == kernel_su_module_source);

        if (should_revert) traces.push_back(info);
    }

    if (abort_process_mount_revert(traces, info_flags)) return false;

    // Remove stacked mounts from the top down.
    std::sort(traces.begin(), traces.end(),
              [](const mount_info& a, const mount_info& b) { return a.id > b.id; });

    bool complete = true;
    for (const auto& trace : traces) {
        LOGV("detaching %s (mnt_id: %u)", trace.target.c_str(), trace.id);
        if (umount2(trace.target.c_str(), MNT_DETACH) != 0) {
            PLOGW("failed to detach %s", trace.target.c_str());
            complete = false;
        }
    }

    return complete;
}

// bionic may retain a static /proc/self/mounts line parsed before the fork.
// Read the now-clean mount table once more after module callbacks so that the
// process does not keep exposing an inherited line from the zygote.
bool refresh_mount_line() {
    FILE* mounts = setmntent("/proc/self/mounts", "r");
    if (mounts == nullptr) {
        PLOGE("open /proc/self/mounts");
        return false;
    }

    size_t lines = 0;
    while (getmntent(mounts) != nullptr) ++lines;
    endmntent(mounts);

    if (lines == 0) {
        LOGW("/proc/self/mounts returned no entries");
        return false;
    }
    return true;
}

}
