# Project-wide warning and optimisation settings, applied through the sloptube::options target.

add_library(sloptube_options INTERFACE)
add_library(sloptube::options ALIAS sloptube_options)

target_compile_options(sloptube_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
    -Wno-missing-field-initializers -Wnon-virtual-dtor -Woverloaded-virtual -Wimplicit-fallthrough
    -fno-rtti
    $<$<CONFIG:Release,MinSizeRel>:-ffunction-sections -fdata-sections -fvisibility=hidden>)

if(ANDROID)
    target_link_options(sloptube_options INTERFACE
        $<$<CONFIG:Release,MinSizeRel>:-Wl,--gc-sections -Wl,--icf=all -Wl,-O2 -s>)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SLOPTUBE_IPO_SUPPORTED OUTPUT ipo_error)
if(SLOPTUBE_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
endif()
