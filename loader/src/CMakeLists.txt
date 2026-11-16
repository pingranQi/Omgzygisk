cmake_minimum_required(VERSION 3.22.1)
project("loader" C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

# Build the platform-independent Rust loader core for the Android target.
# cargo-ndk is already part of the repository's Android Rust toolchain.
find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(RUSTCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore)
set(RUSTCORE_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/rustcore-target)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RUSTCORE_PROFILE debug)
    set(RUSTCORE_PROFILE_ARGS)
else()
    set(RUSTCORE_PROFILE release)
    set(RUSTCORE_PROFILE_ARGS --release)
endif()
set(RUSTCORE_ARCHIVE
    ${RUSTCORE_TARGET_DIR}/aarch64-linux-android/${RUSTCORE_PROFILE}/libomgzygisk_loader_core.a)

add_custom_command(
    OUTPUT ${RUSTCORE_ARCHIVE}
    COMMAND ${CARGO_EXECUTABLE} ndk --platform 26 -t arm64-v8a build
            --locked --target-dir ${RUSTCORE_TARGET_DIR} ${RUSTCORE_PROFILE_ARGS}
    WORKING_DIRECTORY ${RUSTCORE_DIR}
    DEPENDS
        ${RUSTCORE_DIR}/Cargo.toml
        ${RUSTCORE_DIR}/Cargo.lock
        ${RUSTCORE_DIR}/src/lib.rs
        ${RUSTCORE_DIR}/src/ffi.rs
        ${RUSTCORE_DIR}/src/control.rs
        ${RUSTCORE_DIR}/src/ipc.rs
        ${RUSTCORE_DIR}/src/fd.rs
        ${RUSTCORE_DIR}/src/monitor.rs
    COMMENT "Building OmgZygisk Rust loader core for Android ARM64"
    VERBATIM)
add_custom_target(omg_rust_core_build DEPENDS ${RUSTCORE_ARCHIVE})
add_library(omg_rust_core STATIC IMPORTED GLOBAL)
set_target_properties(omg_rust_core PROPERTIES IMPORTED_LOCATION ${RUSTCORE_ARCHIVE})
add_dependencies(omg_rust_core omg_rust_core_build)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DZKSU_VERSION=\"${ZKSU_VERSION}\")

# The work directory comes in as a plain CMake variable from the Gradle build
# (-DWORK_DIRECTORY=...); the escaped quotes produce a C string literal.
# Quoting inside the Gradle cFlags is fragile across host OSes.
add_definitions(-DWORK_DIRECTORY=\"${WORK_DIRECTORY}\")

aux_source_directory(common COMMON_SRC_LIST)
add_library(common STATIC ${COMMON_SRC_LIST})
target_include_directories(common PRIVATE include ../rustcore/include)
target_link_libraries(common log omg_rust_core)

aux_source_directory(injector INJECTOR_SRC_LIST)
# Explicitly list files that aux_source_directory may miss on incremental
# reconfigures (CMake only rescans the dir at configure time, and Gradle's
# Android plugin does not always reconfigure when a file is added).
list(APPEND INJECTOR_SRC_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/injector/ptrace_clear.cpp)
add_library(zygisk SHARED ${INJECTOR_SRC_LIST})
target_compile_options(zygisk PRIVATE -Wno-invalid-offsetof -Wno-unused-private-field)
target_include_directories(zygisk PRIVATE include external/dobby/include)
target_link_libraries(zygisk log common lsplt_static csoloader dobby)

aux_source_directory(ptracer PTRACER_SRC_LIST)
add_executable(libzygisk_ptrace.so ${PTRACER_SRC_LIST})
target_include_directories(libzygisk_ptrace.so PRIVATE include ../rustcore/include)
target_link_libraries(libzygisk_ptrace.so log common omg_rust_core dl m)

add_subdirectory(external)
