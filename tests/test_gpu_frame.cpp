#include "platform/nes_gpu_frame.hpp"
#include "pages/nes/nes_view.hpp"

#include <cm0/typography.h>

#include <src/drivers/opengles/lv_opengles_driver.h>
#include <src/drivers/opengles/lv_opengles_private.h>
#include <src/drivers/opengles/lv_opengles_texture.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#if !LV_USE_DRAW_OPENGLES || !LV_USE_EGL || !LV_USE_3DTEXTURE
#error "The GPU test requires LV_USE_DRAW_OPENGLES, LV_USE_EGL and LV_USE_3DTEXTURE"
#endif

namespace {

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

class EglContext {
public:
    ~EglContext()
    {
        if (display_ != EGL_NO_DISPLAY) {
            if (eglMakeCurrent) eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglDestroyContext && context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (eglDestroySurface && surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            if (eglTerminate) eglTerminate(display_);
        }
        if (gles_library_) dlclose(gles_library_);
        if (egl_library_) dlclose(egl_library_);
    }

    void initialize()
    {
        const auto *egl_path = std::getenv("LILYGO_NES_TEST_EGL_LIBRARY");
        const auto *gles_path = std::getenv("LILYGO_NES_TEST_GLES_LIBRARY");
        require(egl_path && gles_path,
                "Set LILYGO_NES_TEST_EGL_LIBRARY and LILYGO_NES_TEST_GLES_LIBRARY to EGL/GLES shared libraries");
        egl_library_ = dlopen(egl_path, RTLD_NOW | RTLD_LOCAL);
        require(egl_library_, "Could not load the requested EGL library");
        gles_library_ = dlopen(gles_path, RTLD_NOW | RTLD_LOCAL);
        require(gles_library_, "Could not load the requested GLES library");
        get_proc_ = symbol<PFNEGLGETPROCADDRESSPROC>(egl_library_, "eglGetProcAddress");
        const auto initialize_egl = symbol<PFNEGLINITIALIZEPROC>(egl_library_, "eglInitialize");

#if defined(__APPLE__)
        const auto get_platform_display =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(get_proc_("eglGetPlatformDisplayEXT"));
        require(get_platform_display, "The EGL library must provide ANGLE's platform display extension on macOS");
        constexpr EGLenum platform_angle = 0x3202;
        constexpr EGLint platform_angle_type = 0x3203;
        constexpr EGLint platform_angle_type_metal = 0x3489;
        const EGLint platform_attributes[] = {platform_angle_type, platform_angle_type_metal, EGL_NONE};
        display_ = get_platform_display(platform_angle, EGL_DEFAULT_DISPLAY, platform_attributes);
#else
        const auto get_display = symbol<PFNEGLGETDISPLAYPROC>(egl_library_, "eglGetDisplay");
        display_ = get_display(EGL_DEFAULT_DISPLAY);
#endif
        require(display_ != EGL_NO_DISPLAY, "Could not create the offscreen EGL display");
        EGLint major = 0;
        EGLint minor = 0;
        require(initialize_egl(display_, &major, &minor), "Could not initialize the offscreen EGL display");
        require(gladLoadEGLUserPtr(display_, load_egl, this), "Could not load EGL functions");
        require(eglBindAPI(EGL_OPENGL_ES_API), "Could not select OpenGL ES");
        const EGLint attributes[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLConfig config{};
        EGLint count = 0;
        require(eglChooseConfig(display_, attributes, &config, 1, &count) && count == 1,
                "Could not find an RGBA8 ES2 pbuffer configuration");
        const EGLint pbuffer_attributes[] = {EGL_WIDTH, 1280, EGL_HEIGHT, 1280, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, pbuffer_attributes);
        require(surface_ != EGL_NO_SURFACE, "Could not create the EGL pbuffer");
        std::vector<EGLint> context_attributes{EGL_CONTEXT_CLIENT_VERSION, 2};
        const auto *extensions = eglQueryString(display_, EGL_EXTENSIONS);
        const bool exact_es2 = extensions && std::strstr(extensions, "EGL_ANGLE_create_context_backwards_compatible");
        if (exact_es2) {
            constexpr EGLint context_backwards_compatible_angle = 0x3483;
            context_attributes.insert(context_attributes.end(), {context_backwards_compatible_angle, EGL_FALSE});
        }
        context_attributes.push_back(EGL_NONE);
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes.data());
        require(context_ != EGL_NO_CONTEXT, "Could not create an ES2 context");
        require(eglMakeCurrent(display_, surface_, surface_, context_), "Could not activate the ES2 context");
        require(gladLoadGLES2UserPtr(load_gles, this), "Could not load GLES functions");
        std::printf("Offscreen EGL %d.%d; GL %s\n", major, minor, glGetString(GL_VERSION));
        if (exact_es2) {
            require(std::strstr(reinterpret_cast<const char *>(glGetString(GL_VERSION)), "OpenGL ES 2.0"),
                    "ANGLE did not honor the exact OpenGL ES 2.0 context request");
        } else {
            std::printf("Exact ES2 context extension unavailable; the GL version above is the tested version\n");
        }
    }

private:
    template <typename T> static T symbol(void *library, const char *name)
    {
        auto result = reinterpret_cast<T>(dlsym(library, name));
        require(result, name);
        return result;
    }

    static GLADapiproc load_egl(void *data, const char *name)
    {
        auto *self = static_cast<EglContext *>(data);
        auto result = reinterpret_cast<GLADapiproc>(dlsym(self->egl_library_, name));
        return result ? result : reinterpret_cast<GLADapiproc>(self->get_proc_(name));
    }

    static GLADapiproc load_gles(void *data, const char *name)
    {
        auto *self = static_cast<EglContext *>(data);
        auto result = reinterpret_cast<GLADapiproc>(self->get_proc_(name));
        return result ? result : reinterpret_cast<GLADapiproc>(dlsym(self->gles_library_, name));
    }

    void *egl_library_ = nullptr;
    void *gles_library_ = nullptr;
    PFNEGLGETPROCADDRESSPROC get_proc_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};

using Pixels = std::vector<std::uint8_t>;
using Frame = std::array<std::uint16_t, NesEmulator::frame_pixels>;

template <typename Operation> void with_unpack_stride(Operation operation)
{
    const bool has_row_length = GLAD_GL_EXT_unpack_subimage != 0;
    GLint previous_row_length = 0;
    constexpr GLint padded_row_length = NesEmulator::frame_width + 17;
    if (has_row_length) {
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, padded_row_length);
    }
    operation();
    if (has_row_length) {
        GLint restored_row_length = 0;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &restored_row_length);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
        require(restored_row_length == padded_row_length, "NES upload did not preserve the GL unpack row length");
    }
}

