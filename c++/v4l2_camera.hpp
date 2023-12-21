#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <linux/videodev2.h>
#include <iostream>
#include <string>
// use dmesg 
// adb root
// adb shell
// dmesg > tt.log
// "_max96722_i2c_write_a16r16:write reg error: reg=a007, val=131a, retry = 0" will be error
static const char *DEV_NAME = "/dev/video21cif";
static const int CAPTURE_TYPE = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
static const int MMAP_MEMORY_TYPE = V4L2_MEMORY_MMAP;
static const int USERPTR_MEMORY_TYPE = V4L2_MEMORY_USERPTR;
static const int BUFFER_NUM = 4;
static const char *FILE_PATH = "/data/";
enum v4l2_camera_error_code {
    OPEN_VIDEO_DEVICE_FAILED = -1,
    SHOW_CAMERA_FORMAT_FAILED = -2,
    SET_CAMERA_FORMAT_FAILED = -3,
    MALLOC_FRAME_FAILED = -4,
    REQUEST_BUFFER_FAILED = -5,
    BIND_MMAP_FAILED = -6,
    START_STREAM_FAILED = -7,
};
class v4l2_camera {
public:
    char *frame = nullptr;
    size_t frame_len = 0;
protected:
    const char* video_device_path_ = DEV_NAME;
    int video_device_fd_ = -1;
    int memory_type_ = MMAP_MEMORY_TYPE;
    unsigned int camera_width_ = 0;
    unsigned int camera_height_ = 0;
    int camera_pixelformat_ = 0;

