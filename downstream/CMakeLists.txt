cmake_minimum_required(VERSION 3.24)
project(DownstreamConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CompatibilityRegistry CONFIG REQUIRED)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE CompatibilityRegistry::compat)

if(MSVC)
    target_compile_options(consumer PRIVATE /W4 /WX)
endif()
