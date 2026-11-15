#pragma once

/*
 * Module loading seam.
 *
 * Zygisk, FN and Native ZN libraries are brought into the target process
 * through this one entry point. Today it wraps the system dynamic linker
 * (android_dlopen_ext, via DlopenMem). It exists so that an in-process custom
 * ELF loader — one that maps the module without registering it in the linker's
 * solist — can be dropped in behind a single interface, with the system linker
 * kept as an always-available fallback.
 *
 * See docs/CUSTOM_LINKER.md for the staged plan and the licensing constraint
 * (the custom loader is written from the public ELF/AArch64 ABI and Android's
 * Apache-2.0 bionic linker — it is NOT derived from any AGPL custom-linker
 * source).
 */

#include <dlfcn.h>

struct LoadedModule {
    /// Opaque handle for symbol lookups and, eventually, unloading.
    /// For the system-linker path this is the dlopen() handle.
    void *handle = nullptr;
    /// Resolved address of the caller-selected exported symbol, or null if
    /// the library failed to load or does not export that symbol.
    void *entry = nullptr;
    /// True when the in-process CSOLoader (not the system linker) produced
    /// this handle.
    bool custom = false;

    /// A module is usable only if its entry point resolved.
    explicit operator bool() const { return entry != nullptr; }
};

/**
 * @brief Load a module library from a memfd and resolve an exported symbol.
 *
 * The custom CSOLoader path is attempted first, with android_dlopen_ext as a
 * fallback. Existing Zygisk/FN callers use the default `zygisk_module_entry`
 * symbol; Native ZN callers request `zn_module`.
 *
 * @param memfd A file descriptor holding the shared library image.
 * @param entry_symbol Exported symbol to resolve.
 * @return The handle + resolved symbol; falsy if loading or lookup fails.
 */
LoadedModule LoadModuleFromMemfd(int memfd,
                                 const char *entry_symbol = "zygisk_module_entry");

/**
 * @brief Unload a module handle previously returned by LoadModuleFromMemfd.
 *
 * Dispatches to dlclose() for a system-linker handle, or to the custom
 * loader's own teardown (which also frees its bookkeeping struct) for one
 * loaded in-process. `handle` must not be used again after this call either
 * way.
 *
 * @param handle LoadedModule::handle from the original load.
 * @param custom LoadedModule::custom from the same load.
 * @return true on success.
 */
bool UnloadModule(void *handle, bool custom);

/**
 * @brief Tear down the custom loader's process-wide global state.
 *
 * Safe to call unconditionally — a no-op if the custom loader was never used
 * in this process. Must only be called once every module this process loaded
 * through it has also been unloaded via UnloadModule(): the teardown releases
 * TLS bookkeeping that a still-resident custom-loaded module (one that never
 * asked to unload — the common case) would depend on, so calling it early
 * corrupts that module's state instead of merely leaking memory.
 */
void DeinitCustomLoaderIfUsed();

/**
 * @brief Pin CSOLoader's process-wide state until process exit.
 *
 * Native ZN modules intentionally remain resident for the process lifetime.
 * If any such module was loaded by CSOLoader, a later Zygisk module cleanup
 * must not tear down the shared TLS/runtime state while the ZN module remains
 * mapped and may still use it.
 */
void RetainCustomLoaderForProcessLifetime();
