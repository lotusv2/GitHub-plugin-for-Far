cmake_minimum_required(VERSION 3.20)
project(FarGitHub VERSION 0.5.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "FarGitHub is a Windows Far Manager plugin")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Фиксируем версию официального заголовка Far Manager Plugin API.
# Официальный plugin.hpp по умолчанию включает FAR_USE_INTERNALS,
# поэтому отключаем этот режим для внешнего плагина.
set(FAR_SDK_DIR "${CMAKE_BINARY_DIR}/far-sdk")
set(FAR_PLUGIN_HPP "${FAR_SDK_DIR}/plugin.hpp")
file(MAKE_DIRECTORY "${FAR_SDK_DIR}")

if(NOT EXISTS "${FAR_PLUGIN_HPP}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/FarGroup/FarManager/457da91d334cf8d6118da9bcd7a67eeb8a2c8864/far/plugin.hpp"
        "${FAR_PLUGIN_HPP}"
        TLS_VERIFY ON
        STATUS FAR_SDK_DOWNLOAD_STATUS
    )
    list(GET FAR_SDK_DOWNLOAD_STATUS 0 FAR_SDK_DOWNLOAD_CODE)
    if(NOT FAR_SDK_DOWNLOAD_CODE EQUAL 0)
        message(FATAL_ERROR "Не удалось загрузить plugin.hpp Far Manager")
    endif()

    file(READ "${FAR_PLUGIN_HPP}" FAR_PLUGIN_HPP_CONTENT)
    string(REPLACE
        "#ifndef FAR_USE_INTERNALS\n#define FAR_USE_INTERNALS\n#endif // END FAR_USE_INTERNALS"
        "#undef FAR_USE_INTERNALS"
        FAR_PLUGIN_HPP_CONTENT
        "${FAR_PLUGIN_HPP_CONTENT}"
    )
    file(WRITE "${FAR_PLUGIN_HPP}" "${FAR_PLUGIN_HPP_CONTENT}")
endif()

add_library(FarGitHub SHARED
    src/FarGitHub.cpp
    src/FarGitHub.hpp
    src/Plugin.cpp
    src/Plugin.hpp
    src/GitHubClient.cpp
    src/GitHubClient.hpp
    src/Panel.cpp
    src/Panel.hpp
    src/PanelOperations.cpp
    src/PanelRename.cpp
    src/SaveProgress.cpp
    src/SaveProgress.hpp
    src/Settings.cpp
    src/Settings.hpp
    src/resource.rc
    src/FarGitHub.def
)

# ProcessEditorEventW вызывается Far только для плагинов с PF_EDITOR.
set_source_files_properties(src/Plugin.cpp PROPERTIES COMPILE_DEFINITIONS PF_NONE=PF_EDITOR)

target_compile_definitions(FarGitHub PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(FarGitHub PRIVATE /W4 /EHsc /FIwindows.h)
target_link_libraries(FarGitHub PRIVATE winhttp crypt32 user32)

target_include_directories(FarGitHub PRIVATE
    ${FAR_SDK_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(FarGitHub PROPERTIES
    OUTPUT_NAME "FarGitHub"
    PREFIX ""
)
