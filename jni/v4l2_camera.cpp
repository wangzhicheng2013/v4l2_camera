#include "v4l2_camera.h"
bool v4l2_camera::get_camera_dev_fd() {
    for (int i = 0;i < DEV_LEN;i++) {
        // 1. open device
        fd_ = open(DEV_NAMES[i], O_RDWR, 0); 
        if (fd_ < 0) {
            printf("%s open fail!, error:%s\n", DEV_NAMES[i], strerror(errno));
            continue;
        }
        printf("%s open success!\n", DEV_NAMES[i]);
        // 2. query device capability： VIDIOC_QUERYCAP
        struct v4l2_capability cap;
        int ret = ioctl(fd_, VIDIOC_QUERYCAP, &cap);
        if (ret < 0) {
            printf("%s ioctl fail, error:%s\n", DEV_NAMES[i], strerror(errno));
            close(fd_);
            continue;
        }
        printf("%s ioctl success!\n", DEV_NAMES[i]);
        printf("Driver Name:%s\nCard Name:%s\nBus info:%s\nDriver Version:%u.%u.%u\n"
        ,cap.driver, cap.card, cap.bus_info, (cap.version >> 16) & 0XFF, (cap.version >> 8) & 0XFF, cap.version & 0XFF);
        dev_name_ = DEV_NAMES[i];
        return true;
    }  
    return false;    
}
void v4l2_camera::show_camera_format() {
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_frmsizeenum frmsize;
    fmtdesc.index = 0; 
    fmtdesc.type = CAPTURE_TYPE;  // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE is decoded data
    printf("fd_:%d, dev:%s Support format:\n", fd_, dev_name_);
    while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
        printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        frmsize.index = fmtdesc.index;
        frmsize.pixel_format = fmtdesc.pixelformat;
        if (ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
            printf("camera width = %d, height = %d, pixel format:%d\n", frmsize.discrete.width, frmsize.discrete.height, frmsize.pixel_format);
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
        printf("fd_:%d ioctl failed!\n", fd_);
    }
}
bool v4l2_camera::set_camera_format() {
    struct v4l2_format vfmt; 
    vfmt.type = CAPTURE_TYPE;          // set type camera acquisition
    vfmt.fmt.pix.pixelformat = camera_pixelformat_;          
    vfmt.fmt.pix.width = camera_width_;
    vfmt.fmt.pix.height = camera_height_;
    vfmt.fmt.pix.field = V4L2_FIELD_ANY;  // must set
    int ret = ioctl(fd_, VIDIOC_S_FMT, &vfmt);
    if (ret < 0) {
        printf("---------set camera format failed!--------\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    printf("current camera format information:\n\twidth:%d\n\theight:%d\n\tformat:%d\n", vfmt.fmt.pix.width, vfmt.fmt.pix.height, vfmt.fmt.pix.pixelformat);
    return true;
}
bool v4l2_camera::malloc_frame() {
    switch (camera_pixelformat_)
    {
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
bool v4l2_camera::request_buffer() {
    struct v4l2_requestbuffers req = {};
    req.count = BUFFER_NUM;
    req.type = CAPTURE_TYPE;
    req.memory = MEMORY_TYPE;
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        printf("-----VIDIOC_REQBUFS-fail!------\n");
        return false;
    }
    printf("-----VIDIOC_REQBUFS-success!------\n");
    return true;
}
int v4l2_camera::bind_mmap() {
    struct v4l2_buffer mapbuffer;
    for (unsigned int n_buffers = 0;n_buffers < BUFFER_NUM;++n_buffers) {
        struct v4l2_plane plane[VIDEO_MAX_PLANES] = {};
        memset(&mapbuffer, 0, sizeof(mapbuffer));
        mapbuffer.type = CAPTURE_TYPE;
        mapbuffer.memory = MEMORY_TYPE;
        mapbuffer.index = n_buffers;
        mapbuffer.length = 1;
        mapbuffer.m.planes = plane;     // must set             
        // The query sequence number is n_ Buffer, get its starting physical address and size
        if (ioctl(fd_, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            printf("----VIDIOC_QUERYBUF-fail!------\n");
            printf("error:%s\n", strerror(errno));
            return -1;
        }
        printf("----VIDIOC_QUERYBUF-success!------\n");
        size_[n_buffers] = mapbuffer.m.planes[0].length;
        // mapped memory
        mptr_[n_buffers] = (unsigned char *)mmap(NULL, size_[n_buffers], PROT_READ | PROT_WRITE, MAP_SHARED, fd_, mapbuffer.m.planes[0].m.mem_offset);
        if (nullptr == mptr_[n_buffers]) {
            printf("----MMAP-fail!------\n");
            return -2;
        }
        // VIDIOC_QBUF  // put the frame in the queue
		// VIDIOC_DQBUF // remove frame from queue
        if (ioctl (fd_, VIDIOC_QBUF, &mapbuffer) < 0) {
             printf("----------frame put into queue failed------------\n");
             printf("error:%s\n", strerror(errno));
             return -3;
        }
        printf("----------frame put into queue success!------------\n");
    }
    return 0;
}
bool v4l2_camera::start_stream() {
    int type = CAPTURE_TYPE;
    if (ioctl (fd_, VIDIOC_STREAMON, &type) < 0) {
        printf("-----------start stream failed!-------\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    printf("-----------start stream success!-------\n");
    return true;
}
void v4l2_camera::dump_images(const char *ptr, size_t len) {
    if (dump_image_index_ >= dump_image_max_) {
        dump_image_flag_ = false;
        return;
    }
    char file_path[128] = { 0 };
    snprintf(file_path, sizeof(file_path), "%s%d_%dx%d.uyvy", FILE_PATH, dump_image_index_++, camera_width_, camera_height_);
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
bool v4l2_camera::get_frame() {
    struct v4l2_buffer readbuffer = {};
    struct v4l2_plane plane = {};
    readbuffer.type = CAPTURE_TYPE;
    readbuffer.memory = MEMORY_TYPE;
    readbuffer.m.planes = &plane;     // must set
    readbuffer.length = 1;            // must set
    int ret = ioctl(fd_, VIDIOC_DQBUF, &readbuffer);    // fetch a buffer frame from the buffer
    if (ret < 0) {
        printf("get frame failed!\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    ret = ioctl(fd_, VIDIOC_QUERYBUF, &readbuffer);
    if (ret < 0) {
        printf("query buf failed!\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    if (V4L2_BUF_FLAG_DONE != readbuffer.flags) {       // V4L2_BUF_FLAG_DONE means that frame is ready
        return false;
    }
    bool success = false;
    if (readbuffer.index >= 0 && readbuffer.index < BUFFER_NUM) {
        if (frame_len == size_[readbuffer.index]) {
            memcpy(frame, mptr_[readbuffer.index], frame_len);
            success = true;
        }
        else {
            printf("get frame addr:%p,error frame len:%d\n", mptr_[readbuffer.index], size_[readbuffer.index]);
        }
        //printf("get frame addr:%p,frame len:%d\n", mptr_[readbuffer.index], size_[readbuffer.index]);
    }
    if (true == dump_image_flag_) {
        dump_images((const char *)(mptr_[readbuffer.index]), size_[readbuffer.index]);
    }
    // notify the kernel that it has been used
    ret = ioctl(fd_, VIDIOC_QBUF, &readbuffer);
    if (ret < 0) {
        printf("put back to queue failed!\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    return success;
}
void v4l2_camera::stop_stream() {
    int type = CAPTURE_TYPE;
    int ret = ioctl(fd_, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        printf("close stream failed!\n");
        printf("error:%s\n", strerror(errno));
    }
    else {
        printf("close stream success!\n");
    }
    close(fd_);
    printf("close fd:%d success!\n", fd_);
    fd_ = -1;
	for(int i = 0;i < BUFFER_NUM;i++) {
        munmap(mptr_[i], size_[i]);
        printf("disconnect mmap!\n");
    }
}
void v4l2_camera::free_frame() {
    if (frame != nullptr) {
        free(frame);
        frame = nullptr;
        frame_len = 0;
    }
}
int v4l2_camera::init() {
    if (false == get_camera_dev_fd()) {
        return GET_FD_FAILED;
    }
    printf("get camera dev:%s, fd_:%d\n", dev_name_, fd_);
    show_camera_format();
    if (false == set_camera_format()) {
        return SET_CAMERA_FORMAT_FAILED;
    }
    if (false == malloc_frame()) {
        return MALLOC_FRAME_FAILED;
    }
    if (false == request_buffer()) {
        return REQUEST_BUFFER_FAILED;
    }
    if (bind_mmap() != 0) {
        return BIND_MMAP_FAILED;
    }
    if (false == start_stream()) {
        return START_STREAM_FAILED;
    }
    return 0;
}
v4l2_camera::~v4l2_camera() {
    free_frame();
    stop_stream();
}