#include "zn_loader.hpp"

#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "daemon.hpp"
#include "logging.hpp"
#include "module_loader.hpp"
#include "zn_api.hpp"

namespace zn {
namespace {

std::mutex g_loaded_mutex;
std::vector<LoadedModule> g_loaded_modules;
std::vector<ZnModuleHandle *> g_module_handles;

std::string getProcessPath() {
    char path[PATH_MAX]{};
    const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count <= 0) return {};
    std::string result(path, static_cast<size_t>(count));
    constexpr std::string_view deleted = " (deleted)";
    if (result.size() >= deleted.size() &&
        result.compare(result.size() - deleted.size(), deleted.size(), deleted) == 0) {
        result.resize(result.size() - deleted.size());
    }
    return result;
}

std::string getProcessName(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool loadEntry(zygiskd::ZnPlanEntry &entry) {
    // Share the existing CSOLoader-first module-loading seam with classic
    // Zygisk/FN modules. ZN's public entry is a data object, so only the
    // resolved address differs; the mapped image and lifetime policy are the
    // same. The always-available system-linker fallback remains in the seam.
    LoadedModule loaded = LoadModuleFromMemfd(entry.memfd, "zn_module");
    if (!loaded) {
        LOGE("ZN: unable to load/resolve zn_module for %s", entry.lib_path.c_str());
        zygiskd::ReportZnModule(entry.lib_path, false);
        return false;
    }

    // Pin shared custom-linker TLS before entering module code. The ZN DSO
    // remains resident for this process lifetime.
    if (loaded.custom) RetainCustomLoaderForProcessLifetime();

    auto *module = reinterpret_cast<ZygiskNextModule *>(loaded.entry);
    if (!module || !module->onModuleLoaded || module->target_api_version < 3 ||
        module->target_api_version > ZYGISK_NEXT_API_VERSION) {
        LOGW("ZN: %s has no compatible zn_module export", entry.lib_path.c_str());
        (void) UnloadModule(loaded.handle, loaded.custom);
        zygiskd::ReportZnModule(entry.lib_path, false);
        return false;
    }

    auto *self = new ZnModuleHandle();
    self->lib_path = entry.lib_path;
    self->companion = entry.companion;
    self->target_api_version = module->target_api_version;

    LOGI("ZN: loading %s (API %d, companion=%s)", entry.lib_path.c_str(),
         module->target_api_version, entry.companion ? "yes" : "no");
    module->onModuleLoaded(self, getApiForVersion(module->target_api_version));

    {
        // Native ZN modules and their opaque handles remain resident for this
        // process lifetime. If CSOLoader produced this image, pin its shared
        // process-global TLS state so the classic Zygisk full-unload path
        // cannot tear it down while this module still has live code/hooks.
        std::lock_guard<std::mutex> lock(g_loaded_mutex);
        g_loaded_modules.push_back(loaded);
        g_module_handles.push_back(self);
    }
    zygiskd::ReportZnModule(entry.lib_path, true);
    return true;
}

}  // namespace

void loadAllModules(unsigned char connect_retry) {
    const std::string process_path = getProcessPath();
    if (process_path.empty()) {
        LOGW("ZN: cannot determine current executable path");
        return;
    }

    const std::string process_name = getProcessName(process_path);
    LOGI("ZN: requesting Native ZN plan for %s (%s)", process_name.c_str(),
         process_path.c_str());

    auto plan = zygiskd::GetZnPlan(process_name, process_path, connect_retry);
    LOGI("ZN: daemon returned %zu matching module(s)", plan.size());
    for (auto &entry : plan) {
        if (entry.memfd < 0) continue;
        (void) loadEntry(entry);
    }
}

}  // namespace zn
