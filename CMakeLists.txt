cmake_minimum_required(VERSION 3.10)
project(draw_app LANGUAGES C)

include(CTest)

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

add_executable(draw_app
    main.c
    app.c
    canvas.c
    canvas_frame.c
    canvas_json.c
    canvas_json_cjson.c
    canvas_json_jansson.c
    canvas_state.c
    canvas_page.c
    pages.c)
target_include_directories(draw_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(draw_app PRIVATE
    draw_app_warnings
    corestack::tui
    corestack::config
    cjson
    ${DRAW_JANSSON_TARGET})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS draw_app RUNTIME DESTINATION bin)