Pixels present(lv_display_t *display, int width, int height)
{
    lv_refr_now(display);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    lv_opengles_viewport(0, 0, width, height);
    lv_opengles_render_display_texture(display, false, true);
    Pixels result(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    require(glGetError() == GL_NO_ERROR, "GL error during frame presentation/readback");
    return result;
}

const std::uint8_t *pixel_at(const Pixels &pixels, int width, int height, int x, int y)
{
    return pixels.data() + (static_cast<std::size_t>(height - 1 - y) * width + x) * 4;
}

void expect_color(const Pixels &pixels, int width, int height, int x, int y, std::uint32_t color)
{
    const auto *pixel = pixel_at(pixels, width, height, x, y);
    const auto red = (color >> 16U) & 0xffU;
    const auto green = (color >> 8U) & 0xffU;
    const auto blue = color & 0xffU;
    if (pixel[0] != red || pixel[1] != green || pixel[2] != blue) {
        std::fprintf(stderr, "Pixel (%d,%d): RGB %u,%u,%u; expected %u,%u,%u\n",
                     x, y, pixel[0], pixel[1], pixel[2], red, green, blue);
        throw std::runtime_error("Incorrect GPU pixel");
    }
}

void write_ppm(const std::filesystem::path &path, const Pixels &pixels, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Could not open GPU screenshot output");
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            file.write(reinterpret_cast<const char *>(pixel_at(pixels, width, height, x, y)), 3);
        }
    }
    require(file.good(), "Could not write GPU screenshot");
}

