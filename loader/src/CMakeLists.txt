cmake_minimum_required(VERSION 3.22.1)
project("loader" C CXX)

set(CMAKE_CXX_STANDARD 23)

# Optional host-side validation for the platform-independent Rust loader
# primitives. The Rust core is not linked into the Android runtime yet:
# Android's Zygisk ABI and third-party linker/hook libraries remain a
# separate compatibility boundary.
option(ONYX_BUILD_RUSTCORE_TESTS "Expose the platform-independent Rust tests" OFF)
if(ONYX_BUILD_RUSTCORE_TESTS)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    add_custom_target(rustcore-test
        COMMAND ${CARGO_EXECUTABLE} test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../rustcore
        COMMENT "Running Rust loader-core tests"
        VERBATIM)
endif()

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
# Explicitly list files that aux_source_directory may miss on incremental
# reconfigures (CMake only rescans the dir at configure time, and Gradle's
# Android plugin does not always reconfigure when a file is added).
list(APPEND INJECTOR_SRC_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/injector/ptrace_clear.cpp)
add_library(zygisk SHARED ${INJECTOR_SRC_LIST})
target_compile_options(zygisk PRIVATE -Wno-invalid offsetof -Wno-unused-private-field)
target_include_directories(zygisk PRIVATE include)
target_link_libraries(zygisk log common lsplt_static csoloader)

aux_source_directory(ptracer PTRACER_SRC_LIST)
add_executable(libzygisk_ptrace.so ${PTRACER_SRC_LIST})
target_include_directories(libzygisk_ptrace.so PRIVATE include)
target_link_libraries(libzygisk_ptrace.so log common)

add_subdirectory(external)
