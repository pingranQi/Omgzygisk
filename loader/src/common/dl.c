#include "dl.h"

#include <android/dlext.h>
#include <android/log.h>

#ifndef LOG_TAG
#if defined(__LP64__)
#define LOG_TAG "zygisk-core64"
#else
#define LOG_TAG "zygisk-core32"
#endif
#endif

/*
 * Open a shared library from a memory-backed file descriptor without
 * materialising it as a regular filesystem path.
 */
void *DlopenMem(int fd, int flags) {
    static const char jit_cache_name[] = "/jit-cache-zygisk";
    android_dlextinfo info = {
        .flags = ANDROID_DLEXT_USE_LIBRARY_FD,
        .reserved_addr = NULL,
        .reserved_size = 0,
        .relro_fd = 0,
        .library_fd = fd,
        .library_fd_offset = 0,
        .library_namespace = NULL,
    };

    void *handle = android_dlopen_ext(jit_cache_name, flags, &info);
    if (handle != NULL) {
        __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG,
                            "dlopen_ext from fd %d succeeded: handle %p",
                            fd, handle);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "dlopen_ext from fd %d failed: %s",
                            fd, dlerror());
    }

    return handle;
}
