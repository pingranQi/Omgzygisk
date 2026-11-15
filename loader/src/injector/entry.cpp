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

    // Kernel-level TracerPid cleanup: install a one-shot seccomp BPF filter
    // that triggers PTRACE_EVENT_SECCOMP, which the kernel consumes silently
    // (no tracer attached) and in doing so clears any lingering ptrace state
    // the injector left behind. On kernels >= 5.10 this is a no-op.
    perform_ptrace_message_clear();
    send_seccomp_event_if_needed();
}

/* Exec-time bootstrap used for Native ZN targets outside the Zygote tree. */
extern "C" [[gnu::visibility("default")]]
void zn_entry(void*, size_t, const char* path, bool) {
    LOGI("Native ZN process bootstrap, version %s", ZKSU_VERSION);
    zygiskd::Init(path);

    if (!zygiskd::PingHeartbeat()) {
        LOGE("zygisk daemon is not running; Native ZN loading skipped");
        return;
    }

    // The runtime is exposed only inside HyperOS's native Rust spawner.
    if (zn::isHyosSpawner()) {
        zn::initHyosRuntime();
    }
    zn::loadAllModules();
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
