cmake_minimum_required(VERSION 3.10)
project(draw_app LANGUAGES C)

include(CTest)

add_compile_options(
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

add_subdirectory(
    "${CORESTACK_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/corestack-build"
    EXCLUDE_FROM_ALL)

add_executable(draw_app
    main.c
    app.c
    canvas.c
    canvas_state.c
    canvas_page.c
    pages.c)
target_include_directories(draw_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(draw_app PRIVATE corestack::tui corestack::config)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS draw_app RUNTIME DESTINATION bin)
