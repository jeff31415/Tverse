# Shared AddressSanitizer/UndefinedBehaviorSanitizer wiring for first-party test
# targets. Included once from the top-level CMakeLists so that every test
# directory can opt a target in with:
#
#     draw_enable_test_sanitizers(<target>)
#
# This used to live inside tests/CMakeLists.txt and was therefore reachable only
# by the canvas tests, leaving the online tests — which cross every ownership
# boundary in the server — with no sanitizer coverage at all.

option(
    DRAW_TEST_SANITIZERS
    "Build draw test targets with AddressSanitizer and UndefinedBehaviorSanitizer"
    ON)

# Deprecated alias. Kept so existing build trees and any caller still passing
# -DDRAW_CANVAS_TEST_SANITIZERS select the same behavior as before.
if(DEFINED DRAW_CANVAS_TEST_SANITIZERS)
    set(DRAW_TEST_SANITIZERS "${DRAW_CANVAS_TEST_SANITIZERS}"
        CACHE BOOL "" FORCE)
endif()

function(draw_enable_test_sanitizers target)
    if(NOT DRAW_TEST_SANITIZERS)
        return()
    endif()
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(WARNING
            "DRAW_TEST_SANITIZERS is ON, but ${CMAKE_C_COMPILER_ID} does not "
            "support the configured -fsanitize flags")
        return()
    endif()

    target_compile_options("${target}" PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)

    get_target_property(target_type "${target}" TYPE)
    if(target_type STREQUAL "EXECUTABLE"
       OR target_type STREQUAL "MODULE_LIBRARY")
        # target_link_options() requires CMake 3.13, while this project
        # currently supports CMake 3.10.
        set_property(
            TARGET "${target}"
            APPEND_STRING
            PROPERTY LINK_FLAGS " -fsanitize=address,undefined")
    endif()
endfunction()
