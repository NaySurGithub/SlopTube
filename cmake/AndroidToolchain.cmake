# Resolves the Android SDK/NDK and configures the NDK toolchain. Included before project().

set(SLOPTUBE_PLATFORM "host" CACHE STRING "Target platform: host or android")
set_property(CACHE SLOPTUBE_PLATFORM PROPERTY STRINGS host android)

if(NOT SLOPTUBE_PLATFORM STREQUAL "android")
    return()
endif()

set(SLOPTUBE_ANDROID_MIN_SDK 30 CACHE STRING "Minimum supported Android API level")
set(SLOPTUBE_ANDROID_TARGET_SDK 35 CACHE STRING "Target Android API level")
set(SLOPTUBE_ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")

if(NOT SLOPTUBE_ANDROID_SDK)
    foreach(candidate IN ITEMS
            "$ENV{ANDROID_HOME}"
            "$ENV{ANDROID_SDK_ROOT}"
            "/opt/homebrew/share/android-commandlinetools"
            "$ENV{HOME}/Library/Android/sdk")
        if(candidate AND EXISTS "${candidate}/platforms")
            set(SLOPTUBE_ANDROID_SDK "${candidate}" CACHE PATH "Android SDK root")
            break()
        endif()
    endforeach()
endif()

if(NOT SLOPTUBE_ANDROID_SDK)
    message(FATAL_ERROR "Android SDK not found; set ANDROID_HOME or SLOPTUBE_ANDROID_SDK")
endif()

if(NOT SLOPTUBE_ANDROID_NDK)
    file(GLOB ndk_versions LIST_DIRECTORIES true "${SLOPTUBE_ANDROID_SDK}/ndk/*")
    list(SORT ndk_versions COMPARE NATURAL ORDER DESCENDING)
    list(GET ndk_versions 0 ndk_latest)
    set(SLOPTUBE_ANDROID_NDK "${ndk_latest}" CACHE PATH "Android NDK root")
endif()

set(CMAKE_TOOLCHAIN_FILE "${SLOPTUBE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
set(ANDROID_ABI "${SLOPTUBE_ANDROID_ABI}")
set(ANDROID_PLATFORM "android-${SLOPTUBE_ANDROID_MIN_SDK}")
set(ANDROID_STL "c++_static")
set(ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES ON)