void check_texture_settings(unsigned int texture)
{
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (const auto parameter : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER}) {
        GLint value = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, parameter, &value);
        require(value == GL_NEAREST, "Native NES texture must use nearest-neighbor filtering");
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

void exercise_display_rotation(const std::filesystem::path &output)
{
    constexpr int native_width = 96;
    constexpr int native_height = 160;
    constexpr std::array<std::uint32_t, 4> colors{0xff0000, 0x00ff00, 0x0000ff, 0xffffff};
    // Physical scanout corners (TL, TR, BL, BR), matching LVGL's software
    // rotation and its inverse input transform used by the Launcher.
    constexpr int expected[][4] = {{0, 1, 2, 3}, {1, 3, 0, 2}, {3, 2, 1, 0}, {2, 0, 3, 1}};
    auto *display = lv_opengles_texture_create(native_width, native_height);
    require(display, "Could not create the rotation test display");
    lv_display_set_default(display);
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(display);
#endif
#if LV_USE_MEM_MONITOR
    lv_sysmon_hide_memory(display);
#endif
    for (int angle = 0; angle < 4; ++angle) {
        lv_display_set_rotation(display, static_cast<lv_display_rotation_t>(angle));
        auto *screen = lv_display_get_screen_active(display);
        lv_obj_clean(screen);
        lv_obj_remove_style_all(screen);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        const int width = lv_display_get_horizontal_resolution(display);
        const int height = lv_display_get_vertical_resolution(display);
        for (int corner = 0; corner < 4; ++corner) {
            auto *block = lv_obj_create(screen);
            lv_obj_remove_style_all(block);
            lv_obj_set_pos(block, (corner % 2) * width / 2, (corner / 2) * height / 2);
            lv_obj_set_size(block, width / 2, height / 2);
            lv_obj_set_style_bg_color(block, lv_color_hex(colors[corner]), 0);
            lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
        }
        const auto pixels = present(display, native_width, native_height);
        for (int corner = 0; corner < 4; ++corner) {
            expect_color(pixels, native_width, native_height,
                         corner % 2 ? native_width - 2 : 1,
                         corner / 2 ? native_height - 2 : 1,
                         colors[expected[angle][corner]]);
        }
        write_ppm(output / ("rotation-" + std::to_string(angle * 90) + ".ppm"),
                  pixels, native_width, native_height);
    }
    lv_display_delete(display);
    std::printf("Display scanout rotations 0/90/180/270 match Launcher orientation\n");
}

