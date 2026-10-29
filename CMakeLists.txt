cmake_minimum_required(VERSION 3.25)

# Selects the Android NDK toolchain before project() when SLOPTUBE_PLATFORM=android.
include(cmake/AndroidToolchain.cmake)

project(SlopTube
    VERSION 0.1.0
    DESCRIPTION "Lightweight native YouTube client"
    LANGUAGES C CXX)

# Single source of truth for application identity; consumed by the manifest and BuildInfo.hpp.
set(SLOPTUBE_APP_NAME "SlopTube")
set(SLOPTUBE_APP_ID "studio.kanye.sloptube")
# Where releases are published, for the app's update check (owner/name on GitHub).
set(SLOPTUBE_REPOSITORY "NaySurGithub/SlopTube")
# Only builds signed with the release key can install a release over themselves, so only they look
# for one (CI turns this on for v* tags; see docs/releasing.md).
option(SLOPTUBE_UPDATES "Check GitHub for newer releases and offer to install them" OFF)
if(SLOPTUBE_UPDATES)
    set(SLOPTUBE_UPDATES_VALUE true)
else()
    set(SLOPTUBE_UPDATES_VALUE false)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Dependencies)

add_subdirectory(src/core)

if(ANDROID)
    include(AndroidPackaging)
    add_subdirectory(src/app)
else()
    add_subdirectory(tools/cli)
endif()
