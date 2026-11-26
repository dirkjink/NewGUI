# AccessKit's C bindings as their release ships them: the header and a static
# library per platform, built by the AccessKit project (Rust); NewGUI needs no
# Rust toolchain this way. Pinned by the archive's SHA512 (the release
# publishes no checksum of its own).
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/AccessKit/accesskit-c/releases/download/${VERSION}/accesskit-c-${VERSION}.zip"
    FILENAME "accesskit-c-${VERSION}.zip"
    SHA512 9848bd9fa6dfe34fea093d6d266380ed1dcfc289171594368d29cc58b98e0b946caba2477047137c12764b8215af4e67efefac8038b5ab69ce115175cf8ae888
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# The library of the target, as the release's accesskit.cmake picks it.
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(arch x86_64)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(arch x86)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(arch arm64)
else()
    message(FATAL_ERROR "accesskit-c: no prebuilt library for ${VCPKG_TARGET_ARCHITECTURE}")
endif()
if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_TARGET_IS_MINGW)
        set(library "lib/windows/${arch}/mingw/static/libaccesskit.a")
    else()
        set(library "lib/windows/${arch}/msvc/static/accesskit.lib")
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    set(library "lib/macos/${arch}/static/libaccesskit.a")
else()
    set(library "lib/linux/${arch}/static/libaccesskit.a")
endif()
cmake_path(GET library FILENAME library_name)

# The prebuilt library (a release build) serves both configurations: it is
# installed once, under lib.
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
# Rust's MSVC target links the release CRT dynamically (/MD), as vcpkg's
# x64-windows triplet does.
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)

file(INSTALL "${SOURCE_PATH}/include/accesskit.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/${library}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
configure_file("${CMAKE_CURRENT_LIST_DIR}/unofficial-accesskit-c-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/unofficial-accesskit-c/unofficial-accesskit-c-config.cmake"
    @ONLY)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE-MIT"
    "${SOURCE_PATH}/LICENSE-APACHE"
    "${SOURCE_PATH}/LICENSE.chromium")
