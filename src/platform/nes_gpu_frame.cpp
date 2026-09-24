#include "platform/nes_gpu_frame.hpp"

#if LV_USE_DRAW_OPENGLES

#include <src/drivers/opengles/lv_opengles_private.h>

#include <cstdio>

namespace {

bool supports_unpack_row_length() noexcept
{
#if LV_USE_EGL
    return GLAD_GL_EXT_unpack_subimage != 0;
#else
    return true;
#endif
}

} // namespace

NesGpuFrame::~NesGpuFrame()
{
    reset();
}

bool NesGpuFrame::fail(const char *operation, unsigned int error) noexcept
{
    std::snprintf(error_.data(), error_.size(), "%s (GL error 0x%04x)", operation, error);
    std::fprintf(stderr, "[NES GPU] %s\n", error_.data());
    return false;
}

bool NesGpuFrame::initialize() noexcept
{
    if (ready()) return true;
    if (!glGetString || !glGenTextures || !glTexImage2D || !glTexSubImage2D || !glGetString(GL_VERSION)) {
        return fail("No current OpenGL ES context", 0);
    }
    reset();
    error_.fill('\0');

    static bool renderer_logged = false;
    if (!renderer_logged) {
        std::fprintf(stderr, "[NES GPU] vendor=%s renderer=%s version=%s\n",
                     glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        renderer_logged = true;
    }

    GLint previous_texture = 0;
    GLint previous_alignment = 0;
    GLint previous_row_length = 0;
    const bool has_row_length = supports_unpack_row_length();
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
    if (has_row_length) {
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glGenTextures(1, &texture_id_);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    upload_buffer_.fill(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, NesEmulator::frame_width, NesEmulator::frame_height,
                 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, upload_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    if (has_row_length) glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
    const auto error = glGetError();
    if (error != GL_NO_ERROR || texture_id_ == 0) {
        reset();
        return fail("Could not allocate native NES texture", error);
    }
    std::fprintf(stderr, "[NES GPU] 256x240 RGB565 texture; nearest-neighbor GPU scaling enabled\n");
    return true;
}

bool NesGpuFrame::update(const std::array<std::uint16_t, NesEmulator::frame_pixels> &source) noexcept
{
    if (!ready()) return false;
    // LVGL's display texture stores BGR; the final display shader swaps back to RGB.
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto pixel = source[i];
        upload_buffer_[i] = static_cast<std::uint16_t>((pixel & 0x07e0U) | ((pixel & 0xf800U) >> 11U) |
                                                      ((pixel & 0x001fU) << 11U));
    }
    GLint previous_texture = 0;
    GLint previous_alignment = 0;
    GLint previous_row_length = 0;
    const bool has_row_length = supports_unpack_row_length();
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
    if (has_row_length) {
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NesEmulator::frame_width, NesEmulator::frame_height,
                    GL_RGB, GL_UNSIGNED_SHORT_5_6_5, upload_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    if (has_row_length) glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
    const auto error = glGetError();
    return error == GL_NO_ERROR || fail("Could not upload NES frame", error);
}

void NesGpuFrame::reset() noexcept
{
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
}

#endif