void exercise_frame(int width, int height, const std::filesystem::path &output, int iteration)
{
    auto *display = lv_opengles_texture_create(width, height);
    require(display, "Could not create an LVGL GPU display texture");
    lv_display_set_default(display);
    auto *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x203040), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    {
        NesGpuFrame gpu;
        with_unpack_stride([&] { require(gpu.initialize(), gpu.error()); });
        const auto texture = gpu.texture_id();
        check_texture_settings(texture);
        auto *widget = lv_3dtexture_create(screen);
        lv_obj_remove_style_all(widget);
        lv_obj_set_name_static(widget, "nes_frame_image");
        constexpr int left = 31;
        constexpr int top = 47;
        const int frame_width = width < height ? 413 : 401;
        const int frame_height = width < height ? 387 : 375;
        lv_obj_set_pos(widget, left, top);
        lv_obj_set_size(widget, frame_width, frame_height);
        lv_3dtexture_set_src(widget, texture);
        lv_3dtexture_set_flip(widget, false, false);

        Frame frame{};
        for (int y = 0; y < NesEmulator::frame_height; ++y) {
            for (int x = 0; x < NesEmulator::frame_width; ++x) {
                frame[y * NesEmulator::frame_width + x] =
                    y < 120 ? (x < 128 ? 0xf800 : 0x07e0) : (x < 128 ? 0x001f : 0xffff);
            }
        }
        with_unpack_stride([&] { require(gpu.update(frame), gpu.error()); });
        lv_obj_invalidate(widget);
        auto pixels = present(display, width, height);
        expect_color(pixels, width, height, 0, 0, 0x203040);
        expect_color(pixels, width, height, left - 1, top, 0x203040);
        expect_color(pixels, width, height, left + frame_width, top, 0x203040);
        expect_color(pixels, width, height, left + 1, top + 1, 0xff0000);
        expect_color(pixels, width, height, left + frame_width - 2, top + 1, 0x00ff00);
        expect_color(pixels, width, height, left + 1, top + frame_height - 2, 0x0000ff);
        expect_color(pixels, width, height, left + frame_width - 2, top + frame_height - 2, 0xffffff);
        const auto prefix = std::to_string(width) + "x" + std::to_string(height) + "-" + std::to_string(iteration);
        write_ppm(output / (prefix + "-colors.ppm"), pixels, width, height);

        // Updating the same texture must change the screen, without stale LVGL image-cache content.
        frame.fill(0xffe0);
        with_unpack_stride([&] { require(gpu.update(frame), gpu.error()); });
        require(gpu.texture_id() == texture, "Frame upload unexpectedly replaced the GL texture");
        lv_obj_invalidate(widget);
        pixels = present(display, width, height);
        expect_color(pixels, width, height, left + frame_width / 2, top + frame_height / 2, 0xffff00);
        expect_color(pixels, width, height, 0, 0, 0x203040);
        write_ppm(output / (prefix + "-updated.ppm"), pixels, width, height);

        // A one-pixel checkerboard must remain two colors at noninteger scaling factors.
        for (int y = 0; y < NesEmulator::frame_height; ++y) {
            for (int x = 0; x < NesEmulator::frame_width; ++x) {
                frame[y * NesEmulator::frame_width + x] = ((x ^ y) & 1) ? 0xffff : 0;
            }
        }
        with_unpack_stride([&] { require(gpu.update(frame), gpu.error()); });
        lv_obj_invalidate(widget);
        pixels = present(display, width, height);
        std::size_t white = 0;
        for (int y = top; y < top + frame_height; ++y) {
            for (int x = left; x < left + frame_width; ++x) {
                const auto *pixel = pixel_at(pixels, width, height, x, y);
                require((pixel[0] == 0 || pixel[0] == 255) && pixel[0] == pixel[1] && pixel[1] == pixel[2],
                        "Nearest-neighbor GPU scaling produced a blended pixel");
                white += pixel[0] == 255;
            }
        }
        require(white > static_cast<std::size_t>(frame_width * frame_height / 4) &&
                    white < static_cast<std::size_t>(frame_width * frame_height * 3 / 4),
                "The GPU checkerboard is blank or incorrectly scaled");
        write_ppm(output / (prefix + "-checker.ppm"), pixels, width, height);
        lv_obj_delete(widget);
        gpu.reset();
        require(glIsTexture(texture) == GL_FALSE, "NES texture leaked after reset");
        require(glGetError() == GL_NO_ERROR, "GL error during NES texture cleanup");
    }
    lv_display_delete(display);
}

