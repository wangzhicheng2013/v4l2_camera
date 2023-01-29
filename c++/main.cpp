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
const char *DEVNAME = "/dev/video1";
const int DEV_LEN = 13;
const char *DEVNAMES[DEV_LEN] = {
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
const int CAPTURE_TYPE = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
const int MEMORY_TYPE = V4L2_MEMORY_MMAP;
int camera_width = 0;
int camera_height = 0;
int camera_pixelformat = 0;

const int BUFFER_NUM = 4;
unsigned char *mptr[BUFFER_NUM] = { 0 };     // protect the first address of user space after mapping
unsigned int size[BUFFER_NUM] = { 0 };

const char *FILE_PATH = "/data/";
bool dump_image_flag = true;
int dump_image_index = 0;
int DUMP_IMAGE_MAX = 10;

bool exit_flag = false;
void set_exit_flag(int signo) {
    exit_flag = true;
}
int get_camera_dev_fd() {
    int fd = -1;
    for (int i = 0;i < DEV_LEN;i++) {
        // 1. open device
        fd = open(DEVNAMES[i], O_RDWR, 0); 
        if (fd < 0) {
            printf("%s open fail!, error:%s\n", DEVNAMES[i], strerror(errno));
            continue;
        }
        printf("%s open success!\n", DEVNAMES[i]);
        // 2. query device capability： VIDIOC_QUERYCAP
        struct v4l2_capability cap;
        int ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
        if (ret < 0) {
            printf("%s ioctl fail, error:%s\n", DEVNAMES[i], strerror(errno));
            close(fd);
            continue;
        }
        printf("%s ioctl success!\n", DEVNAMES[i]);
        printf("Driver Name:%s\nCard Name:%s\nBus info:%s\nDriver Version:%u.%u.%u\n"
        ,cap.driver, cap.card, cap.bus_info, (cap.version >> 16) & 0XFF, (cap.version >> 8) & 0XFF, cap.version & 0XFF);
        DEVNAME = DEVNAMES[i];
        return fd;
    }  
    return -1; 
}
void show_camera_format(int fd) {
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_frmsizeenum frmsize;
    fmtdesc.index = 0; 
    fmtdesc.type = CAPTURE_TYPE;  // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE is decoded data
    printf("fd:%d, dev:%s Support format:\n", fd, DEVNAME);
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
        printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        frmsize.index = fmtdesc.index;
        frmsize.pixel_format = fmtdesc.pixelformat;
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
            printf("camera width = %d, height = %d, pixel format:%d\n", frmsize.discrete.width, frmsize.discrete.height, frmsize.pixel_format);
            camera_width = frmsize.discrete.width;
            camera_height = frmsize.discrete.height;
            camera_pixelformat = fmtdesc.pixelformat;
        }
        else {
            printf("get failed for index:%d\n", frmsize.index);
        }
        fmtdesc.index++;
    }
    if (0 == fmtdesc.index) {
        printf("fd:%d ioctl failed!\n", fd);
    }
}
bool set_camera_format(int fd) {
    struct v4l2_format vfmt; 
    vfmt.type = CAPTURE_TYPE;          // set type camera acquisition
    vfmt.fmt.pix.pixelformat = camera_pixelformat;          
    vfmt.fmt.pix.width = camera_width;
    vfmt.fmt.pix.height = camera_height;
    vfmt.fmt.pix.field = V4L2_FIELD_ANY;  // must set
    int ret = ioctl(fd, VIDIOC_S_FMT, &vfmt);
    if (ret < 0) {
        printf("---------set camera format failed!--------\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    printf("current camera format information:\n\twidth:%d\n\theight:%d\n\tformat:%d\n", vfmt.fmt.pix.width, vfmt.fmt.pix.height, vfmt.fmt.pix.pixelformat);
    return true;
}

bool request_buffer(int fd) {
    struct v4l2_requestbuffers req = { 0 };
    req.count = BUFFER_NUM;
    req.type = CAPTURE_TYPE;
    req.memory = MEMORY_TYPE;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        printf("-----VIDIOC_REQBUFS-fail!------\n");
        return false;
    }
    printf("-----VIDIOC_REQBUFS-success!------\n");
    return true;
}
int bind_mmap(int fd) {
    struct v4l2_buffer mapbuffer;
    for (unsigned int n_buffers = 0;n_buffers < BUFFER_NUM;++n_buffers) {
        struct v4l2_plane plane[VIDEO_MAX_PLANES] = { 0 };
        memset(&mapbuffer, 0, sizeof(mapbuffer));
        mapbuffer.type = CAPTURE_TYPE;
        mapbuffer.memory = MEMORY_TYPE;
        mapbuffer.index = n_buffers;
        mapbuffer.length = 1;
        mapbuffer.m.planes = plane;     // must set             
        // The query sequence number is n_ Buffer, get its starting physical address and size
        if (ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            printf("----VIDIOC_QUERYBUF-fail!------\n");
            printf("error:%s\n", strerror(errno));
            return -1;
        }
        printf("----VIDIOC_QUERYBUF-success!------\n");
        size[n_buffers] = mapbuffer.m.planes[0].length;
        // mapped memory
        mptr[n_buffers] = (unsigned char *)mmap(NULL, size[n_buffers], PROT_READ | PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.planes[0].m.mem_offset);
        if (nullptr == mptr[n_buffers]) {
            printf("----MMAP-fail!------\n");
            return -2;
        }
        // VIDIOC_QBUF  // put the frame in the queue
		// VIDIOC_DQBUF // remove frame from queue
        if (ioctl (fd, VIDIOC_QBUF, &mapbuffer) < 0) {
             printf("----------frame put into queue failed------------\n");
             printf("error:%s\n", strerror(errno));
             return -3;
        }
        printf("----------frame put into queue success!------------\n");
    }
    return 0;
}
bool start_stream(int fd) {
    int type = CAPTURE_TYPE;
    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0) {
        printf("-----------start stream failed!-------\n");
        printf("error:%s\n", strerror(errno));
        return false;
    }
    printf("-----------start stream success!-------\n");
    return true;
}
void dump_images(const char *ptr, size_t len) {
    if (dump_image_index >= DUMP_IMAGE_MAX) {
        return;
    }
    char file_path[128] = { 0 };
    snprintf(file_path, sizeof(file_path), "%s%d_%dx%d.uyvy", FILE_PATH, dump_image_index++, camera_width, camera_height);
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
void get_frame(int fd) {
    struct v4l2_buffer readbuffer = { 0 };
    while (false == exit_flag) {
        struct v4l2_plane plane = {};
        memset(&readbuffer, 0, sizeof(readbuffer));
        readbuffer.type = CAPTURE_TYPE;
        readbuffer.memory = MEMORY_TYPE;
        readbuffer.m.planes = &plane;     // must set
        readbuffer.length = 1;            // must set
        printf("--------------1-----------\n");
        int ret = ioctl(fd, VIDIOC_DQBUF, &readbuffer);    // fetch a buffer frame from the buffer
        if (ret < 0) {
            printf("get frame failed!\n");
            printf("error:%s\n", strerror(errno));
            continue;
        }
        printf("--------------2-----------\n");
        printf("frame index:%d\n", readbuffer.index);
        if (readbuffer.index >= 0 && readbuffer.index < BUFFER_NUM) {
            printf("get frame addr:%p,frame len:%d\n", mptr[readbuffer.index], size[readbuffer.index]);
        }
        dump_images((const char *)(mptr[readbuffer.index]), size[readbuffer.index]);
        // notify the kernel that it has been used
        ret = ioctl(fd, VIDIOC_QBUF, &readbuffer);
        if (ret < 0) {
            printf("put back to queue failed!\n");
            printf("error:%s\n", strerror(errno));
            return;
        }
    }
}
void stop_stream(int fd) {
    int type = CAPTURE_TYPE;
    int ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        printf("close stream failed!\n");
        printf("error:%s\n", strerror(errno));
    }
    else {
        printf("close stream success!\n");
    }
	for(int i = 0;i < BUFFER_NUM;i++) {
        munmap(mptr[i], size[i]);
        printf("disconnect mmap!\n");
    }
}
int main() {
    signal(SIGTERM, set_exit_flag);
    signal(SIGINT, set_exit_flag);
    int value = 0;
    printf("input max dump images count:(default value is 10, max value is 100)\n");
    scanf("%d", &value);
    if (value > 0 && value <= 100) {
        DUMP_IMAGE_MAX = value;
        printf("dump images count is:%d\n", DUMP_IMAGE_MAX);
    }
    else {
        printf("input error!\n");
    }
    printf("all dump uyvy images will be stored in /data/\n");
    int fd = get_camera_dev_fd();
    if (fd < 0) {
        return -1;
    }
    printf("get camera dev:%s, fd:%d\n", DEVNAME, fd);
    show_camera_format(fd);
    if (false == set_camera_format(fd)) {
        close(fd);
        return -2;
    }
    if (false == request_buffer(fd)) {
        close(fd);
        return -3;
    }
    if (bind_mmap(fd) != 0) {
        close(fd);
        return -4;
    }
    if (false == start_stream(fd)) {
        close(fd);
        return -5;
    }
    get_frame(fd);
    stop_stream(fd);
    close(fd);

    return 0;
}