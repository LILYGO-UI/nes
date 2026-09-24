cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
option(LILYGO_NES_GLES "Render NES frames as native OpenGL ES textures" OFF)
option(LILYGO_NES_GLES_TEST "Build the offscreen GLES pixel tests" OFF)
option(LILYGO_NES_LVGL_MONITOR "Show LVGL performance and memory monitors on screen" ON)
if(LILYGO_NES_GLES_TEST AND NOT LILYGO_NES_GLES)
    message(FATAL_ERROR "LILYGO_NES_GLES_TEST requires LILYGO_NES_GLES")
endif()
if(LILYGO_NES_GLES)
    include(cmake/graphics-deps.cmake)
    if(CMAKE_CROSSCOMPILING)
        lilygo_prepare_graphics_dependencies()
    elseif(LILYGO_NES_GLES_TEST)
        lilygo_prepare_graphics_dependencies(HEADERS_ONLY)
    endif()
endif()
find_package(LilyGoUI CONFIG REQUIRED)
find_package(Threads REQUIRED)
if(LILYGO_NES_LVGL_MONITOR)
    # Share the configuration with every LVGL consumer to keep struct layouts consistent.
    target_compile_definitions(lvgl PUBLIC
        LV_USE_SYSMON=1
        LV_USE_PERF_MONITOR=1
        LV_USE_PERF_MONITOR_LOG_MODE=0
        LV_USE_PERF_MONITOR_POS=LV_ALIGN_BOTTOM_RIGHT
        LV_USE_MEM_MONITOR=1
        LV_USE_MEM_MONITOR_POS=LV_ALIGN_BOTTOM_LEFT
        LV_SYSMON_GET_IDLE=lv_timer_get_idle)
endif()
if(LILYGO_NES_GLES)
    include(cmake/NesGles.cmake)
    lilygo_nes_enable_gles()
endif()

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

include(cmake/Fceumm.cmake)

add_library(lilygo_ui_nes_core STATIC
    src/domain/rom_library.cpp
    src/emulator/nes_emulator.cpp
    src/emulator/nes_performance.cpp
    src/emulator/fceumm_core.cpp)
target_include_directories(lilygo_ui_nes_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_nes_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_nes_core PUBLIC cxx_std_17)
target_link_libraries(lilygo_ui_nes_core PUBLIC fceumm Threads::Threads)
if(CM0_SIMULATOR)
    target_compile_definitions(lilygo_ui_nes_core PRIVATE LILYGO_UI_NES_SDL_AUDIO=1)
    target_link_libraries(lilygo_ui_nes_core PRIVATE PkgConfig::SDL2)
endif()
if(UNIX)
    target_link_libraries(lilygo_ui_nes_core PUBLIC m)
endif()

add_library(lilygo_ui_nes_ui STATIC
    src/app.cpp
    src/components/components.cpp
    src/pages/nes/frame_scaler.cpp
    src/pages/nes/nes_view.cpp
    src/pages/nes/nes_view_model.cpp
    src/platform/multitouch_reader.cpp)
target_include_directories(lilygo_ui_nes_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_nes_ui PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_nes_ui PUBLIC
    lilygo_ui_nes_core
    LilyGoUI::Runtime)
if(LILYGO_NES_GLES)
    target_sources(lilygo_ui_nes_ui PRIVATE src/platform/nes_gpu_frame.cpp)
endif()

add_executable(lilygo_ui_nes src/main.cpp)
target_compile_options(lilygo_ui_nes PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_nes PRIVATE lilygo_ui_nes_ui)
set_target_properties(lilygo_ui_nes PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_nes
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}
    COMPONENT app)
install(FILES third_party/fceumm/Copying third_party/fceumm/Authors
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}/fceumm
    COMPONENT app)