void write_animated_rom(const std::filesystem::path &path)
{
    std::ofstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Could not create the GPU page's test ROM");
    const std::array<unsigned char, 16> header{'N', 'E', 'S', 0x1a, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    file.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    std::array<unsigned char, 16384> program{};
    program.fill(0xea);
    // Enable vblank NMI; each NMI changes the universal backdrop palette color.
    const unsigned char reset[] = {
        0x78, 0xd8, 0xa2, 0xff, 0x9a, 0xa9, 0x00,
        0x8d, 0x00, 0x20, 0x8d, 0x01, 0x20, 0x85, 0x00,
        0xa9, 0x80, 0x8d, 0x00, 0x20, 0x4c, 0x14, 0x80,
    };
    // Move PPUADDR outside palette RAM after changing the backdrop so the
    // disabled PPU displays $3f00 rather than the unchanged entry at $3f01.
    const unsigned char nmi[] = {
        0x48, 0x2c, 0x02, 0x20, 0xa9, 0x3f, 0x8d, 0x06, 0x20,
        0xa9, 0x00, 0x8d, 0x06, 0x20, 0xe6, 0x00, 0xa5, 0x00,
        0x29, 0x0f, 0x09, 0x20, 0x8d, 0x07, 0x20,
        0xa9, 0x20, 0x8d, 0x06, 0x20, 0xa9, 0x00, 0x8d, 0x06, 0x20, 0x68, 0x40,
    };
    std::copy_n(reset, sizeof(reset), program.begin());
    std::copy_n(nmi, sizeof(nmi), program.begin() + 0x100);
    program[0x3ffa] = 0x00;
    program[0x3ffb] = 0x81;
    program[0x3ffc] = 0x00;
    program[0x3ffd] = 0x80;
    program[0x3ffe] = 0x00;
    program[0x3fff] = 0x80;
    file.write(reinterpret_cast<const char *>(program.data()), static_cast<std::streamsize>(program.size()));
    std::array<unsigned char, 8192> characters{};
    file.write(reinterpret_cast<const char *>(characters.data()), static_cast<std::streamsize>(characters.size()));
    require(file.good(), "Could not write the GPU page's test ROM");
}

void run_page_tick()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    lv_tick_inc(20);
    lv_timer_handler();
}

