include_guard(GLOBAL)

function(lilygo_nes_enable_gles)
    if(CM0_SIMULATOR AND NOT LILYGO_NES_GLES_TEST)
        message(FATAL_ERROR
            "The GLES application uses Linux DRM/GBM. For headless host validation "
            "use the host-gles-test preset; use host-simulator for the SDL application.")
    endif()

    get_target_property(_lvgl_source lvgl SOURCE_DIR)
    # LVGL stores raw C++ string literals in this .c shader asset.
    set_source_files_properties("${_lvgl_source}/src/drivers/opengles/assets/lv_opengles_shader.c"
        TARGET_DIRECTORY lvgl PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-x;c++")
    set(_patches
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/lvgl-9.5.0-gles-compat.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/lvgl-9.5.0-gles-display-rotation.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/lvgl-9.5.0-drm-timing.patch")
    find_program(_patch_executable patch REQUIRED)
    foreach(_patch IN LISTS _patches)
        execute_process(COMMAND "${_patch_executable}" -p1 --forward --batch --dry-run --silent -i "${_patch}"
            WORKING_DIRECTORY "${_lvgl_source}" RESULT_VARIABLE _patch_forward OUTPUT_QUIET ERROR_QUIET)
        if(_patch_forward EQUAL 0)
            execute_process(COMMAND "${_patch_executable}" -p1 --forward --batch --silent -i "${_patch}"
                WORKING_DIRECTORY "${_lvgl_source}" RESULT_VARIABLE _patch_result)
            if(NOT _patch_result EQUAL 0)
                message(FATAL_ERROR "Could not apply LVGL patch: ${_patch}")
            endif()
        else()
            execute_process(COMMAND "${_patch_executable}" -p1 --reverse --batch --dry-run --silent -i "${_patch}"
                WORKING_DIRECTORY "${_lvgl_source}" RESULT_VARIABLE _patch_reverse OUTPUT_QUIET ERROR_QUIET)
            if(NOT _patch_reverse EQUAL 0)
                message(FATAL_ERROR "The LVGL source does not match the pinned patch: ${_patch}")
            endif()
        endif()
    endforeach()

    target_sources(lvgl PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/drm_timing.c")
    target_include_directories(lvgl PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform")

    # AppKit fixes LV_CONF_PATH; preload its guarded config plus the app's GPU overrides.
    target_compile_options(lvgl PUBLIC "-include${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lv_conf_gles.h")
    target_include_directories(lvgl SYSTEM PUBLIC ${LILYGO_GRAPHICS_INCLUDE_DIRS})
    if(APPLE AND LILYGO_NES_GLES_TEST)
        target_compile_definitions(lvgl PUBLIC EGL_NO_PLATFORM_SPECIFIC_TYPES=1)
    endif()
    target_link_libraries(lvgl PUBLIC ${CMAKE_DL_LIBS})
    if(NOT CM0_SIMULATOR)
        target_link_libraries(lvgl PUBLIC LilyGoGraphics::EGL LilyGoGraphics::GLES LilyGoGraphics::GBM)
    endif()
endfunction()
