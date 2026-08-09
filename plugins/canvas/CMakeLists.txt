add_library(draw_canvas_core STATIC
    canvas.c
    canvas_json.c
    canvas_json_cjson.c
    canvas_json_jansson.c
    canvas_state.c)
set_target_properties(draw_canvas_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)
target_include_directories(draw_canvas_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CJSON_SOURCE_DIR}")
target_link_libraries(draw_canvas_core PUBLIC draw_plugin_abi)
target_link_libraries(draw_canvas_core PRIVATE
    draw_app_warnings
    cjson
    ${DRAW_JANSSON_TARGET})

add_library(draw_page_canvas MODULE canvas_page.c)
target_link_libraries(draw_page_canvas PRIVATE
    draw_app_warnings
    draw_canvas_core
    draw_plugin_frame
    cjson
    ${DRAW_JANSSON_TARGET})
set_target_properties(draw_page_canvas PROPERTIES
    C_VISIBILITY_PRESET hidden
    PREFIX ""
    OUTPUT_NAME "canvas_page"
    LIBRARY_OUTPUT_DIRECTORY "${DRAW_APP_PLUGIN_OUTPUT_DIR}")
