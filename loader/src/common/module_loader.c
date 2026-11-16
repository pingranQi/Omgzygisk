#include "module_loader_c.h"

#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "dl.h"
#include "../external/csoloader/include/csoloader.h"

#define LOG_TAG "zygisk-module-loader"
#ifndef USE_CUSTOM_LOADER
#define USE_CUSTOM_LOADER 1
#endif

static const char default_entry_symbol[] = "zygisk_module_entry";
static int used_custom_loader;
static int custom_loader_pinned;

static int load_system_linker(int memfd, const char *entry_symbol,
                              omg_loaded_module *out) {
    void *handle = DlopenMem(memfd, RTLD_NOW);
    void *entry;
    if (handle == NULL) return 0;

    entry = dlsym(handle, entry_symbol);
    if (entry == NULL) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "module handle %p has no '%s' symbol", handle, entry_symbol);
        dlclose(handle);
        return 0;
    }
    out->handle = handle;
    out->entry = entry;
    out->custom = 0;
    return 1;
}

#if USE_CUSTOM_LOADER
static int load_custom_linker(int memfd, const char *entry_symbol,
                              omg_loaded_module *out) {
    char path[64];
    struct csoloader *lib;
    void *entry;

    snprintf(path, sizeof(path), "/proc/self/fd/%d", memfd);
    lib = (struct csoloader *)calloc(1, sizeof(*lib));
    if (lib == NULL) return 0;

    if (!csoloader_load(lib, path)) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "custom loader failed for fd %d (%s); falling back",
                            memfd, path);
        free(lib);
        return 0;
    }

    entry = csoloader_get_symbol(lib, entry_symbol);
    if (entry == NULL) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "custom-loaded module has no '%s' symbol; falling back",
                            entry_symbol);
        csoloader_unload(lib);
        free(lib);
        return 0;
    }

    out->handle = lib;
    out->entry = entry;
    out->custom = 1;
    return 1;
}
#endif

int omg_module_load(int memfd, const char *entry_symbol, omg_loaded_module *out) {
    if (out == NULL) return 0;
    out->handle = NULL;
    out->entry = NULL;
    out->custom = 0;
    if (entry_symbol == NULL || entry_symbol[0] == '\0') {
        entry_symbol = default_entry_symbol;
    }

#if USE_CUSTOM_LOADER
    if (load_custom_linker(memfd, entry_symbol, out)) {
        used_custom_loader = 1;
        __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG,
                            "loaded module from fd %d via custom linker", memfd);
        return 1;
    }
#endif
    return load_system_linker(memfd, entry_symbol, out);
}

int omg_module_unload(void *handle, int custom) {
    if (!custom) return dlclose(handle) == 0;

    struct csoloader *lib = (struct csoloader *)handle;
    const int ok = csoloader_unload(lib);
    if (!ok) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "csoloader_unload failed for handle %p", handle);
    }
    free(lib);
    return ok;
}

void omg_module_deinit_custom_loader(void) {
    if (!used_custom_loader || custom_loader_pinned) return;
    csoloader_deinit();
    used_custom_loader = 0;
}

void omg_module_retain_custom_loader(void) {
    custom_loader_pinned = 1;
}
