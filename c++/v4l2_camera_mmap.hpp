#pragma once
#include "v4l2_camera.hpp"
class v4l2_camera_mmap : public v4l2_camera {
private:
    unsigned char *mptr_[BUFFER_NUM] = { 0 };     // protect the first address of user space after mapping
    unsigned int size_[BUFFER_NUM] = { 0 };
public:
    v4l2_camera_mmap() = default;
    virtual ~v4l2_camera_mmap() {
      	for(int i = 0;i < BUFFER_NUM;i++) {
            if (mptr_[i] != nullptr) {
                munmap(mptr_[i], size_[i]);
                printf("disconnect mmap!\n");
            }
        }
    }
    virtual int init() override {
        int ret = init_v4l2();
        if (ret) {
            return ret;
        }
        ret = bind_mmap();
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
            memcpy(frame, mptr_[readbuffer.index], frame_len);
            if (true == dump_image_flag_) {
                if (V4L2_PIX_FMT_UYVY == camera_pixelformat_) {
                    // must offset 64 bytes
                    dump_images((const char *)(mptr_[readbuffer.index] + 64), frame_len, "uyvy");    
                }
                else {
                    dump_images((const char *)(mptr_[readbuffer.index]), frame_len, "yuyv"); 
                }
            }
        }
        else {
            success = false;
            printf("get frame addr:%p,error frame len:%d\n", mptr_[readbuffer.index], size_[readbuffer.index]);
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
    int bind_mmap() {
        for (int i = 0; i < BUFFER_NUM; i++) {
            struct v4l2_buffer buf = {};
            struct v4l2_plane planes = {};
            //memset(&buf, 0, sizeof(buf));
            //memset(&planes, 0, sizeof(planes));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.m.planes = &planes;
            buf.length = 1;
            int ret = ioctl(video_device_fd_, VIDIOC_QUERYBUF, &buf);
            if (ret) {
                printf("[%s]query buffer failed:%s\n", __func__, strerror(errno));
                return ret;
            }
            size_[i] = buf.m.planes[0].length;  // it is 1600 * 1300 * 2 + 64
            // mapped memory
            mptr_[i] = (unsigned char *)mmap(NULL, size_[i],
                                            PROT_READ | PROT_WRITE, MAP_SHARED, video_device_fd_,
                                            buf.m.planes[0].m.mem_offset);
            if (nullptr == mptr_[i]) {
                printf("----MMAP-fail!------\n");
                return -2;
            }
            // VIDIOC_QBUF  // put the frame in the queue
            // VIDIOC_DQBUF // remove frame from queue
            if (ioctl (video_device_fd_, VIDIOC_QBUF, &buf) < 0) {
                printf("----------frame put into queue failed------------\n");
                printf("error:%s\n", strerror(errno));
                return -3;
            }
            printf("----------frame put into queue success!------------\n");
        }
        return 0;
    }
};
