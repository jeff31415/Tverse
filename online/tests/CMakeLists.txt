add_executable(draw_online_wire_test test_wire.c)
target_link_libraries(draw_online_wire_test PRIVATE
    draw_app_warnings
    draw_online_wire)

add_executable(draw_online_queue_test test_queue.c)
target_link_libraries(draw_online_queue_test PRIVATE
    draw_app_warnings
    draw_online_queue)

add_executable(draw_online_integration_test test_integration.c)
target_compile_definitions(draw_online_integration_test PRIVATE
    DRAW_ONLINE_TEST_ROOM_MODULE="$<TARGET_FILE:draw_online_example_room>")
target_link_libraries(draw_online_integration_test PRIVATE
    draw_app_warnings
    draw_online_client
    draw_online_server)
add_dependencies(draw_online_integration_test draw_online_example_room)

set(DRAW_ONLINE_TEST_TARGETS
    draw_online_wire_test
    draw_online_queue_test
    draw_online_integration_test)
foreach(target IN LISTS DRAW_ONLINE_TEST_TARGETS)
    set_target_properties("${target}" PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME "${target}" COMMAND "${target}")
    set_tests_properties("${target}" PROPERTIES TIMEOUT 10)
endforeach()

set_tests_properties(draw_online_wire_test draw_online_queue_test PROPERTIES
    LABELS "unit;online")
set_tests_properties(draw_online_integration_test PROPERTIES
    LABELS "integration;online")

add_custom_target(draw_online_tests DEPENDS ${DRAW_ONLINE_TEST_TARGETS})