    bool dump_image_flag_ = true;
    int dump_image_index_ = 0;
    int dump_image_max_ = 10;
public:
    v4l2_camera() = default;
    virtual ~v4l2_camera() {
        stop_stream();
        free_frame();
    }
    inline void set_video_device_path(const char* path) {
        video_device_path_ = path;
    }
    inline void set_memory_type(int type) {
        memory_type_ = type;
    }
    inline void set_dump_flag(bool flag) {
        dump_image_flag_ = flag;
    }
    inline void set_dump_limit(int num) {
        dump_image_max_ = num;
    }
    inline unsigned int get_width() const {
        return camera_width_;
    }
    inline unsigned int get_height() const {
        return camera_height_;
    }
    virtual bool get_frame() = 0;
    virtual int init() = 0;
    void dump_images(const char *ptr, size_t len, const char* type) {
        if (dump_image_index_ >= dump_image_max_) {
            dump_image_flag_ = false;
            return;
        }
        char file_path[128] = { 0 };
        snprintf(file_path, sizeof(file_path), "%s%d_%dx%d.%s", FILE_PATH, dump_image_index_++, camera_width_, camera_height_, type);
        FILE *fp = fopen(file_path, "wb+");
        if (nullptr == fp) {
            printf("%s open failed!\n", file_path);
            return;
        }
        if (1 != fwrite(ptr, len, 1, fp)) {
            printf("%s write error!\n", file_path);
        }
        fflush(fp);
        fclose(fp);
    }
protected:
    bool init_v4l2() {
        if (false == open_video_device()) {
            return OPEN_VIDEO_DEVICE_FAILED;
        }
        printf("get camera dev:%s, fd:%d\n", video_device_path_, video_device_fd_);
        if (false == show_camera_format()) {
            return SHOW_CAMERA_FORMAT_FAILED;
        }
        if (false == set_camera_format()) {
            return SET_CAMERA_FORMAT_FAILED;
        }
        if (false == request_buffer()) {
            return REQUEST_BUFFER_FAILED;
        }
        if (false == malloc_frame()) {
            return MALLOC_FRAME_FAILED;
        }
        return 0;      
    }
    bool open_video_device() {
        // 1. open device
        video_device_fd_ = open(video_device_path_, O_RDWR, 0); 
        if (video_device_fd_ < 0) {
            printf("%s open fail!, error:%s\n", video_device_path_, strerror(errno));
            return false;
        }
        printf("%s open success!\n", video_device_path_);
        // 2. query device capability： VIDIOC_QUERYCAP
        struct v4l2_capability cap;
        int ret = ioctl(video_device_fd_, VIDIOC_QUERYCAP, &cap);
        if (ret < 0) {
            printf("%s ioctl fail, error:%s\n", video_device_path_, strerror(errno));
            return true;
        }
        printf("%s ioctl success!\n", video_device_path_);
        printf("Driver Name:%s\nCard Name:%s\nBus info:%s\nDriver Version:%u.%u.%u\n"
        ,cap.driver, cap.card, cap.bus_info, (cap.version >> 16) & 0XFF, (cap.version >> 8) & 0XFF, cap.version & 0XFF);
        if (!(cap.capabilities & CAPTURE_TYPE)) {
            printf("V4L2_CAP_VIDEO_CAPTURE_MPLANE not supported!\n");
        }
        if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
            printf("V4L2_CAP_STREAMING not supported!\n");
        }
        return true;
    }
    bool show_camera_format() {
        struct v4l2_fmtdesc fmtdesc;
        struct v4l2_frmsizeenum frmsize;
        fmtdesc.index = 0;
        fmtdesc.type = CAPTURE_TYPE;  // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE is decoded data
        printf("fd:%d, dev:%s Support format:\n", video_device_fd_, video_device_path_);
        while (ioctl(video_device_fd_, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
            printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
            frmsize.index = fmtdesc.index;
            frmsize.pixel_format = fmtdesc.pixelformat;
            if (ioctl(video_device_fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
                printf("camera width = %d, height = %d, pixel format:%d\n",
                        frmsize.discrete.width, frmsize.discrete.height, frmsize.pixel_format);
                camera_width_ = frmsize.discrete.width;
                camera_height_ = frmsize.discrete.height;
                camera_pixelformat_ = fmtdesc.pixelformat;
            }
            else {
                printf("get failed for index:%d\n", frmsize.index);
            }
            fmtdesc.index++;
        }
        if (0 == fmtdesc.index) {
            printf("fd:%d ioctl failed!\n", video_device_fd_);
            return false;
        }
        return true;
    }
    bool set_camera_format() {
        struct v4l2_format vfmt = {}; 
        vfmt.type = CAPTURE_TYPE;          // set type camera acquisition
        vfmt.fmt.pix.pixelformat = camera_pixelformat_;          
        vfmt.fmt.pix.width = camera_width_;
        vfmt.fmt.pix.height = camera_height_;
        //vfmt.fmt.pix.field = V4L2_FIELD_ANY;
        int ret = ioctl(video_device_fd_, VIDIOC_S_FMT, &vfmt);
        if (ret < 0) {
            printf("---------set camera format failed!--------\n");
            printf("error:%s\n", strerror(errno));
            return false;
        }
        printf("current camera format information:\n\twidth:%d\n\theight:%d\n\tformat:%d\n",
                 vfmt.fmt.pix.width, vfmt.fmt.pix.height, vfmt.fmt.pix.pixelformat);
        return true;
    }
    bool request_buffer() {
        struct v4l2_requestbuffers req = {};
        req.count = BUFFER_NUM;
        req.type = CAPTURE_TYPE;
        req.memory = memory_type_;
        if (ioctl(video_device_fd_, VIDIOC_REQBUFS, &req) < 0) {
            printf("-----VIDIOC_REQBUFS-fail!------\n");
            return false;
        }
        printf("-----VIDIOC_REQBUFS-success!------\n");
        return true;
    }
    bool start_stream() {
        int type = CAPTURE_TYPE;
        if (ioctl (video_device_fd_, VIDIOC_STREAMON, &type) < 0) {
            printf("-----------start stream failed!-------\n");
            printf("error:%s\n", strerror(errno));
            return false;
        }
        printf("-----------start stream success!-------\n");
        return true;
    }
    void stop_stream() {
        int type = CAPTURE_TYPE;
        int ret = ioctl(video_device_fd_, VIDIOC_STREAMOFF, &type);
        if (ret < 0) {
            printf("close stream failed!\n");
            printf("error:%s\n", strerror(errno));
        }
        else {
            printf("close stream success!\n");
        }
        close(video_device_fd_);
        printf("close fd:%d success!\n", video_device_fd_);
        video_device_fd_ = -1;
    }
    inline bool malloc_frame() {
        switch (camera_pixelformat_) {
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            frame_len = camera_width_ * camera_height_ * 2;
            frame = (char *)malloc(sizeof(char) * frame_len);
            if (nullptr == frame) {
                return false;
            }
            return true;
            break;
        default:
            break;
        }
        printf("camera support is not uyvy or yuyv!\n");
        return false;
    }
    void free_frame() {
        if (frame != nullptr) {
            free(frame);
            frame = nullptr;
        }
    }
};
