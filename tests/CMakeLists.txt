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
enable_draw_canvas_test_sanitizers(draw_canvas_unity)

add_library(draw_canvas_test_support STATIC canvas_test_allocator.c)
target_include_directories(draw_canvas_test_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
enable_draw_canvas_test_sanitizers(draw_canvas_test_support)

add_library(draw_canvas_test_subject OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/../canvas.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../canvas_state.c")
target_compile_definitions(draw_canvas_test_subject PRIVATE CANVAS_TESTING=1)
target_include_directories(draw_canvas_test_subject PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}/include")
enable_draw_canvas_test_sanitizers(draw_canvas_test_subject)

function(add_draw_canvas_test target source)
    add_executable("${target}"
        "${source}"
        $<TARGET_OBJECTS:draw_canvas_test_subject>)
    target_include_directories("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries("${target}" PRIVATE
        draw_canvas_test_support
        draw_canvas_unity)
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

add_custom_target(
    draw_canvas_tests
    DEPENDS ${DRAW_CANVAS_TEST_TARGETS})
