#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct omg_loaded_module {
    void *handle;
    void *entry;
    int custom;
} omg_loaded_module;

int omg_module_load(int memfd, const char *entry_symbol, omg_loaded_module *out);
int omg_module_unload(void *handle, int custom);
void omg_module_deinit_custom_loader(void);
void omg_module_retain_custom_loader(void);

#ifdef __cplusplus
}
#endif
