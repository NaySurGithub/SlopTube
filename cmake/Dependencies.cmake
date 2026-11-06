# Third-party dependencies. Versions are pinned here and nowhere else.

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(BUILD_SHARED_LIBS OFF)

# --- simdjson: JSON parsing of InnerTube responses ---------------------------------------------
set(SIMDJSON_DEVELOPER_MODE OFF CACHE INTERNAL "")
set(SIMDJSON_ENABLE_THREADS OFF CACHE INTERNAL "")
FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v5.0.5
    GIT_SHALLOW TRUE
    SYSTEM)
FetchContent_MakeAvailable(simdjson)

# --- libcurl: HTTP transport --------------------------------------------------------------------
if(ANDROID)
    # mbedTLS backend: small footprint, no OpenSSL dependency. The user config must be visible to
    # both mbedTLS and curl so they agree on the threading layer.
    add_compile_definitions("MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_CURRENT_LIST_DIR}/MbedtlsConfig.h\"")
    set(ENABLE_TESTING OFF CACHE INTERNAL "")
    set(ENABLE_PROGRAMS OFF CACHE INTERNAL "")
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "")
    FetchContent_Declare(mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v3.6.6
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(mbedtls)

    set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE INTERNAL "")
    set(MBEDTLS_LIBRARY mbedtls CACHE INTERNAL "")
    set(MBEDX509_LIBRARY mbedx509 CACHE INTERNAL "")
    set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE INTERNAL "")
    set(MBEDTLS_INCLUDE_DIRS "${MBEDTLS_INCLUDE_DIR}" CACHE INTERNAL "")
    set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto CACHE INTERNAL "")

    set(CURL_USE_MBEDTLS ON CACHE INTERNAL "")
    set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
    set(CURL_USE_LIBPSL OFF CACHE INTERNAL "")
    set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "")
    set(CURL_BROTLI OFF CACHE INTERNAL "")
    set(CURL_ZSTD OFF CACHE INTERNAL "")
    set(USE_NGHTTP2 OFF CACHE INTERNAL "")
    set(USE_LIBIDN2 OFF CACHE INTERNAL "")
    set(CURL_DISABLE_INSTALL ON CACHE INTERNAL "")
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
    set(BUILD_LIBCURL_DOCS OFF CACHE INTERNAL "")
    set(BUILD_MISC_DOCS OFF CACHE INTERNAL "")
    set(ENABLE_CURL_MANUAL OFF CACHE INTERNAL "")
    set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(HTTP_ONLY ON CACHE INTERNAL "")
    set(CURL_DISABLE_ALTSVC ON CACHE INTERNAL "")
    set(CURL_DISABLE_HSTS ON CACHE INTERNAL "")
    set(CURL_DISABLE_NETRC ON CACHE INTERNAL "")
    set(CURL_CA_BUNDLE "none" CACHE INTERNAL "")
    set(CURL_CA_PATH "none" CACHE INTERNAL "")
    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_22_0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()

# --- FreeType: glyph rasterisation, with variable font instances (app only) ---------------------
if(ANDROID)
    set(FT_DISABLE_ZLIB ON CACHE INTERNAL "")
    set(FT_DISABLE_BZIP2 ON CACHE INTERNAL "")
    set(FT_DISABLE_PNG ON CACHE INTERNAL "")
    set(FT_DISABLE_HARFBUZZ ON CACHE INTERNAL "")
    set(FT_DISABLE_BROTLI ON CACHE INTERNAL "")
    set(SKIP_INSTALL_ALL ON CACHE INTERNAL "")
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-14-3
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(freetype)
endif()

# --- Dear ImGui: immediate-mode UI (app only) ---------------------------------------------------
if(ANDROID)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_android.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
    target_include_directories(imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/freetype)
    target_compile_definitions(imgui PUBLIC
        IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEMO_WINDOWS
        IMGUI_DISABLE_DEBUG_TOOLS IMGUI_USE_WCHAR32 IMGUI_ENABLE_FREETYPE)
    target_link_libraries(imgui PUBLIC freetype android EGL GLESv3)
endif()

# --- ThorVG: Lottie animations of the Shorts like button (app only) ----------------------------
# Built here from its sources with only what is used: the Lottie loader and the software
# rasteriser (no threads, expressions, fonts or images), since its own build system is Meson.
if(ANDROID)
    FetchContent_Declare(thorvg
        GIT_REPOSITORY https://github.com/thorvg/thorvg.git
        GIT_TAG v1.1.3
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR none # Sources only: there is no CMake build to add.
        SYSTEM)
    FetchContent_MakeAvailable(thorvg)

    set(thorvg_src "${thorvg_SOURCE_DIR}/src")
    file(GLOB thorvg_sources
        "${thorvg_src}/common/*.cpp"
        "${thorvg_src}/renderer/*.cpp"
        "${thorvg_src}/renderer/cpu_engine/*.cpp"
        "${thorvg_src}/loaders/lottie/*.cpp"
        "${thorvg_src}/loaders/raw/*.cpp")
    add_library(thorvg STATIC ${thorvg_sources})
    # Meson writes this file; the features themselves are switched by the definitions below.
    set(thorvg_config "${CMAKE_CURRENT_BINARY_DIR}/thorvg-config")
    file(WRITE "${thorvg_config}/config.h" "#define THORVG_VERSION_STRING \"1.1.3\"\n")
    target_include_directories(thorvg SYSTEM PUBLIC "${thorvg_SOURCE_DIR}/inc")
    target_include_directories(thorvg PRIVATE
        "${thorvg_config}" "${thorvg_src}/common" "${thorvg_src}/renderer"
        "${thorvg_src}/renderer/cpu_engine" "${thorvg_src}/loaders/lottie"
        "${thorvg_src}/loaders/raw")
    target_compile_definitions(thorvg PUBLIC TVG_STATIC)
    target_compile_definitions(thorvg PRIVATE
        THORVG_CPU_ENGINE_SUPPORT=1 THORVG_LOTTIE_LOADER_SUPPORT=1
        $<$<STREQUAL:${ANDROID_ABI},arm64-v8a>:THORVG_NEON_SUPPORT=1>)
    target_compile_options(thorvg PRIVATE
        -w -fno-exceptions -fno-rtti -fno-math-errno -fvisibility=hidden
        $<$<CONFIG:Debug>:-O2>) # Rasterising stays fast enough to judge in debug builds.
endif()
