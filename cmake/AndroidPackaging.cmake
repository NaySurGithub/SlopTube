# Packages a native library into a signed APK without Gradle: aapt2 compiles the resources (the
# launcher icon, generated from android/logo.svg) and links the manifest, the library is stored
# uncompressed and page-aligned, then the archive is signed.
#
# Targets created: apk, apk-install, apk-run.

include(Logo)

set(SLOPTUBE_BUILD_TOOLS_VERSION "35.0.0" CACHE STRING "Android build-tools version")
set(debug_keystore "$ENV{HOME}/.android/debug.keystore")
set(SLOPTUBE_KEYSTORE "${debug_keystore}" CACHE FILEPATH "APK signing keystore")
# apksigner password spec: "pass:<password>", "env:<variable>" or "file:<path>". Releases read it
# from the environment so it never lands in the build files.
set(SLOPTUBE_KEYSTORE_PASS "pass:android" CACHE STRING "APK signing keystore password (apksigner spec)")

set(build_tools "${SLOPTUBE_ANDROID_SDK}/build-tools/${SLOPTUBE_BUILD_TOOLS_VERSION}")
set(platform_jar "${SLOPTUBE_ANDROID_SDK}/platforms/android-${SLOPTUBE_ANDROID_TARGET_SDK}/android.jar")
foreach(required IN ITEMS "${build_tools}/aapt2" "${platform_jar}")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing Android SDK component: ${required}")
    endif()
endforeach()

find_program(SLOPTUBE_ADB adb HINTS "${SLOPTUBE_ANDROID_SDK}/platform-tools")
find_program(SLOPTUBE_ZIP zip REQUIRED)
find_program(SLOPTUBE_JAVA java
    HINTS "$ENV{JAVA_HOME}/bin" "/opt/homebrew/opt/openjdk@17/bin" "/opt/homebrew/opt/openjdk/bin"
    NO_DEFAULT_PATH)
if(NOT SLOPTUBE_JAVA)
    message(FATAL_ERROR "A Java runtime is required by apksigner; set JAVA_HOME")
endif()
cmake_path(GET SLOPTUBE_JAVA PARENT_PATH java_bin)

# versionCode derived from the project version: MAJOR*10000 + MINOR*100 + PATCH.
math(EXPR SLOPTUBE_VERSION_CODE
    "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

# Fills the resource templates of android/res: the icon's paths and gradient come from the logo,
# the background from the app's theme, so neither is written twice.
function(sloptube_configure_resources out_dir)
    set(theme "${PROJECT_SOURCE_DIR}/src/app/ui/Theme.hpp")
    set(gradient "android:type=\"linear\" android:startX=\"${LOGO_X1}\" android:startY=\"${LOGO_Y1}\"")
    string(APPEND gradient " android:endX=\"${LOGO_X2}\" android:endY=\"${LOGO_Y2}\"")
    string(APPEND gradient " android:startColor=\"${LOGO_START_COLOR}\"")
    string(APPEND gradient " android:endColor=\"${LOGO_END_COLOR}\"")
    set(LOGO_GRADIENT "<gradient ${gradient} />")

    file(READ "${theme}" theme_source)
    string(REGEX MATCH "kBackground = IM_COL32\\(0x(..), 0x(..), 0x(..)" match "${theme_source}")
    if(NOT match)
        message(FATAL_ERROR "kBackground not found in ${theme}")
    endif()
    set(SLOPTUBE_BACKGROUND "#${CMAKE_MATCH_1}${CMAKE_MATCH_2}${CMAKE_MATCH_3}")

    file(REMOVE_RECURSE "${out_dir}")
    file(GLOB_RECURSE templates CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/android/res/*.in")
    foreach(template IN LISTS templates)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}/android/res" "${template}")
        string(REGEX REPLACE "\\.in$" "" relative "${relative}")
        configure_file("${template}" "${out_dir}/${relative}" @ONLY)
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${theme}")
endfunction()

function(sloptube_add_apk library)
    set(SLOPTUBE_LIB_NAME "${library}")
    set(work "${CMAKE_CURRENT_BINARY_DIR}/apk")
    set(manifest "${work}/AndroidManifest.xml")
    set(unsigned "${work}/unsigned.apk")
    set(aligned "${work}/aligned.apk")
    set(apk "${CMAKE_BINARY_DIR}/${SLOPTUBE_APP_NAME}.apk")
    set(lib_dir "${work}/lib/${ANDROID_ABI}")

    configure_file("${PROJECT_SOURCE_DIR}/android/AndroidManifest.xml.in" "${manifest}" @ONLY)
    sloptube_configure_resources("${work}/res")
    set(resources "${work}/resources.zip")
    file(GLOB_RECURSE resource_files "${work}/res/*")

    if(NOT EXISTS "${SLOPTUBE_KEYSTORE}")
        if(NOT SLOPTUBE_KEYSTORE STREQUAL debug_keystore)
            message(FATAL_ERROR "Signing keystore not found: ${SLOPTUBE_KEYSTORE}")
        endif()
        cmake_path(GET SLOPTUBE_KEYSTORE PARENT_PATH keystore_dir)
        file(MAKE_DIRECTORY "${keystore_dir}")
        execute_process(
            COMMAND "${java_bin}/keytool" -genkeypair -keystore "${SLOPTUBE_KEYSTORE}"
                -storepass android -keypass android -alias androiddebugkey -keyalg RSA
                -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()

    add_custom_command(
        OUTPUT "${apk}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${work}/lib" "${unsigned}" "${aligned}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${lib_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${library}>" "${lib_dir}/"
        COMMAND "${build_tools}/aapt2" compile --dir "${work}/res" -o "${resources}"
        COMMAND "${build_tools}/aapt2" link -o "${unsigned}" --manifest "${manifest}"
            -I "${platform_jar}" "${resources}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${work}" "${SLOPTUBE_ZIP}" -q -0 -r "${unsigned}" lib
        COMMAND "${build_tools}/zipalign" -f -P 16 4 "${unsigned}" "${aligned}"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${java_bin}:$ENV{PATH}"
            "${build_tools}/apksigner" sign --ks "${SLOPTUBE_KEYSTORE}"
            --ks-pass "${SLOPTUBE_KEYSTORE_PASS}" --out "${apk}" "${aligned}"
        DEPENDS ${library} "${manifest}" ${resource_files}
        COMMENT "Packaging ${SLOPTUBE_APP_NAME}.apk"
        VERBATIM)
    add_custom_target(apk ALL DEPENDS "${apk}")

    if(SLOPTUBE_ADB)
        add_custom_target(apk-install
            COMMAND "${SLOPTUBE_ADB}" install -r "${apk}"
            DEPENDS apk
            VERBATIM)
        add_custom_target(apk-run
            COMMAND "${SLOPTUBE_ADB}" shell am start -S -n
                "${SLOPTUBE_APP_ID}/android.app.NativeActivity"
            DEPENDS apk-install
            VERBATIM)
    endif()
endfunction()
