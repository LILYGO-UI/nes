include_guard(GLOBAL)

# Debian 13 ARM64 development files and the shared objects needed by the linker.
# The BSP remains untouched; GPU drivers are supplied by Raspberry Pi OS at runtime.
function(_lilygo_extract_graphics_package path sha256 destination)
    get_filename_component(_filename "${path}" NAME)
    set(_archive "${LILYGO_GRAPHICS_CACHE_DIR}/archives/${_filename}")
    set(_download TRUE)
    if(EXISTS "${_archive}")
        file(SHA256 "${_archive}" _actual_hash)
        if(_actual_hash STREQUAL sha256)
            set(_download FALSE)
        endif()
    endif()
    if(_download)
        message(STATUS "Downloading ARM64 graphics dependency ${_filename}")
        file(DOWNLOAD "${LILYGO_GRAPHICS_MIRROR}/${path}" "${_archive}"
            EXPECTED_HASH "SHA256=${sha256}"
            TLS_VERIFY ON
            INACTIVITY_TIMEOUT 60
            STATUS _status)
        list(GET _status 0 _code)
        list(GET _status 1 _message)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_archive}")
            message(FATAL_ERROR "Cannot download ${_filename}: ${_message}")
        endif()
    endif()

    set(_unpack "${LILYGO_GRAPHICS_CACHE_DIR}/package.extracting")
    file(REMOVE_RECURSE "${_unpack}")
    file(MAKE_DIRECTORY "${_unpack}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_unpack}")
    file(GLOB _payloads "${_unpack}/data.tar.*")
    list(LENGTH _payloads _payload_count)
    if(NOT _payload_count EQUAL 1)
        message(FATAL_ERROR "No unique data archive in ${_filename}")
    endif()
    list(GET _payloads 0 _payload)
    file(ARCHIVE_EXTRACT INPUT "${_payload}" DESTINATION "${destination}")
    file(REMOVE_RECURSE "${_unpack}")
endfunction()

