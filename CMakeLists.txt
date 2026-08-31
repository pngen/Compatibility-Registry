cmake_minimum_required(VERSION 3.24)

project(CompatibilityRegistry VERSION 1.0.0 LANGUAGES CXX)

# Compatibility Registry 1.0.0 - Summon Software Labs
# C++20, Windows-first, MSVC /W4 /WX, CUDA-capable.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(COMPAT_BUILD_TESTS    "Build the test suite" ON)
option(COMPAT_BUILD_CLI      "Build the compat CLI" ON)
option(COMPAT_BUILD_EXAMPLES "Build runnable examples" ON)
option(COMPAT_BUILD_BENCH    "Build benchmarks" ON)
option(COMPAT_BUILD_CUDA     "Build CUDA device discovery and proofs" ON)
option(COMPAT_BUILD_SHARED   "Build the library as shared" OFF)

if(WIN32 AND MSVC)
    add_compile_options(/W4 /WX /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(COMPAT_HAVE_CUDA OFF)
if(COMPAT_BUILD_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(COMPAT_HAVE_CUDA ON)
        message(STATUS "Compatibility Registry: CUDA toolkit ${CUDAToolkit_VERSION} found")
    else()
        message(STATUS "Compatibility Registry: CUDA toolkit not found - CUDA proofs disabled")
    endif()
endif()

file(GLOB_RECURSE COMPAT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/*.cpp")
file(GLOB_RECURSE COMPAT_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/compat/*.hpp")

set(COMPAT_PUBLIC_DEFINES)
if(WIN32 AND COMPAT_BUILD_SHARED)
    list(APPEND COMPAT_PUBLIC_DEFINES COMPAT_SHARED)
endif()

set(COMPAT_LIB_TYPE STATIC)
if(COMPAT_BUILD_SHARED)
    set(COMPAT_LIB_TYPE SHARED)
endif()

add_library(compat ${COMPAT_LIB_TYPE} ${COMPAT_SOURCES} ${COMPAT_HEADERS})
add_library(CompatibilityRegistry::compat ALIAS compat)

target_include_directories(compat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(compat PUBLIC cxx_std_20)
if(COMPAT_BUILD_SHARED AND WIN32)
    target_compile_definitions(compat PRIVATE COMPAT_EXPORTS)
endif()

if(COMPAT_HAVE_CUDA)
    target_include_directories(compat PUBLIC ${CUDAToolkit_INCLUDE_DIRS})
    target_link_libraries(compat PRIVATE CUDA::cudart CUDA::cuda_driver)
    target_compile_definitions(compat PRIVATE COMPAT_HAVE_CUDA=1)
else()
    target_compile_definitions(compat PRIVATE COMPAT_HAVE_CUDA=0)
endif()

if(MSVC)
    target_compile_options(compat PRIVATE /W4 /WX)
endif()

if(COMPAT_BUILD_SHARED AND MSVC)
    set_target_properties(compat PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(COMPAT_BUILD_CUDA AND COMPAT_HAVE_CUDA)
    add_executable(compat_cuda_proof src/cuda/cuda_proof.cpp)
    target_link_libraries(compat_cuda_proof PRIVATE compat CUDA::nvrtc CUDA::cuda_driver)
    target_compile_definitions(compat_cuda_proof PRIVATE COMPAT_HAVE_CUDA=1)
    if(MSVC)
        target_compile_options(compat_cuda_proof PRIVATE /W4 /WX)
    endif()
endif()

if(COMPAT_BUILD_CLI)
    add_executable(compat_coordinator src/net/coordinator_main.cpp)
    target_link_libraries(compat_coordinator PRIVATE compat)
    if(MSVC)
        target_compile_options(compat_coordinator PRIVATE /W4 /WX)
    endif()
    add_executable(compat_net_proof src/net/net_proof.cpp)
    target_link_libraries(compat_net_proof PRIVATE compat)
    if(MSVC)
        target_compile_options(compat_net_proof PRIVATE /W4 /WX)
    endif()
    add_executable(compat_cli src/cli/main.cpp)
    set_target_properties(compat_cli PROPERTIES OUTPUT_NAME "compat")
    target_link_libraries(compat_cli PRIVATE compat)
    if(MSVC)
        target_compile_options(compat_cli PRIVATE /W4 /WX)
    endif()
endif()

if(COMPAT_BUILD_TESTS)
    enable_testing()
    file(GLOB COMPAT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    foreach(test_src ${COMPAT_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE compat)
        if(MSVC)
            target_compile_options(${test_name} PRIVATE /W4 /WX)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

if(COMPAT_BUILD_EXAMPLES)
    file(GLOB COMPAT_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
    foreach(ex_src ${COMPAT_EXAMPLE_SOURCES})
        get_filename_component(ex_name ${ex_src} NAME_WE)
        add_executable(example_${ex_name} ${ex_src})
        target_link_libraries(example_${ex_name} PRIVATE compat)
        if(MSVC)
            target_compile_options(example_${ex_name} PRIVATE /W4 /WX)
        endif()
    endforeach()
endif()

if(COMPAT_BUILD_BENCH)
    add_executable(compat_bench benchmarks/bench.cpp)
    target_link_libraries(compat_bench PRIVATE compat)
    if(MSVC)
        target_compile_options(compat_bench PRIVATE /W4 /WX)
    endif()
endif()

install(TARGETS compat
        EXPORT CompatibilityRegistryTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/compat DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CompatibilityRegistryTargets
        FILE CompatibilityRegistryTargets.cmake
        NAMESPACE CompatibilityRegistry::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CompatibilityRegistry)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompatibilityRegistryConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CompatibilityRegistryConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CompatibilityRegistry)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CompatibilityRegistryConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CompatibilityRegistryConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CompatibilityRegistryConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CompatibilityRegistry)

install(FILES
        LICENSE NOTICE CONTRIBUTING.md README.md ARCHITECTURE.md EXAMPLES.md BENCHMARKS.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/compat-docs)