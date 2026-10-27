# Reads android/logo.svg, the single source of the logo, for the launcher icon (Android resources)
# and the launch animation (a generated C++ header).

set(SLOPTUBE_LOGO "${PROJECT_SOURCE_DIR}/android/logo.svg")
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SLOPTUBE_LOGO}")

file(READ "${SLOPTUBE_LOGO}" logo_svg)
foreach(name IN ITEMS play drips)
    string(REGEX MATCH "id=\"${name}\" d=\"([^\"]+)\"" match "${logo_svg}")
    string(TOUPPER "${name}" upper)
    set(LOGO_${upper} "${CMAKE_MATCH_1}")
endforeach()
string(REGEX MATCH "stroke-width=\"([0-9.]+)\"" match "${logo_svg}")
set(LOGO_STROKE "${CMAKE_MATCH_1}")
string(REGEX MATCH "x1=\"([0-9.]+)\" y1=\"([0-9.]+)\" x2=\"([0-9.]+)\" y2=\"([0-9.]+)\""
    match "${logo_svg}")
set(LOGO_X1 "${CMAKE_MATCH_1}")
set(LOGO_Y1 "${CMAKE_MATCH_2}")
set(LOGO_X2 "${CMAKE_MATCH_3}")
set(LOGO_Y2 "${CMAKE_MATCH_4}")
string(REGEX MATCHALL "stop-color=\"#[0-9A-Fa-f]+\"" stops "${logo_svg}")
list(TRANSFORM stops REPLACE "stop-color=\"(#[0-9A-Fa-f]+)\"" "\\1")
list(LENGTH stops stop_count)
if(NOT LOGO_PLAY OR NOT LOGO_DRIPS OR NOT LOGO_STROKE OR NOT LOGO_Y2 OR NOT stop_count EQUAL 2)
    message(FATAL_ERROR "Unexpected layout of ${SLOPTUBE_LOGO}")
endif()
list(GET stops 0 LOGO_START_COLOR)
list(GET stops 1 LOGO_END_COLOR)

# "#RRGGBB" as an ImGui colour literal.
function(sloptube_imgui_color hex out)
    string(REGEX MATCH "#(..)(..)(..)" match "${hex}")
    set(${out} "IM_COL32(0x${CMAKE_MATCH_1}, 0x${CMAKE_MATCH_2}, 0x${CMAKE_MATCH_3}, 0xff)"
        PARENT_SCOPE)
endfunction()
sloptube_imgui_color("${LOGO_START_COLOR}" LOGO_START_IMGUI)
sloptube_imgui_color("${LOGO_END_COLOR}" LOGO_END_IMGUI)
