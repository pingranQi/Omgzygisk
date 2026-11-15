project(external)

OPTION(LSPLT_BUILD_SHARED OFF)
add_subdirectory(lsplt/lsplt/src/main/jni)

# CSOLoader — traceless, system-linker-independent ELF loader (AGPL-3.0).
# Vendored as a git submodule; builds a static `csoloader` target from its own
# CMakeLists. Its presence makes the combined work AGPL-3.0 — see LICENSE.
add_subdirectory(csoloader)

# Dobby — inline hook backend used by Native ZN API v4 and HyperOS runtime.
# Dobby does not provide the riscv64 backend; the shipped Android ABI is
# currently arm64-v8a.
if(NOT ANDROID_ABI STREQUAL "riscv64")
    option(DOBBY_BUILD_SHARED OFF)
    option(DOBBY_GENERATE_SHARED OFF)
    add_subdirectory(dobby)
endif()
