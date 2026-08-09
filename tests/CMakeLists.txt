option(
    DRAW_CANVAS_TEST_SANITIZERS
    "Build draw canvas tests with AddressSanitizer and UndefinedBehaviorSanitizer"
    ON)

set(DRAW_CANVAS_TEST_TARGETS)

function(enable_draw_canvas_test_sanitizers target)
    if(NOT DRAW_CANVAS_TEST_SANITIZERS)
        return()
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options("${target}" PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer)

        get_target_property(target_type "${target}" TYPE)
        if(target_type STREQUAL "EXECUTABLE")
            # target_link_options() requires CMake 3.13, while this project
            # currently supports CMake 3.10.
            set_property(
                TARGET "${target}"
                APPEND_STRING
                PROPERTY LINK_FLAGS " -fsanitize=address,undefined")
        endif()
    else()
        message(WARNING
            "DRAW_CANVAS_TEST_SANITIZERS is ON, but ${CMAKE_C_COMPILER_ID} "
            "does not support the configured -fsanitize flags")
    endif()
endfunction()

add_library(draw_canvas_unity STATIC unity.c)
target_include_directories(draw_canvas_unity PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(draw_canvas_unity PRIVATE draw_app_warnings)
enable_draw_canvas_test_sanitizers(draw_canvas_unity)

add_library(draw_canvas_test_support STATIC canvas_test_allocator.c)
target_include_directories(draw_canvas_test_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(draw_canvas_test_support PRIVATE draw_app_warnings)
enable_draw_canvas_test_sanitizers(draw_canvas_test_support)

add_library(draw_canvas_test_subject OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas/canvas.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas/canvas_json.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas/canvas_json_cjson.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas/canvas_json_jansson.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas/canvas_state.c")
target_compile_definitions(draw_canvas_test_subject PRIVATE CANVAS_TESTING=1)
target_include_directories(draw_canvas_test_subject PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas"
    "${CMAKE_CURRENT_SOURCE_DIR}/../deps/cjson")
target_link_libraries(draw_canvas_test_subject PRIVATE
    draw_app_warnings
    corestack::tui
    ${DRAW_JANSSON_TARGET})
enable_draw_canvas_test_sanitizers(draw_canvas_test_subject)

function(add_draw_canvas_test target source)
    add_executable("${target}"
        "${source}"
        $<TARGET_OBJECTS:draw_canvas_test_subject>)
    target_include_directories("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/canvas")
    target_link_libraries("${target}" PRIVATE
        draw_app_warnings
        draw_canvas_test_support
        draw_canvas_unity
        cjson
        ${DRAW_JANSSON_TARGET}
        corestack::tui)
    enable_draw_canvas_test_sanitizers("${target}")
    set_target_properties("${target}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME "${target}" COMMAND "${target}")
    set_tests_properties("${target}" PROPERTIES TIMEOUT 10)

    set(
        DRAW_CANVAS_TEST_TARGETS
        ${DRAW_CANVAS_TEST_TARGETS} "${target}"
        PARENT_SCOPE)
endfunction()

# The regular bundle contains tests that can safely share one process.
add_draw_canvas_test(draw_canvas_test_bundle test_canvas_bundle.c)
set_tests_properties(draw_canvas_test_bundle PROPERTIES LABELS "unit")

add_draw_canvas_test(draw_canvas_test_json test_canvas_json.c)
set_tests_properties(draw_canvas_test_json PROPERTIES LABELS "unit")

# These cases get their own process so a timeout, signal, or memory corruption
# cannot prevent the regular bundle or another hazardous case from running.
add_draw_canvas_test(draw_canvas_test_render_bounds test_canvas_render_bounds.c)
set_tests_properties(draw_canvas_test_render_bounds PROPERTIES
    LABELS "unit;hazardous")

add_draw_canvas_test(
    draw_canvas_test_allocation_failures
    test_canvas_allocation_failures.c)
set_tests_properties(draw_canvas_test_allocation_failures PROPERTIES
    LABELS "unit;hazardous"
    TIMEOUT 3)

add_draw_canvas_test(
    draw_canvas_test_extreme_stroke
    test_canvas_extreme_stroke.c)
set_tests_properties(draw_canvas_test_extreme_stroke PROPERTIES
    LABELS "unit;hazardous"
    TIMEOUT 3)

add_draw_canvas_test(draw_canvas_test_history_cycle test_canvas_history_cycle.c)
set_tests_properties(draw_canvas_test_history_cycle PROPERTIES
    LABELS "unit;hazardous"
    TIMEOUT 3)

add_draw_canvas_test(
    draw_canvas_test_history_cycle_reset
    test_canvas_history_cycle_reset.c)
set_tests_properties(draw_canvas_test_history_cycle_reset PROPERTIES
    LABELS "unit;hazardous"
    TIMEOUT 3)

add_draw_canvas_test(
    draw_canvas_test_history_cycle_destroy
    test_canvas_history_cycle_destroy.c)
set_tests_properties(draw_canvas_test_history_cycle_destroy PROPERTIES
    LABELS "unit;hazardous"
    TIMEOUT 3)

add_executable(draw_plugin_loader_test test_plugin_loader.c)
target_compile_definitions(draw_plugin_loader_test PRIVATE
    TEST_PLUGIN_PATH="$<TARGET_FILE:draw_page_example>"
    TEST_CANVAS_PLUGIN_PATH="$<TARGET_FILE:draw_page_canvas>")
target_link_libraries(draw_plugin_loader_test PRIVATE
    draw_app_warnings
    draw_canvas_unity
    draw_plugin_loader)
enable_draw_canvas_test_sanitizers(draw_plugin_loader_test)
set_target_properties(draw_plugin_loader_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_dependencies(
    draw_plugin_loader_test
    draw_page_example
    draw_page_canvas)
add_test(NAME draw_plugin_loader_test COMMAND draw_plugin_loader_test)
set_tests_properties(draw_plugin_loader_test PROPERTIES
    LABELS "unit;integration"
    TIMEOUT 10)
list(APPEND DRAW_CANVAS_TEST_TARGETS draw_plugin_loader_test)

add_custom_target(
    draw_canvas_tests
    DEPENDS ${DRAW_CANVAS_TEST_TARGETS})
