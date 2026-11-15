#include "zn_api.hpp"

#include <dlfcn.h>
#include <dobby.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include "daemon.hpp"
#include "elf_parser.hpp"
#include "logging.hpp"
#include "lsplt.hpp"

struct ZnSymbolResolver {
    std::unique_ptr<ElfParser::ElfImage> image;
};

namespace zn {
namespace {

constexpr int kMaxHyosModules = 16;
std::mutex g_hyos_mutex;
ZygiskNextHyosModule g_hyos_modules[kMaxHyosModules]{};
int g_hyos_module_count = 0;
bool g_hyos_runtime = false;
pid_t g_spawner_pid = 0;
std::atomic<bool> g_hyos_fired{false};
std::atomic<bool> g_hyos_hooks_installing{false};
std::atomic<bool> g_hyos_atfork_registered{false};
std::atomic<bool> g_hyos_warned{false};
std::atomic<bool> g_process_name_captured{false};
char g_process_name[256]{};

using HyosSetcontextFn = int (*)(uid_t, int, const char *, const char *);
using HyosSetnameFn = int (*)(pthread_t, const char *);
HyosSetcontextFn g_orig_setcontext = nullptr;
HyosSetnameFn g_orig_setname = nullptr;

bool hyosInChild() {
    return g_spawner_pid != 0 &&
           static_cast<pid_t>(syscall(__NR_getpid)) != g_spawner_pid;
}

bool readProcCmdline(char *out, size_t capacity) {
    if (!out || capacity < 2) return false;
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t count = read(fd, out, capacity - 1);
    close(fd);
    if (count <= 0) return false;
    const size_t length = strnlen(out, static_cast<size_t>(count));
    out[length] = '\0';
    return length != 0;
}

void hyosDeliverAppSpecialized(const char *package_name, const char *se_info) {
    if (!hyosInChild() || g_hyos_module_count == 0 ||
        g_hyos_fired.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    char process_name[256]{};
    char package[256]{};
    char se_info_copy[128]{};

    if (g_process_name_captured.load(std::memory_order_acquire) &&
        g_process_name[0] != '\0') {
        strlcpy(process_name, g_process_name, sizeof(process_name));
    } else if (!readProcCmdline(process_name, sizeof(process_name)) ||
               process_name[0] == '\0') {
        if (prctl(PR_GET_NAME, process_name, 0, 0, 0) != 0 ||
            process_name[0] == '\0') {
            strlcpy(process_name, "hyos_app", sizeof(process_name));
        }
    }

    strlcpy(package, package_name ? package_name : "", sizeof(package));
    strlcpy(se_info_copy, se_info ? se_info : "", sizeof(se_info_copy));

    const ZnHyosAppSpecializeArgs args{process_name, package, se_info_copy};
    LOGI("HYOS app specialized: process=%s package=%s", process_name, package);

    // Registrations happen in the spawner before it forks; each child receives
    // a private copy of the completed callback table.
    for (int i = 0; i < g_hyos_module_count; ++i) {
        if (g_hyos_modules[i].onAppSpecialized) {
            g_hyos_modules[i].onAppSpecialized(&args);
        }
    }
}

int hyosSetcontextHook(uid_t uid, int is_system_server, const char *se_info,
                       const char *package_name) {
    if (!g_orig_setcontext) return -1;
    const int result = g_orig_setcontext(uid, is_system_server, se_info, package_name);
    if (result == 0) {
        // hyosDeliverAppSpecialized filters the spawner parent using raw getpid.
    } else if (result != 0 && hyosInChild() &&
               !g_hyos_fired.load(std::memory_order_relaxed)) {
        LOGW("HYOS: selinux_android_setcontext failed (%d)", result);
    }
    return result;
}

int hyosSetnameHook(pthread_t thread, const char *name) {
    if (!g_orig_setname) return -1;
    const int result = g_orig_setname(thread, name);
    if (result == 0 && hyosInChild() && name && name[0] != '\0' &&
        pthread_equal(thread, pthread_self()) &&
        !g_process_name_captured.load(std::memory_order_relaxed)) {
        strlcpy(g_process_name, name, sizeof(g_process_name));
        g_process_name_captured.store(true, std::memory_order_release);
    }
    return result;
}

bool hookSpawnerPlt(const char *symbol, void *replacement, void **original) {
    const auto maps = lsplt::MapInfo::Scan();
    const lsplt::MapInfo *spawner = nullptr;
    for (const auto &map : maps) {
        if (map.inode == 0 || map.offset != 0) continue;
        const std::string_view path(map.path);
        if (path == "/system_ext/bin/hyos_spawner" ||
            path.ends_with("/hyos_spawner") ||
            path.ends_with("/hyos_spawner (deleted)")) {
            spawner = &map;
            break;
        }
    }
    if (!spawner) return false;
    if (!lsplt::RegisterHook(spawner->dev, spawner->inode, symbol, replacement, original)) {
        return false;
    }
    return lsplt::CommitHook();
}

void hyosInstallHooks() {
    bool expected = false;
    if (!g_hyos_hooks_installing.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }

    if (!g_orig_setcontext &&
        !hookSpawnerPlt("selinux_android_setcontext",
                        reinterpret_cast<void *>(hyosSetcontextHook),
                        reinterpret_cast<void **>(&g_orig_setcontext))) {
        void *function = dlsym(RTLD_DEFAULT, "selinux_android_setcontext");
        if (!function) {
            void *handle = dlopen("libselinux.so", RTLD_NOW);
            if (handle) function = dlsym(handle, "selinux_android_setcontext");
        }
        if (function &&
            DobbyHook(function, reinterpret_cast<dobby_dummy_func_t>(hyosSetcontextHook),
                      reinterpret_cast<dobby_dummy_func_t *>(&g_orig_setcontext)) == RT_SUCCESS) {
            LOGI("HYOS: inline hooked selinux_android_setcontext");
        }
    }

    if (!g_orig_setname &&
        !hookSpawnerPlt("pthread_setname_np", reinterpret_cast<void *>(hyosSetnameHook),
                        reinterpret_cast<void **>(&g_orig_setname))) {
        void *function = dlsym(RTLD_DEFAULT, "pthread_setname_np");
        if (!function) {
            void *handle = dlopen("libc.so", RTLD_NOW);
            if (handle) function = dlsym(handle, "pthread_setname_np");
        }
        if (function &&
            DobbyHook(function, reinterpret_cast<dobby_dummy_func_t>(hyosSetnameHook),
                      reinterpret_cast<dobby_dummy_func_t *>(&g_orig_setname)) == RT_SUCCESS) {
            LOGI("HYOS: inline hooked pthread_setname_np");
        }
    }

    if (!g_orig_setcontext && !g_hyos_warned.exchange(true)) {
        LOGW("HYOS: selinux_android_setcontext hook unavailable; specialize callbacks will not fire");
    }
    g_hyos_hooks_installing.store(false, std::memory_order_release);
}

void hyosAtForkPrepare() {
    if (g_hyos_module_count > 0 && (!g_orig_setcontext || !g_orig_setname)) {
        hyosInstallHooks();
    }
}

int api_pltHook(void *base, const char *symbol, void *hook, void **original) {
    if (!base || !symbol || !hook) return ZN_FAILED;
    const uintptr_t address = reinterpret_cast<uintptr_t>(base);
    const auto maps = lsplt::MapInfo::Scan();
    const lsplt::MapInfo *target = nullptr;
    for (const auto &map : maps) {
        if (address >= map.start && address < map.end) {
            target = &map;
            break;
        }
    }
    if (!target || target->inode == 0) {
        LOGW("ZN pltHook: base %p is not backed by a file mapping", base);
        return ZN_FAILED;
    }
    if (!lsplt::RegisterHook(target->dev, target->inode, symbol, hook, original)) {
        return ZN_FAILED;
    }
    return lsplt::CommitHook() ? ZN_SUCCESS : ZN_FAILED;
}

int api_inlineHook(void *target, void *replacement, void **original) {
    if (!target || !replacement) return ZN_FAILED;
    return DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(replacement),
                     reinterpret_cast<dobby_dummy_func_t *>(original)) == RT_SUCCESS
               ? ZN_SUCCESS
               : ZN_FAILED;
}

int api_inlineUnhook(void *target) {
    if (!target) return ZN_FAILED;
    return DobbyDestroy(target) == RT_SUCCESS ? ZN_SUCCESS : ZN_FAILED;
}

ZnSymbolResolver *api_newSymbolResolver(const char *path, void *base) {
    if (!path || !path[0]) return nullptr;
    auto *resolver = new ZnSymbolResolver();
    resolver->image = std::make_unique<ElfParser::ElfImage>(path, base);
    if (!resolver->image->isValid()) {
        LOGW("ZN symbol resolver could not open %s", path);
        delete resolver;
        return nullptr;
    }
    return resolver;
}

void api_freeSymbolResolver(ZnSymbolResolver *resolver) {
    delete resolver;
}

void *api_getBaseAddress(ZnSymbolResolver *resolver) {
    if (!resolver || !resolver->image) return nullptr;
    return resolver->image->getBaseAddress();
}

void *api_symbolLookup(ZnSymbolResolver *resolver, const char *name, bool prefix, size_t *size) {
    if (!resolver || !resolver->image || !name) return nullptr;
    if (!prefix) {
        const auto address = resolver->image->findSymbolAddress(name, size);
        return address ? reinterpret_cast<void *>(address) : nullptr;
    }

    const std::string_view symbol = resolver->image->findSymbolNameByPrefix(name);
    if (symbol.empty()) return nullptr;
    const auto address = resolver->image->findSymbolAddress(symbol, size);
    return address ? reinterpret_cast<void *>(address) : nullptr;
}

void api_forEachSymbols(
    ZnSymbolResolver *resolver,
    bool (*callback)(const char *name, void *address, size_t size, void *data),
    void *data) {
    if (!resolver || !resolver->image || !callback) return;
    resolver->image->forEachSymbol(callback, data);
}

int api_connectCompanion(void *opaque_handle) {
    auto *handle = static_cast<ZnModuleHandle *>(opaque_handle);
    if (!handle || !handle->companion || handle->target_api_version < 3 ||
        handle->lib_path.empty()) {
        return -1;
    }
    return zygiskd::ConnectZnCompanion(handle->lib_path);
}

ZnSymbolResolver *api_symbolResolverUnavailable(const char *, void *) {
    LOGE("ZN symbol resolver requires module API version >= 2");
    return nullptr;
}
void api_freeSymbolResolverUnavailable(ZnSymbolResolver *) {}
void *api_getBaseAddressUnavailable(ZnSymbolResolver *) { return nullptr; }
void *api_symbolLookupUnavailable(ZnSymbolResolver *, const char *, bool, size_t *) {
    return nullptr;
}
void api_forEachSymbolsUnavailable(
    ZnSymbolResolver *, bool (*)(const char *, void *, size_t, void *), void *) {}

int api_hyosRegisterModule(const void *opaque_module) {
    if (!opaque_module || !g_hyos_runtime) return ZN_FAILED;
    const auto *module = static_cast<const ZygiskNextHyosModule *>(opaque_module);
    if (module->target_api_version < 1 ||
        module->target_api_version > ZYGISK_NEXT_HYOS_API_VERSION ||
        !module->onAppSpecialized) {
        LOGE("HYOS: invalid runtime module API version or callback");
        return ZN_FAILED;
    }

    {
        std::lock_guard<std::mutex> lock(g_hyos_mutex);
        if (g_hyos_module_count >= kMaxHyosModules) {
            LOGE("HYOS: maximum runtime module count (%d) reached", kMaxHyosModules);
            return ZN_FAILED;
        }
        g_hyos_modules[g_hyos_module_count++] = *module;
    }

    if (!hyosInChild()) {
        bool expected = false;
        if (g_hyos_atfork_registered.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            const int result = pthread_atfork(hyosAtForkPrepare, nullptr, nullptr);
            if (result != 0) {
                g_hyos_atfork_registered.store(false, std::memory_order_release);
                LOGW("HYOS: pthread_atfork registration failed (%d)", result);
            }
        }
        hyosInstallHooks();
    }
    LOGI("HYOS: registered app-specialize callback");
    return ZN_SUCCESS;
}

const ZygiskNextRuntime kHyosRuntime{
    ZN_RUNTIME_HYOS,
    ZYGISK_NEXT_HYOS_API_VERSION,
    api_hyosRegisterModule,
};

const ZygiskNextRuntime *api_getRuntime() {
    return g_hyos_runtime ? &kHyosRuntime : nullptr;
}
const ZygiskNextRuntime *api_getRuntimeUnavailable() {
    LOGW("ZN runtime API requires module API version >= 4");
    return nullptr;
}

const ZygiskNextAPI kApiV4{
    api_pltHook, api_inlineHook, api_inlineUnhook,
    api_newSymbolResolver, api_freeSymbolResolver, api_getBaseAddress,
    api_symbolLookup, api_forEachSymbols, api_connectCompanion, api_getRuntime,
};
const ZygiskNextAPI kApiFull{
    api_pltHook, api_inlineHook, api_inlineUnhook,
    api_newSymbolResolver, api_freeSymbolResolver, api_getBaseAddress,
    api_symbolLookup, api_forEachSymbols, api_connectCompanion, api_getRuntimeUnavailable,
};
const ZygiskNextAPI kApiNoSymbolResolver{
    api_pltHook, api_inlineHook, api_inlineUnhook,
    api_symbolResolverUnavailable, api_freeSymbolResolverUnavailable,
    api_getBaseAddressUnavailable, api_symbolLookupUnavailable,
    api_forEachSymbolsUnavailable, api_connectCompanion, api_getRuntimeUnavailable,
};

}  // namespace

const ZygiskNextAPI *getApiForVersion(int target_api_version) {
    if (target_api_version >= 4) return &kApiV4;
    if (target_api_version >= 2) return &kApiFull;
    return &kApiNoSymbolResolver;
}

bool isHyosSpawner() {
#if !defined(__LP64__)
    return false;
#else
    char path[PATH_MAX]{};
    const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count <= 0) return false;
    path[count] = '\0';
    std::string_view executable(path, static_cast<size_t>(count));
    constexpr std::string_view deleted = " (deleted)";
    if (executable.ends_with(deleted)) executable.remove_suffix(deleted.size());
    return executable == "/system_ext/bin/hyos_spawner" ||
           executable.ends_with("/hyos_spawner");
#endif
}

void initHyosRuntime() {
    if (!isHyosSpawner()) return;
    g_hyos_runtime = true;
    g_spawner_pid = static_cast<pid_t>(syscall(__NR_getpid));
    LOGI("ZN: HyperOS runtime initialized in hyos_spawner pid %d", g_spawner_pid);
}

bool isHyosRuntime() {
    return g_hyos_runtime;
}

}  // namespace zn