install(FILES third_party/FCEUMM_ORIGIN.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}/fceumm
    RENAME ORIGIN.md
    COMPONENT app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT LILYGO_NES_GLES_TEST)
    add_executable(nes-core-tests tests/test_core.cpp)
    target_compile_options(nes-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-core-tests PRIVATE
        lilygo_ui_nes_core)
    add_test(NAME nes-core COMMAND nes-core-tests)

    add_executable(nes-fceumm-tests tests/test_fceumm.cpp)
    target_compile_options(nes-fceumm-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-fceumm-tests PRIVATE lilygo_ui_nes_core)
    add_test(NAME nes-fceumm COMMAND nes-fceumm-tests)

    add_executable(nes-emulator-tests tests/test_emulator.cpp)
    target_compile_options(nes-emulator-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-emulator-tests PRIVATE lilygo_ui_nes_core)
    add_test(NAME nes-emulator COMMAND nes-emulator-tests)

    add_executable(nes-performance-tests tests/test_performance.cpp)
    target_compile_options(nes-performance-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-performance-tests PRIVATE lilygo_ui_nes_core)
    add_test(NAME nes-performance COMMAND nes-performance-tests)

    add_executable(nes-drm-timing-tests tests/test_drm_timing.cpp src/platform/drm_timing.c)
    target_compile_features(nes-drm-timing-tests PRIVATE c_std_11 cxx_std_17)
    target_compile_options(nes-drm-timing-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(nes-drm-timing-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME nes-drm-timing COMMAND nes-drm-timing-tests)

    add_executable(nes-frame-tests tests/test_frame_scaler.cpp)
    target_compile_options(nes-frame-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-frame-tests PRIVATE lilygo_ui_nes_ui)
    add_test(NAME nes-frame COMMAND nes-frame-tests)

    if(CM0_SIMULATOR)
        add_executable(nes-audio-tests tests/test_audio.cpp)
        target_compile_options(nes-audio-tests PRIVATE
            -Wall -Wextra -Wpedantic -UNDEBUG)
        target_link_libraries(nes-audio-tests PRIVATE
            lilygo_ui_nes_core PkgConfig::SDL2)
        add_test(NAME nes-audio COMMAND nes-audio-tests)
        set_tests_properties(nes-audio PROPERTIES ENVIRONMENT "NES_AUDIO=1")
    endif()

    add_executable(nes-pcm-ring-tests tests/test_pcm_ring_buffer.cpp)
    target_compile_options(nes-pcm-ring-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_compile_features(nes-pcm-ring-tests PRIVATE cxx_std_17)
    target_include_directories(nes-pcm-ring-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(nes-pcm-ring-tests PRIVATE Threads::Threads)
    add_test(NAME nes-pcm-ring COMMAND nes-pcm-ring-tests)

    add_executable(nes-device-audio-tests
        tests/test_device_audio.cpp
        src/emulator/nes_emulator.cpp
        src/emulator/nes_performance.cpp
        src/emulator/fceumm_core.cpp)
    target_compile_options(nes-device-audio-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_compile_features(nes-device-audio-tests PRIVATE cxx_std_17)
    target_include_directories(nes-device-audio-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(nes-device-audio-tests PRIVATE fceumm Threads::Threads)
    add_test(NAME nes-device-audio COMMAND nes-device-audio-tests)
    set_tests_properties(nes-device-audio PROPERTIES ENVIRONMENT "NES_AUDIO=1")

    add_executable(nes-view-model-tests tests/test_view_model.cpp)
    target_compile_options(nes-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-view-model-tests PRIVATE
        lilygo_ui_nes_ui)
    add_test(NAME nes-view-model COMMAND nes-view-model-tests)

    add_executable(nes-render-ui tests/render_ui.cpp)
    target_compile_options(nes-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-render-ui PRIVATE
        lilygo_ui_nes_ui)
    foreach(viewport IN ITEMS portrait landscape)
        if(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        else()
            set(_width 1232)
            set(_height 568)
        endif()
        # Keep the user's ROM library out of the single-ROM render fixture.
        add_test(NAME nes-render-${viewport} COMMAND
            ${CMAKE_COMMAND} -E env --unset=HOME $<TARGET_FILE:nes-render-ui>
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/nes-${viewport}.ppm)
        if(CM0_SIMULATOR)
            set_tests_properties(nes-render-${viewport} PROPERTIES
                ENVIRONMENT "SDL_AUDIODRIVER=dummy")
        endif()
    endforeach()
endif()

if(BUILD_TESTING AND LILYGO_NES_GLES_TEST AND NOT CMAKE_CROSSCOMPILING)
    add_executable(nes-gpu-tests tests/test_gpu_frame.cpp)
    target_compile_options(nes-gpu-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(nes-gpu-tests PRIVATE lilygo_ui_nes_ui ${CMAKE_DL_LIBS})
    add_test(NAME nes-gpu COMMAND
        ${CMAKE_COMMAND} -E env --unset=HOME $<TARGET_FILE:nes-gpu-tests>)
endif()

# New applications have no legacy CM0 package to replace. Add Debian migration
# fields only when this project supersedes an actually published package.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libfreetype6, alsa-utils, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
if(LILYGO_NES_GLES)
    string(APPEND CPACK_DEBIAN_APP_PACKAGE_DEPENDS ", libegl1, libgles2, libgbm1, libegl-mesa0, libgl1-mesa-dri")
    set(CPACK_DEBIAN_APP_FILE_NAME "${LILYGO_UI_PACKAGE_NAME}_${LILYGO_UI_VERSION}_arm64-gles.deb")
endif()
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