function(lilygo_prepare_graphics_dependencies)
    cmake_parse_arguments(_graphics "HEADERS_ONLY" "" "" ${ARGN})
    if(NOT _graphics_HEADERS_ONLY AND
       (NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"))
        message(FATAL_ERROR "The graphics dependency overlay is for AArch64 cross builds")
    endif()
    if(NOT _graphics_HEADERS_ONLY AND NOT EXISTS "${CMAKE_SYSROOT}/usr/include")
        message(FATAL_ERROR "A CM0 BSP sysroot must be configured before graphics dependencies")
    endif()

    get_filename_component(_project_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(LILYGO_GRAPHICS_CACHE_DIR "${_project_root}/.cache/cm0-graphics/trixie-arm64-v1"
        CACHE PATH "Cache for pinned ARM64 EGL, GLES and GBM dependencies")
    set(LILYGO_GRAPHICS_MIRROR "https://deb.debian.org/debian"
        CACHE STRING "Debian mirror for pinned ARM64 graphics packages")
    set(_root "${LILYGO_GRAPHICS_CACHE_DIR}/sysroot")
    set(_libdir "${_root}/usr/lib/aarch64-linux-gnu")
    file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" _manifest_hash)
    set(_ready FALSE)
    if(EXISTS "${_root}/.manifest-sha256")
        file(READ "${_root}/.manifest-sha256" _installed_hash)
        if(_installed_hash STREQUAL _manifest_hash)
            set(_ready TRUE)
        endif()
    endif()
    if(NOT _ready)
        file(MAKE_DIRECTORY "${LILYGO_GRAPHICS_CACHE_DIR}/archives")
        file(LOCK "${LILYGO_GRAPHICS_CACHE_DIR}" DIRECTORY GUARD FUNCTION TIMEOUT 180)
        set(_staging "${LILYGO_GRAPHICS_CACHE_DIR}/sysroot.extracting")
        file(REMOVE_RECURSE "${_staging}")
        file(MAKE_DIRECTORY "${_staging}")

        # Versions and SHA256 values from Debian trixie main/binary-arm64/Packages.
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libegl-dev_1.7.0-1+b2_arm64.deb"
            "888639a18354093c289ed1de95e75f927fb6979a3433b733eb2e9d9a8347b02e" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libegl1_1.7.0-1+b2_arm64.deb"
            "b663ad1777df7ed8e320f897bbf678c2baef67f5458fe4a43195ac9b444c7afb" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libgles-dev_1.7.0-1+b2_arm64.deb"
            "0f9d85f3fd85264aaef686b6e95f1c2674c1bf1433e569192c973794063de462" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libgles2_1.7.0-1+b2_arm64.deb"
            "c727fa1746ed3e343d1a2917932363d71e961eacca0443a1e2296587e03c47fb" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libglvnd0_1.7.0-1+b2_arm64.deb"
            "29ec7b8cf1c9a47055de643eb0a1921854e762b4789a7f3bc3b0dea498fd4da9" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/libg/libglvnd/libgl-dev_1.7.0-1+b2_arm64.deb"
            "0597b9a8eb74e0f7b63772f659e3b539cb95d1483f00d9c97682ef81db42345a" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/m/mesa/libgbm-dev_25.0.7-2+deb13u1_arm64.deb"
            "840596ff925ee32a93c03ec0f51864cf2915fbbb6c8b06eb1a9c7cc80b3980cd" "${_staging}")
        _lilygo_extract_graphics_package(
            "pool/main/m/mesa/libgbm1_25.0.7-2+deb13u1_arm64.deb"
            "2beb8d20ca715a01b7b2352c1167c0d7097c9d8a486704a9a966f95d4077cdfc" "${_staging}")
        foreach(_required IN ITEMS usr/include/EGL/egl.h usr/include/GLES2/gl2.h
                usr/include/KHR/khrplatform.h usr/include/gbm.h
                usr/lib/aarch64-linux-gnu/libEGL.so
                usr/lib/aarch64-linux-gnu/libGLESv2.so
                usr/lib/aarch64-linux-gnu/libGLdispatch.so.0
                usr/lib/aarch64-linux-gnu/libgbm.so)
            if(NOT EXISTS "${_staging}/${_required}")
                message(FATAL_ERROR "Incomplete graphics dependency overlay: ${_required}")
            endif()
        endforeach()
        file(WRITE "${_staging}/.manifest-sha256" "${_manifest_hash}")
        file(REMOVE_RECURSE "${_root}")
        file(RENAME "${_staging}" "${_root}")
    endif()

    # EGL/GLES/KHR headers are architecture independent; host tests load their own GL libraries.
    if(_graphics_HEADERS_ONLY)
        set(LILYGO_GRAPHICS_INCLUDE_DIRS "${_root}/usr/include" PARENT_SCOPE)
        return()
    endif()

    # Prefix paths with the BSP sysroot so pkg-config does not prepend it twice.
    # The relative suffix reaches the separate overlay without modifying the BSP.
    file(RELATIVE_PATH _relative_root "${CMAKE_SYSROOT}" "${_root}")
    set(_pkgdir "${LILYGO_GRAPHICS_CACHE_DIR}/pkgconfig")
    file(MAKE_DIRECTORY "${_pkgdir}")
    foreach(_module IN ITEMS egl glesv2 gbm)
        file(READ "${_libdir}/pkgconfig/${_module}.pc" _pc)
        string(REGEX REPLACE "prefix=/usr" "prefix=\${pc_sysrootdir}/${_relative_root}/usr" _pc "${_pc}")
        string(REPLACE "Libs: " "Libs: -Wl,-rpath-link,\${libdir} " _pc "${_pc}")
        file(WRITE "${_pkgdir}/${_module}.pc" "${_pc}")
    endforeach()
    set(ENV{PKG_CONFIG_PATH} "${_pkgdir}:$ENV{PKG_CONFIG_PATH}")

    # Imported libraries keep overlay paths scoped to graphics consumers.
    set(_names EGL GLES GBM)
    set(_libraries EGL GLESv2 gbm)
    foreach(_name _library IN ZIP_LISTS _names _libraries)
        if(NOT TARGET LilyGoGraphics::${_name})
            add_library(LilyGoGraphics::${_name} SHARED IMPORTED GLOBAL)
            set_target_properties(LilyGoGraphics::${_name} PROPERTIES
                IMPORTED_LOCATION "${_libdir}/lib${_library}.so"
                INTERFACE_INCLUDE_DIRECTORIES "${_root}/usr/include"
                INTERFACE_LINK_OPTIONS "LINKER:-rpath-link,${_libdir}")
        endif()
    endforeach()
    list(PREPEND CMAKE_FIND_ROOT_PATH "${_root}")
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_FIND_ROOT_PATH}" PARENT_SCOPE)
    set(LILYGO_GRAPHICS_INCLUDE_DIRS "${_root}/usr/include" PARENT_SCOPE)
    set(LILYGO_GRAPHICS_LIBRARY_DIR "${_libdir}" PARENT_SCOPE)
    set(LILYGO_GRAPHICS_ROOT "${_root}" PARENT_SCOPE)
endfunction()
