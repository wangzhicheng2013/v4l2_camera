#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/DisplayConfig.h>
#include <ui/DisplayState.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>

using namespace android;
class android_egl {
private:
    sp<SurfaceComposerClient> flinger_;
    sp<SurfaceControl> flinger_surface_control_;
    sp<Surface> flinger_surface_;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    int screen_width_ = 0;
    int screen_height_ = 0;
private:
    android_egl() = default;
    virtual ~android_egl() {
        // release all GL resources
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display_, surface_ );
        eglDestroyContext(display_, context_ );
        eglTerminate(display_);
        surface_ = EGL_NO_SURFACE;
        context_ = EGL_NO_CONTEXT;
        display_ = EGL_NO_DISPLAY;
        if (flinger_ != nullptr) {
            flinger_.clear();
        }
        if (flinger_surface_ != nullptr) {
            flinger_surface_.clear();
        }
        if (flinger_surface_control_ != nullptr) {
            flinger_surface_control_.clear();
        }
    }
public:
    static android_egl& get_instance() {
        static android_egl instance;
        return instance;
    }
    static const char *get_egl_error() {
        switch (eglGetError()) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "Unknown error";
        }
        return "";
    }
    bool init() {
        flinger_ = new SurfaceComposerClient();
        if (nullptr == flinger_) {
            printf("new SurfaceComposerClient failed!\n");
            return false;
        }
        status_t err = flinger_->initCheck();
        if (err != NO_ERROR) {
            printf("SurfaceComposerClient::initCheck error:%#x", err);
            return false;
        }
        sp<IBinder> mainDpy = SurfaceComposerClient::getInternalDisplayToken();
        if (nullptr == numainDpy) {
            printf("no internal display!\n");
            return false;
        }
        DisplayConfig displayConfig;
        err = SurfaceComposerClient::getActiveDisplayConfig(mainDpy, &displayConfig);
        if (err != NO_ERROR) {
            printf("unable to get getActiveDisplayConfig!\n");
            return false;
        }
        ui::DisplayState displayState;
        err = SurfaceComposerClient::getDisplayState(mainDpy, &displayState);
        if (err != NO_ERROR) {
            printf("unable to get getDisplayState!\n");
            return false;
        }
        const ui::Size &resolution = displayConfig.resolution;
        auto width = resolution.getWidth();
        auto height = resolution.getHeight();
        if (displayState.orientation != ui::ROTATION_0 && displayState.orientation != ui::ROTATION_180) {
            std::swap(width, height);
        }
        flinger_surface_control_ = flinger_->createSurface(String8("Evs Display"), width, height, PIXEL_FORMAT_RGBX_8888, ISurfaceComposerClient::eOpaque);
        if ((nullptr == flinger_surface_control_) || !flinger_surface_control_->isValid()) {
            printf("failed to create SurfaceControl!\n");
            return false;
        }
        flinger_surface_ = flinger_surface_control_->getSurface();
        // set up our OpenGL ES context associated with the default display
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (EGL_NO_DISPLAY == display_) {
            printf("failed to get egl display!");
            return false;
        }
        EGLint major = 3;   // OPENGL 3.0
        EGLint minor = 0;
        if (!eglInitialize(display_, &major, &minor)) {
            printf("failed to initialize EGL:%s", get_egl_error());
            return false;
        }
        static const EGLint config_attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_NONE};

        EGLConfig egl_config = {0};
        EGLint numConfigs = -1;
        eglChooseConfig(display_, config_attribs, &egl_config, 1, &numConfigs);
        if (numConfigs != 1) {
            printf("didn't find a suitable format for our display window!");
            return false;
        }
        // Create the EGL render target surface
        surface_  = eglCreateWindowSurface(display_, egl_config, flinger_surface_.get(), nullptr);
        if (EGL_NO_SURFACE == surface_ ) {
            printf("gelCreateWindowSurface failed!\n");
            return false;
        }

        const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_  = eglCreateContext(display_, egl_config, EGL_NO_CONTEXT, context_attribs);
        if (EGL_NO_CONTEXT == context_ ) {
            printf("failed to create OpenGL ES Context:%s\n", get_egl_error());
            return false;
        }
        // Activate our render target for drawing
        if (!eglMakeCurrent(display_, surface_ , surface_ , context_ )) {
            printf("failed to make the OpenGL ES Context current:%s\n", get_egl_error());
            return false;
        }
        // Force display to sync mode
        if (!eglSwapInterval(display_, 1)) {
            printf("failed to set eglSwapInterval:%s\n", get_egl_error());
            return false;
        }
        SurfaceComposerClient::Transaction{}
        .setPosition(flinger_surface_control_, 0, 0)
	    .hide(flinger_surface_control_)
	    .apply();
        screen_width_ = width;
        screen_height_ = height;
        return true;
    }
    inline void get_screen_resolution(int *outWidth, int *outHeight) {
        *outWidth = screen_width_;
        *outHeight = screen_height_;
    }
    void egl_make_current() {
        eglMakeCurrent(display_, surface_ , surface_ , context_ );
    }
    void egl_swap_buffers() {
        eglSwapBuffers(display_, surface_ );
    }
    void show_window(int layer) {
        SurfaceComposerClient::Transaction{}.setLayer(flinger_surface_control_, layer).show(flinger_surface_control_).apply();
    }
    void hide_window() {
        SurfaceComposerClient::Transaction{}.hide(flinger_surface_control_).apply();
    }
};
#define ANDROID_EGL android_egl::get_instance()

