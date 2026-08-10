find_package(Threads REQUIRED)

set(DRAW_ONLINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(draw_online_wire STATIC src/wire.c)
target_include_directories(draw_online_wire PUBLIC "${DRAW_ONLINE_INCLUDE_DIR}")
target_link_libraries(draw_online_wire PRIVATE draw_app_warnings)

add_library(draw_online_queue STATIC
    src/owned_queue.c
    src/room_request.c)
target_compile_definitions(draw_online_queue PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(draw_online_queue PUBLIC "${DRAW_ONLINE_INCLUDE_DIR}")
target_link_libraries(draw_online_queue PUBLIC Threads::Threads)
target_link_libraries(draw_online_queue PRIVATE draw_app_warnings)

add_library(draw_online_client STATIC src/client.c)
target_compile_definitions(draw_online_client PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(draw_online_client PUBLIC "${DRAW_ONLINE_INCLUDE_DIR}")
target_link_libraries(draw_online_client PUBLIC draw_online_wire)
target_link_libraries(draw_online_client PRIVATE draw_app_warnings)

add_library(draw_online_server STATIC src/server.c)
target_compile_definitions(draw_online_server PRIVATE _GNU_SOURCE)
target_include_directories(draw_online_server
    PUBLIC "${DRAW_ONLINE_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(draw_online_server
    PUBLIC
        draw_online_queue
        draw_online_wire
        Threads::Threads
        ${CMAKE_DL_LIBS})
target_link_libraries(draw_online_server PRIVATE draw_app_warnings)

add_library(draw_online_example_room MODULE plugins/example_room.c)
set_target_properties(draw_online_example_room PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draw_online_example_room"
    C_VISIBILITY_PRESET hidden)
target_include_directories(draw_online_example_room PRIVATE
    "${DRAW_ONLINE_INCLUDE_DIR}")
target_link_libraries(draw_online_example_room PRIVATE draw_app_warnings)

add_executable(draw_online_demo_server demo/server_main.c)
target_compile_definitions(draw_online_demo_server PRIVATE
    _POSIX_C_SOURCE=200809L
    DRAW_ONLINE_DEFAULT_ROOM_MODULE="$<TARGET_FILE:draw_online_example_room>")
target_link_libraries(draw_online_demo_server PRIVATE
    draw_app_warnings
    draw_online_server)
add_dependencies(draw_online_demo_server draw_online_example_room)

add_executable(draw_online_demo_client demo/client_main.c)
target_link_libraries(draw_online_demo_client PRIVATE
    draw_app_warnings
    draw_online_client)

set(DRAW_ONLINE_C11_TARGETS
    draw_online_wire
    draw_online_queue
    draw_online_client
    draw_online_server
    draw_online_example_room
    draw_online_demo_server
    draw_online_demo_client)
foreach(target IN LISTS DRAW_ONLINE_C11_TARGETS)
    set_target_properties("${target}" PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
endforeach()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