void exercise_page(int width, int height, const std::filesystem::path &output)
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lilygo-nes-gpu-page-" + std::to_string(static_cast<long long>(getpid())) + "-" +
                            std::to_string(width));
    std::filesystem::create_directories(directory / "nes" / "rom");
    const auto rom = directory / "nes" / "rom" / "Animated GPU Test.nes";
    write_animated_rom(rom);
    require(setenv("HOME", directory.c_str(), 1) == 0, "Could not isolate the test ROM directory");
    require(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0, "Could not select dummy audio for the GPU test");

    auto *display = lv_opengles_texture_create(width, height);
    require(display, "Could not create the GPU page display");
    lv_display_set_default(display);
    auto *screen = lv_display_get_screen_active(display);
    const auto prefix = std::to_string(width) + "x" + std::to_string(height) + "-view";
    {
        NesViewModel model;
        NesView view(model);
        view.create(screen);
        try {
            lv_obj_update_layout(screen);
            auto *row = lv_obj_find_by_name(screen, "nes_rom_row");
            auto *library = lv_obj_find_by_name(screen, "nes_library");
            auto *game = lv_obj_find_by_name(screen, "nes_game");
            auto *image = lv_obj_find_by_name(screen, "nes_frame_image");
            auto *stop = lv_obj_find_by_name(screen, "nes_stop");
            auto *button_up = lv_obj_find_by_name(screen, "nes_control_up");
            auto *button_a = lv_obj_find_by_name(screen, "nes_control_a");
            require(row && library && game && image && stop && button_up && button_a,
                    "The actual NES GPU page is missing required widgets");
            require(model.roms().size() == 1, "The GPU page test must contain exactly one ROM");
#if LV_USE_SYSMON
            // Allow the native monitors to publish current values before capture.
            for (int tick = 0; tick < 30; ++tick) run_page_tick();
#endif
            auto pixels = present(display, width, height);
            write_ppm(output / (prefix + "-library.ppm"), pixels, width, height);

            lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
            lv_obj_update_layout(screen);
            require(model.state() == NesUiState::running, "The GPU page failed to start the test ROM");
            require(!lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN),
                    "Starting a game did not switch the actual NES page");
            lv_area_t image_area{};
            lv_obj_get_coords(image, &image_area);
            require(image_area.x1 >= 0 && image_area.y1 >= 0 && image_area.x2 < width && image_area.y2 < height,
                    "The actual NES GPU frame exceeds the display bounds");
            const int sample_x = (image_area.x1 + image_area.x2) / 2;
            const int sample_y = (image_area.y1 + image_area.y2) / 2;
            std::array<std::uint8_t, 3> previous{};
            bool sampled = false;
            int changes = 0;
            for (int attempt = 0; attempt < 120 && changes < 5; ++attempt) {
                run_page_tick();
                pixels = present(display, width, height);
                if (lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) continue;
                const auto *pixel = pixel_at(pixels, width, height, sample_x, sample_y);
                const std::array<std::uint8_t, 3> current{pixel[0], pixel[1], pixel[2]};
                if (sampled && current != previous) ++changes;
                previous = current;
                sampled = true;
            }
            require(changes >= 5, "The actual NES GPU page did not display changing emulator frames");
#if LV_USE_SYSMON
            for (int tick = 0; tick < 30; ++tick) run_page_tick();
            pixels = present(display, width, height);
#endif
            write_ppm(output / (prefix + "-game.ppm"), pixels, width, height);

            lv_obj_send_event(button_up, LV_EVENT_PRESSED, nullptr);
            lv_obj_send_event(button_a, LV_EVENT_PRESSED, nullptr);
            run_page_tick();
            pixels = present(display, width, height);
            write_ppm(output / (prefix + "-controls.ppm"), pixels, width, height);
            lv_obj_send_event(button_up, LV_EVENT_RELEASED, nullptr);
            lv_obj_send_event(button_a, LV_EVENT_RELEASED, nullptr);
            lv_obj_send_event(stop, LV_EVENT_CLICKED, nullptr);
            for (int attempt = 0; attempt < 100 && model.state() != NesUiState::library; ++attempt) run_page_tick();
            require(model.state() == NesUiState::library && !lv_obj_has_flag(library, LV_OBJ_FLAG_HIDDEN) &&
                        lv_obj_has_flag(game, LV_OBJ_FLAG_HIDDEN),
                    "Stopping the emulator did not return the GPU page to the ROM library");
            pixels = present(display, width, height);
            write_ppm(output / (prefix + "-stopped.ppm"), pixels, width, height);
            std::printf("Actual NES GPU page %dx%d: %d changing frames, controls and stop passed\n", width, height, changes);
        } catch (...) {
            view.destroy();
            throw;
        }
        view.destroy();
    }
    lv_display_delete(display);
    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        EglContext context;
        context.initialize();
        const auto *output_environment = std::getenv("LILYGO_NES_TEST_OUTPUT_DIR");
        const std::filesystem::path output = output_environment ? output_environment : "gpu-frame-output";
        std::filesystem::create_directories(output);
        lv_init();
        exercise_display_rotation(output);
        for (int iteration = 0; iteration < 3; ++iteration) {
            exercise_frame(480, 800, output, iteration);
            exercise_frame(800, 480, output, iteration);
        }
        exercise_page(568, 1232, output);
        exercise_page(1232, 568, output);
        lilygo_ui_fonts_deinit();
        lv_deinit();
        lv_opengles_deinit();
        require(glGetError() == GL_NO_ERROR, "GL error during LVGL teardown");
        const int saved_vertex_array_support = GLAD_GL_OES_vertex_array_object;
        GLAD_GL_OES_vertex_array_object = 0;
        lv_init();
        exercise_frame(480, 800, output, 3);
        lilygo_ui_fonts_deinit();
        lv_deinit();
        lv_opengles_deinit();
        GLAD_GL_OES_vertex_array_object = saved_vertex_array_support;
        require(glGetError() == GL_NO_ERROR, "GL error in the renderer fallback without VAO support");
        std::printf("Renderer fallback without OES_vertex_array_object passed\n");
        std::printf("GPU frame checks passed: colors, orientation, scaling, dynamic updates and lifecycle. PPMs: %s\n",
                    output.c_str());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "GPU frame test failed: %s\n", error.what());
        return 1;
    }
}
