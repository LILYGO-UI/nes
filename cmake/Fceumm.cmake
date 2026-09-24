set(FCEUMM_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/fceumm/src")
set(FCEUMM_LIBRETRO_DIR "${FCEUMM_DIR}/drivers/libretro")
set(FCEUMM_COMMON_DIR "${FCEUMM_LIBRETRO_DIR}/libretro-common")

file(GLOB FCEUMM_BOARD_SOURCES CONFIGURE_DEPENDS "${FCEUMM_DIR}/boards/*.c")
file(GLOB FCEUMM_INPUT_SOURCES CONFIGURE_DEPENDS "${FCEUMM_DIR}/input/*.c")

# Match upstream Makefile.common without the optional CRT filter and HD packs.
# Include libretro-common locally: this frontend does not provide its symbols.
add_library(fceumm STATIC
    ${FCEUMM_BOARD_SOURCES}
    ${FCEUMM_INPUT_SOURCES}
    ${FCEUMM_LIBRETRO_DIR}/libretro.c
    ${FCEUMM_LIBRETRO_DIR}/libretro_dipswitch.c
    ${FCEUMM_DIR}/cart.c
    ${FCEUMM_DIR}/cheat.c
    ${FCEUMM_DIR}/crc32.c
    ${FCEUMM_DIR}/fceu-endian.c
    ${FCEUMM_DIR}/fceu-memory.c
    ${FCEUMM_DIR}/fceu.c
    ${FCEUMM_DIR}/fds.c
    ${FCEUMM_DIR}/fds_apu.c
    ${FCEUMM_DIR}/file.c
    ${FCEUMM_DIR}/filter.c
    ${FCEUMM_DIR}/general.c
    ${FCEUMM_DIR}/input.c
    ${FCEUMM_DIR}/md5.c
    ${FCEUMM_DIR}/nsf.c
    ${FCEUMM_DIR}/palette.c
    ${FCEUMM_DIR}/ppu.c
    ${FCEUMM_DIR}/sound.c
    ${FCEUMM_DIR}/state.c
    ${FCEUMM_DIR}/video.c
    ${FCEUMM_DIR}/vsuni.c
    ${FCEUMM_DIR}/ines.c
    ${FCEUMM_DIR}/unif.c
    ${FCEUMM_DIR}/x6502.c
    ${FCEUMM_COMMON_DIR}/compat/compat_posix_string.c
    ${FCEUMM_COMMON_DIR}/compat/compat_snprintf.c
    ${FCEUMM_COMMON_DIR}/compat/compat_strcasestr.c
    ${FCEUMM_COMMON_DIR}/compat/compat_strl.c
    ${FCEUMM_COMMON_DIR}/compat/fopen_utf8.c
    ${FCEUMM_COMMON_DIR}/encodings/encoding_utf.c
    ${FCEUMM_COMMON_DIR}/file/file_path.c
    ${FCEUMM_COMMON_DIR}/file/file_path_io.c
    ${FCEUMM_COMMON_DIR}/streams/file_stream.c
    ${FCEUMM_COMMON_DIR}/streams/file_stream_transforms.c
    ${FCEUMM_COMMON_DIR}/streams/memory_stream.c
    ${FCEUMM_COMMON_DIR}/string/stdstring.c
    ${FCEUMM_COMMON_DIR}/time/rtime.c
    ${FCEUMM_COMMON_DIR}/vfs/vfs_implementation.c)

target_include_directories(fceumm SYSTEM
    PUBLIC "${FCEUMM_COMMON_DIR}/include"
    PRIVATE
        "${FCEUMM_DIR}"
        "${FCEUMM_DIR}/boards"
        "${FCEUMM_DIR}/input"
        "${FCEUMM_LIBRETRO_DIR}")
target_compile_features(fceumm PRIVATE c_std_99)
target_compile_definitions(fceumm PRIVATE
    __LIBRETRO__
    FRONTEND_SUPPORTS_RGB565
    FCEU_VERSION_NUMERIC=9900
    GIT_VERSION="236ccdf")
if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    target_compile_definitions(fceumm PRIVATE MSB_FIRST)
endif()
if(UNIX)
    target_link_libraries(fceumm PUBLIC m)
endif()
