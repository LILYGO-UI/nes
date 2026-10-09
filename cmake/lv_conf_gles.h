#ifndef LILYGO_UI_NES_LV_CONF_GLES_H
#define LILYGO_UI_NES_LV_CONF_GLES_H

#include "../third_party/cm0-appkit/config/lv_conf.h"

/* Keep AppKit's fonts and widgets while selecting its LVGL EGL/GBM backend. */
#undef LV_LINUX_DRM_USE_EGL
#undef LV_USE_OPENGLES
#define LV_USE_OPENGLES 1
#undef LV_USE_DRAW_OPENGLES
#define LV_USE_DRAW_OPENGLES 1
#undef LV_USE_3DTEXTURE
#define LV_USE_3DTEXTURE 1
#undef LV_USE_FLOAT
#define LV_USE_FLOAT 1
#undef LV_USE_MATRIX
#define LV_USE_MATRIX 1
#undef LV_USE_OPENGLES_DEBUG
#define LV_USE_OPENGLES_DEBUG 0

#endif
