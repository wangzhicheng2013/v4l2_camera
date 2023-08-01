#pragma once
#define OES_TEXTURE
#include "v4l2_camera.hpp"
#include "android_ion.hpp"

#ifndef EGL_EGLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES 1
#endif
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#ifdef OES_TEXTURE
#include <drm/drm_fourcc.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif
#include <memory>
struct camera_memory_info {
    int mem_size = 0;
    int mem_fd = -1;
    void *mem_vaddr = nullptr;
#ifdef OES_TEXTURE
    EGLImageKHR khr_image;
    GLuint texture_id;
#endif
};
class v4l2_camera_userptr : public v4l2_camera {
private:
    std::unique_ptr<camera_memory_info>camera_memory_array_[BUFFER_NUM];
public:
    int texture_id = -1;
public:
    v4l2_camera_userptr() {
        set_memory_type(USERPTR_MEMORY_TYPE);
    }
    virtual ~v4l2_camera_userptr() {
        free_camera_buffers();
    }
    virtual int init() override {
        int ret = init_v4l2();
        if (ret) {
            return ret;
        }
        ret = bind_userptr();
        if (ret) {
            return ret;
        }
        if (false == start_stream()) {
            return START_STREAM_FAILED;
        }
        return 0;
    }
    virtual bool get_frame() override {
        struct v4l2_buffer readbuffer = {};
        struct v4l2_plane plane = {};
        readbuffer.type = CAPTURE_TYPE;
        readbuffer.memory = memory_type_;
        readbuffer.m.planes = &plane;     // must set
        readbuffer.length = 1;            // must set
        int ret = ioctl(video_device_fd_, VIDIOC_DQBUF, &readbuffer);    // fetch a buffer frame from the buffer
        if (ret < 0) {
            printf("get frame failed!\n");
            printf("error:%s\n", strerror(errno));
            return false;
        }
        bool success = true;
        if (readbuffer.index >= 0 && readbuffer.index < BUFFER_NUM) {
            memcpy(frame, camera_memory_array_[readbuffer.index]->mem_vaddr, frame_len);
            texture_id = camera_memory_array_[readbuffer.index]->texture_id;
            if (V4L2_PIX_FMT_UYVY == camera_pixelformat_) {
                dump_images((const char *)(camera_memory_array_[readbuffer.index]->mem_vaddr), frame_len, ".uyvy");    
            }
            else {
                dump_images((const char *)(camera_memory_array_[readbuffer.index]->mem_vaddr), frame_len, ".yuyv"); 
            }
        }
        else {
            success = false;
            printf("get frame addr:%p,error frame len:%d\n", camera_memory_array_[readbuffer.index]->mem_vaddr, camera_memory_array_[readbuffer.index]->mem_size);
        }
        // notify the kernel that it has been used
        ret = ioctl(video_device_fd_, VIDIOC_QBUF, &readbuffer);
        if (ret < 0) {
            printf("put back to queue failed!\n");
            printf("error:%s\n", strerror(errno));
            return false;
        }
        return success;      
    }
private:
    int bind_userptr() {
        struct v4l2_buffer mapbuffer;
        for (unsigned int n_buffers = 0;n_buffers < BUFFER_NUM;++n_buffers) {
            struct v4l2_plane plane[VIDEO_MAX_PLANES] = {};
            memset(&mapbuffer, 0, sizeof(mapbuffer));
            mapbuffer.type = CAPTURE_TYPE;
            mapbuffer.memory = memory_type_;
            mapbuffer.index = n_buffers;
            mapbuffer.length = 1;
            mapbuffer.m.planes = plane;     // must set             
            // The query sequence number is n_ Buffer, get its starting physical address and size
            if (ioctl(video_device_fd_, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
                printf("----VIDIOC_QUERYBUF-fail!------\n");
                printf("error:%s\n", strerror(errno));
                return -1;
            }
            printf("----VIDIOC_QUERYBUF-success!------\n");
            camera_memory_array_[n_buffers] = std::unique_ptr<camera_memory_info>(new camera_memory_info);
            auto& ptr = camera_memory_array_[n_buffers];
            ptr->mem_size = mapbuffer.m.planes[0].length;
            if (ANDROID_ION.alloc_memory(ptr->mem_size, &(ptr->mem_fd), &(ptr->mem_vaddr))) {
                printf("i = %u, length = %u, allocate memory failed!\n", n_buffers, ptr->mem_size);
                return -2;
            }
        #ifdef OES_TEXTURE
            if (false == bind_oes_texture(*ptr)) {
                return -3;
            }
        #endif
            plane[0].bytesused = ptr->mem_size;
            plane[0].length = ptr->mem_size;
            plane[0].m.userptr = (unsigned long)ptr->mem_vaddr;     // to get frame
            // VIDIOC_QBUF  // put the frame in the queue
            // VIDIOC_DQBUF // remove frame from queue
            if (ioctl (video_device_fd_, VIDIOC_QBUF, &mapbuffer) < 0) {
                printf("----------frame put into queue failed------------\n");
                printf("error:%s\n", strerror(errno));
                return -4;
            }
            printf("----------frame put into queue success!------------\n");
        }
        return 0;
    }
    bool bind_oes_texture(camera_memory_info& info) {
        int format = (V4L2_PIX_FMT_UYVY == camera_pixelformat_) ? DRM_FORMAT_UYVY : DRM_FORMAT_YUYV;
        EGLint egl_image_attrs[] = { EGL_WIDTH,
                                   static_cast<EGLint>(camera_width_),
                                   EGL_HEIGHT,
                                   static_cast<EGLint>(camera_height_),
                                   EGL_LINUX_DRM_FOURCC_EXT,
                                   format,
                                   EGL_DMA_BUF_PLANE0_FD_EXT,
                                   info.mem_fd,
                                   EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                   0,
                                   EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                   static_cast<EGLint>(camera_width_ * 2),
                                   EGL_SAMPLE_RANGE_HINT_EXT,
                                   EGL_YUV_NARROW_RANGE_EXT,
                                   EGL_YUV_COLOR_SPACE_HINT_EXT,
                                   EGL_ITU_REC601_EXT,
                                   EGL_NONE };
        info.khr_image = eglCreateImageKHR(eglGetCurrentDisplay(), 
                                            EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, egl_image_attrs);
        if (EGL_NO_IMAGE_KHR == info.khr_image) {
            printf("eglCreateImageKHR error 0x%X\n", glGetError());
            return false;
        }
        glGenTextures(1, &info.texture_id);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, info.texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)info.khr_image);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        return EGL_SUCCESS == eglGetError();
    }
    void free_camera_buffers() {
        for (auto &ptr : camera_memory_array_) {
            ANDROID_ION.free_memory(ptr->mem_fd);
        #ifdef OES_TEXTURE
            eglDestroyImageKHR(eglGetCurrentDisplay(), ptr->khr_image);
            glDeleteTextures(1, &ptr->texture_id);
        #endif
        }
    }
};