#pragma once

#include <string>

#include "zygisk_next_api.h"

/// Opaque module handle passed to Native ZN callbacks. Its lifetime matches
/// the loaded library in this process.
struct ZnModuleHandle {
    std::string lib_path;
    bool companion = false;
    int target_api_version = 0;
};

namespace zn {
const ZygiskNextAPI *getApiForVersion(int target_api_version);
bool isHyosSpawner();
void initHyosRuntime();
bool isHyosRuntime();
}  // namespace zn
