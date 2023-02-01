#ifndef _V4L2_CAMERA_H_
#define _V4L2_CAMERA_H_
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
static const int DEV_LEN = 13;
static const char *DEV_NAMES[DEV_LEN] = {
    "/dev/v4l-subdev0",
    "/dev/v4l-subdev1",
    "/dev/v4l-subdev2",
    "/dev/v4l-subdev3",
    "/dev/v4l-subdev4",
    "/dev/v4l-subdev5",
    "/dev/video1",
    "/dev/video-disp0",
    "/dev/video-dvr2",
    "/dev/video-dvr3",
    "/dev/video-dvr4",
    "/dev/video-dvr5",
    "/dev/video-evs6"
};
static const int CAPTURE_TYPE = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
static const int MEMORY_TYPE = V4L2_MEMORY_MMAP;
static const int BUFFER_NUM = 4;
static const char *FILE_PATH = "/data/";
enum v4l2_camera_error_code {
    GET_FD_FAILED = -1,
    SET_CAMERA_FORMAT_FAILED = -2,
    MALLOC_FRAME_FAILED = -3,
    REQUEST_BUFFER_FAILED = -4,
    BIND_MMAP_FAILED = -5,
    START_STREAM_FAILED = -6,
};
class v4l2_camera {
public:
    v4l2_camera() {
    }
    virtual ~v4l2_camera();
    int init();
    bool get_frame();
    inline unsigned int get_width() const {
        return camera_width_;
    }
    inline unsigned int get_height() const {
        return camera_height_;
    }
private:
    bool get_camera_dev_fd();
    void show_camera_format();
    bool set_camera_format();
    bool malloc_frame();
    bool request_buffer();
    int bind_mmap();
    bool start_stream();
    void dump_images(const char *ptr, size_t len);
    void stop_stream();
    bool free_frame();
public:
    char *frame = nullptr;
    size_t frame_len = 0;
private:
    int fd_ = -1;
    const char *dev_name_ = nullptr;
    unsigned int camera_width_ = 0;
    unsigned int camera_height_ = 0;
    int camera_pixelformat_ = 0;
private:
    unsigned char *mptr_[BUFFER_NUM] = { 0 };     // protect the first address of user space after mapping
    unsigned int size_[BUFFER_NUM] = { 0 };
private:
    bool dump_image_flag_ = true;
    int dump_image_index_ = 0;
    int dump_image_max_ = 10;
};
#endif