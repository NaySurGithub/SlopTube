configure_file(BuildInfo.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.hpp" @ONLY)

file(GLOB_RECURSE core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(sloptube_core STATIC ${core_sources})
add_library(sloptube::core ALIAS sloptube_core)

target_include_directories(sloptube_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(sloptube_core
    PUBLIC simdjson::simdjson
    PRIVATE sloptube::options CURL::libcurl)
