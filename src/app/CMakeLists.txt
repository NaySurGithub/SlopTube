add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue SYSTEM PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue")
target_link_libraries(native_app_glue PUBLIC android log)

file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(sloptube SHARED ${app_sources})

# The logo's shape for the launch animation, from android/logo.svg.
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(ui/LogoShape.hpp.in "${generated}/app/ui/LogoShape.hpp" @ONLY)
target_include_directories(sloptube PRIVATE "${generated}")

target_link_libraries(sloptube PRIVATE
    sloptube::core
    sloptube::options
    imgui
    mbedcrypto # SHA-256 of downloaded updates (mbedTLS is built for curl anyway).
    native_app_glue
    aaudio
    android
    EGL
    GLESv3
    jnigraphics
    log
    mediandk)

# ANativeActivity_onCreate lives in the glue library and must survive dead-code stripping.
target_link_options(sloptube PRIVATE -u ANativeActivity_onCreate)

sloptube_add_apk(sloptube)
