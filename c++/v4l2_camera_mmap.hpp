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
                    dump_images((const char *)(mptr_[readbuffer.index]), frame_len, ".uyvy");    
                }
                else {
                    dump_images((const char *)(mptr_[readbuffer.index]), frame_len, ".yuyv"); 
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
        struct v4l2_buffer mapbuffer;
        for (unsigned int n_buffers = 0;n_buffers < BUFFER_NUM;++n_buffers) {
            struct v4l2_plane plane[VIDEO_MAX_PLANES] = {};
            memset(&mapbuffer, 0, sizeof(mapbuffer));
            mapbuffer.type = CAPTURE_TYPE;
            mapbuffer.memory = V4L2_MEMORY_MMAP;
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
            size_[n_buffers] = mapbuffer.m.planes[0].length;
            // mapped memory
            mptr_[n_buffers] = (unsigned char *)mmap(NULL, size_[n_buffers], 
                                            PROT_READ | PROT_WRITE, MAP_SHARED, video_device_fd_,
                                            mapbuffer.m.planes[0].m.mem_offset);
            if (nullptr == mptr_[n_buffers]) {
                printf("----MMAP-fail!------\n");
                return -2;
            }
            // VIDIOC_QBUF  // put the frame in the queue
            // VIDIOC_DQBUF // remove frame from queue
            if (ioctl (video_device_fd_, VIDIOC_QBUF, &mapbuffer) < 0) {
                printf("----------frame put into queue failed------------\n");
                printf("error:%s\n", strerror(errno));
                return -3;
            }
            printf("----------frame put into queue success!------------\n");
        }
        return 0;
    }
};