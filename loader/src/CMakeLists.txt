cmake_minimum_required(VERSION 3.22.1)
project("loader" C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Android Gradle plugin supplies -std=c18 in CMAKE_C_FLAGS. Keep the C
# standard flag there: CMake 3.22 does not model C18 as a distinct dialect.

# Optional host-side validation for the platform-independent Rust loader
# primitives.
option(ONYX_BUILD_RUSTCORE_TESTS "Expose the platform-independent Rust tests" OFF)
if(ONYX_BUILD_RUSTCORE_TESTS)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    add_custom_target(rustcore-test
        COMMAND ${CARGO_EXECUTABLE} test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore
        COMMENT "Running Rust loader-core tests"
        VERBATIM)
endif()

# Build the portable Rust core for the ARM64 Android adapter. The Rust
# crate owns monitor state and exposes only a small C ABI; platform syscalls
# and ptrace remain in the native adapter.
find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(RUSTCORE_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore/Cargo.toml)
set(RUSTCORE_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/rustcore-target)
set(RUSTCORE_LIBRARY ${RUSTCORE_TARGET_DIR}/aarch64-linux-android/release/libonyxzygisk_loader_core.a)
file(GLOB_RECURSE RUSTCORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore/src/*.rs)
add_custom_command(
    OUTPUT ${RUSTCORE_LIBRARY}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${RUSTCORE_TARGET_DIR}
            ${CARGO_EXECUTABLE} build --manifest-path ${RUSTCORE_MANIFEST}
            --target aarch64-linux-android --release
    DEPENDS
        ${RUSTCORE_MANIFEST}
        ${RUSTCORE_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore
    COMMENT "Building Rust monitor core for Android ARM64"
    VERBATIM)
add_custom_target(rustcore-android DEPENDS ${RUSTCORE_LIBRARY})
add_library(zygisk_rustcore STATIC IMPORTED GLOBAL)
set_target_properties(zygisk_rustcore PROPERTIES IMPORTED_LOCATION ${RUSTCORE_LIBRARY})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DZKSU_VERSION=\"${ZKSU_VERSION}\")

# The work directory comes in as a plain CMake variable from the Gradle build
# (-DWORK_DIRECTORY=...); the escaped quotes produce a C string literal.
# Quoting inside the Gradle cFlags is fragile across host OSes.
add_definitions(-DWORK_DIRECTORY=\"${WORK_DIRECTORY}\")

aux_source_directory(common COMMON_SRC_LIST)
add_library(common STATIC ${COMMON_SRC_LIST})
target_include_directories(common PRIVATE include)
target_link_libraries(common log)

aux_source_directory(injector INJECTOR_SRC_LIST)
add_library(zygisk SHARED ${INJECTOR_SRC_LIST})
target_compile_options(zygisk PRIVATE -Wno-invalid-offsetof -Wno-unused-private-field)
target_include_directories(zygisk PRIVATE include external/dobby/include)
target_link_libraries(zygisk log common lsplt_static csoloader dobby)

aux_source_directory(ptracer PTRACER_SRC_LIST)
add_executable(libzygisk_ptrace.so ${PTRACER_SRC_LIST})
target_include_directories(libzygisk_ptrace.so PRIVATE include)
add_dependencies(libzygisk_ptrace.so rustcore-android)
target_link_libraries(libzygisk_ptrace.so log common zygisk_rustcore dl m)

add_subdirectory(external)
