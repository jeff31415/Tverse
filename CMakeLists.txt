cmake_minimum_required(VERSION 3.10)
project(draw_app LANGUAGES C)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DrawTestSanitizers)

set(
    BUILD_SHARED_LIBS
    OFF
    CACHE BOOL
    "Build dependencies as static libraries"
    FORCE)

add_library(draw_app_warnings INTERFACE)
target_compile_options(draw_app_warnings INTERFACE
    -g
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wno-strict-prototypes)

set(
    CORESTACK_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/corestack"
    CACHE PATH
    "Path to the corestack source tree")

if(NOT EXISTS "${CORESTACK_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "corestack is unavailable at ${CORESTACK_SOURCE_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

set(
    CJSON_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/cjson"
    CACHE PATH
    "Path to the cJSON source tree")
if(NOT EXISTS "${CJSON_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cJSON is unavailable at ${CJSON_SOURCE_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

set(
    JANSSON_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/jansson"
    CACHE PATH
    "Path to the Jansson source tree")
if(NOT EXISTS "${JANSSON_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Jansson is unavailable at ${JANSSON_SOURCE_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(
    "${CORESTACK_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/corestack-build"
    EXCLUDE_FROM_ALL)

set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CJSON_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/cjson-build"
    EXCLUDE_FROM_ALL)
target_include_directories(cjson PUBLIC
    "${CJSON_SOURCE_DIR}")

set(JANSSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
set(JANSSON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${JANSSON_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/jansson-build"
    EXCLUDE_FROM_ALL)
set(DRAW_JANSSON_TARGET jansson::jansson)

set_target_properties(cjson jansson PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

add_library(draw_plugin_abi INTERFACE)
target_include_directories(draw_plugin_abi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CORESTACK_SOURCE_DIR}/include")

add_library(draw_plugin_frame STATIC plugin_frame.c)
set_target_properties(draw_plugin_frame PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)
target_link_libraries(draw_plugin_frame PUBLIC draw_plugin_abi)
target_link_libraries(draw_plugin_frame PRIVATE draw_app_warnings)

add_library(draw_plugin_loader STATIC plugin_loader.c)
target_link_libraries(draw_plugin_loader PUBLIC
    draw_plugin_abi
    ${CMAKE_DL_LIBS})
target_link_libraries(draw_plugin_loader PRIVATE draw_app_warnings)

set(DRAW_APP_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
add_subdirectory(plugins)

set(DRAW_APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(examples/minimal_page_plugin)
add_subdirectory(example/plugin_templete)
add_subdirectory(online)

add_executable(draw_app
    main.c
    app.c
    pages.c)
target_compile_definitions(draw_app PRIVATE
    DRAW_APP_PLUGIN_DIR="${DRAW_APP_PLUGIN_OUTPUT_DIR}")
target_link_libraries(draw_app PRIVATE
    draw_app_warnings
    draw_plugin_frame
    draw_plugin_loader
    corestack::tui
    corestack::config)
add_dependencies(draw_app draw_page_canvas draw_page_example)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS draw_app RUNTIME DESTINATION bin)
install(TARGETS draw_page_canvas draw_page_example
    LIBRARY DESTINATION lib/draw_app/plugins)
