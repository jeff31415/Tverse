cmake_minimum_required(VERSION 3.10)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(draw_example_page_plugin LANGUAGES C)
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

    add_library(draw_plugin_frame STATIC
        "${DRAW_APP_SOURCE_DIR}/plugin_frame.c")
    set_target_properties(draw_plugin_frame PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(draw_plugin_frame PUBLIC
        "${DRAW_APP_SOURCE_DIR}"
        "${DRAW_APP_SOURCE_DIR}/deps/corestack/include")
endif()

add_library(draw_page_example MODULE example_page.c)
target_include_directories(draw_page_example PRIVATE
    "${DRAW_APP_SOURCE_DIR}"
    "${DRAW_APP_SOURCE_DIR}/deps/corestack/include")
target_link_libraries(draw_page_example PRIVATE draw_plugin_frame)
if(TARGET draw_app_warnings)
    target_link_libraries(draw_page_example PRIVATE draw_app_warnings)
endif()
set_target_properties(draw_page_example PROPERTIES
    C_VISIBILITY_PRESET hidden
    PREFIX ""
    OUTPUT_NAME "example_page"
    LIBRARY_OUTPUT_DIRECTORY "${DRAW_APP_PLUGIN_OUTPUT_DIR}")
