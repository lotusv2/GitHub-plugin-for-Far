cmake_minimum_required(VERSION 3.20)
project(FarGitHub VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "FarGitHub is a Windows Far Manager plugin")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    FarManager
    GIT_REPOSITORY https://github.com/FarGroup/FarManager.git
    GIT_TAG master
)
FetchContent_MakeAvailable(FarManager)

add_library(FarGitHub SHARED
    src/FarGitHub.cpp
    src/FarGitHub.hpp
    src/Plugin.cpp
    src/Plugin.hpp
    src/GitHubClient.cpp
    src/GitHubClient.hpp
    src/Panel.cpp
    src/Panel.hpp
    src/resource.rc
    src/FarGitHub.def
)

target_compile_definitions(FarGitHub PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(FarGitHub PRIVATE /W4 /EHsc)
target_link_libraries(FarGitHub PRIVATE winhttp)

target_include_directories(FarGitHub PRIVATE
    ${farmanager_SOURCE_DIR}/far
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(FarGitHub PROPERTIES
    OUTPUT_NAME "FarGitHub"
    PREFIX ""
)
