#include <dlfcn.h>

#include "daemon.hpp"
#include "logging.hpp"
#include "ptrace_clear.hpp"
#include "zygisk.hpp"
#include "zn_api.hpp"
#include "zn_loader.hpp"

using namespace std;

extern "C" [[gnu::visibility("default")]]
void entry(void* addr, size_t size, const char* path, bool custom_loaded) {
    LOGI("zygisk library injected, version %s", ZKSU_VERSION);

    zygiskd::Init(path);

    if (!zygiskd::PingHeartbeat()) {
        LOGE("zygisk daemon is not running");
        return;
    }

    hook_entry(addr, size, custom_loaded);

    // Native ZN manifests may also target the Zygote executable itself.
    zn::loadAllModules();

    // Run the single C implementation of the seccomp-based TracerPid cleanup.
    // It skips kernels where the Seccomp_filters field makes this method visible.
    perform_ptrace_message_clear();
}

/* Exec-time bootstrap used for Native ZN targets outside the Zygote tree. */
extern "C" [[gnu::visibility("default")]]
void zn_entry(void*, size_t, const char* path, bool) {
    LOGI("Native ZN process bootstrap, version %s", ZKSU_VERSION);
    zygiskd::Init(path);

    // The exec monitor ensures zygiskd is available before handing this
    // process to the injector. Do not send PingHeartbeat here: that command
    // intentionally updates Zygote-injected state, not Native ZN status.


    // The runtime is exposed only inside HyperOS's native Rust spawner.
    if (zn::isHyosSpawner()) {
        zn::initHyosRuntime();
    }
    zn::loadAllModules();

    // Keep Native ZN exec-time injection on the same tracer-cleanup path as
    // the classic Zygisk bootstrap.
    perform_ptrace_message_clear();
}

/**
 * @brief Intercepts calls to __cxa_atexit to prevent registration of exit handlers.
 *
 * This function serves as a local replacement for the __cxa_atexit provided by libc.
 */
extern "C" int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(func), &info)) {
        const char* library_path = info.dli_fname ? info.dli_fname : "<unknown library>";
        const char* symbol_name = info.dli_sname ? info.dli_sname : "<unknown symbol>";
        LOGV("atexit registration BLOCKED [func, lib, sym, obj, dso]: [%p, %s, %s, %p, %p]",
             func, library_path, symbol_name, arg, dso);
    } else {
        LOGV("atexit registration BLOCKED for function at %p", func);
    }
    return 0;
}
