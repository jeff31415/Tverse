cmake_minimum_required(VERSION 3.10)

# This file is both a working build and a copy/rename CMake template.
# When copied, rename the project, target, source file and OUTPUT_NAME.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(draw_plugin_templete LANGUAGES C)

    set(
        DRAW_APP_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../.."
        CACHE PATH
        "Path to the draw_app source tree")
    set(
        DRAW_APP_PLUGIN_OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/plugins"
        CACHE PATH
        "Directory for the built page module")

    if(NOT EXISTS "${DRAW_APP_SOURCE_DIR}/plugin.h")
        message(FATAL_ERROR
            "plugin.h was not found under DRAW_APP_SOURCE_DIR="
            "${DRAW_APP_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${DRAW_APP_SOURCE_DIR}/deps/corestack/include/tui.h")
        message(FATAL_ERROR
            "corestack headers are unavailable. Run: "
            "git submodule update --init --recursive")
    endif()

    add_library(draw_plugin_frame STATIC
        "${DRAW_APP_SOURCE_DIR}/plugin_frame.c")
    set_target_properties(draw_plugin_frame PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(draw_plugin_frame PUBLIC
        "${DRAW_APP_SOURCE_DIR}"
        "${DRAW_APP_SOURCE_DIR}/deps/corestack/include")
endif()

add_library(draw_page_plugin_templete MODULE plugin_templete.c)
target_include_directories(draw_page_plugin_templete PRIVATE
    "${DRAW_APP_SOURCE_DIR}"
    "${DRAW_APP_SOURCE_DIR}/deps/corestack/include")
target_link_libraries(draw_page_plugin_templete PRIVATE draw_plugin_frame)
if(TARGET draw_app_warnings)
    target_link_libraries(draw_page_plugin_templete PRIVATE draw_app_warnings)
endif()
set_target_properties(draw_page_plugin_templete PROPERTIES
    C_VISIBILITY_PRESET hidden
    PREFIX ""
    OUTPUT_NAME "plugin_templete"
    LIBRARY_OUTPUT_DIRECTORY "${DRAW_APP_PLUGIN_OUTPUT_DIR}")
