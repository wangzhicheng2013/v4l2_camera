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

int get_camera_dev_fd() {
    int fd = -1;
    for (int i = 0;i < DEV_LEN;i++) {
        // 1. 打开设备
        fd = open(DEVNAMES[i], O_RDWR, 0); 
        if (fd < 0) {
            printf("%s open fail!, error:%s\n", DEVNAMES[i], strerror(errno));
            continue;
        }
        printf("%s open success!\n", DEVNAMES[i]);
        // 2.查询设备属性： VIDIOC_QUERYCAP
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
    fmtdesc.type = CAPTURE_TYPE;  // V4L2_ BUF_ TYPE_ VIDEO_ CAPTURE_ MPLANE is decoded data
    printf("fd:%d, Support format:\n", fd);
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
    //req.memory = MEMORY_TYPE;
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
        mapbuffer.m.planes = plane;              
        // The query sequence number is n_ Buffer, get its starting physical address and size
        if (ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            printf("----VIDIOC_QUERYBUF-fail!------\n");
            printf("error:%s\n", strerror(errno));
            return -1;
        }
        printf("----VIDIOC_QUERYBUF-success!------\n");
        size[n_buffers] = mapbuffer.m.planes[0].length;
        // mapped memory
        mptr[n_buffers] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.planes[0].m.mem_offset);
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


            /*appbuff.buffer.index = i;
        appbuff.buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        appbuff.buffer.memory = V4L2_MEMORY_USERPTR;
        appbuff.buffer.length = 1;
        appbuff.buffer.m.planes = plane;

        if (ioctl(mFd, VIDIOC_QUERYBUF, &appbuff.buffer) < 0) {
            ALOGE("abc VIDIOC_QUERYBUF: %s, errno:%d", strerror(errno), errno);
            if(mFd >= 0) {
                close(mFd);
                mFd = -1;
            }
            ret = ERROR_INIT_FAIL;
            goto ERROR_OUT;
        }*/
}
int main() {
    int fd = get_camera_dev_fd();
    if (fd < 0) {
        return -1;
    }
    printf("get camera dev:%s, fd:%d\n", DEVNAME, fd);
    show_camera_format(fd);
    set_camera_format(fd);
    
    if (false == request_buffer(fd)) {
        close(fd);
        return -2;
    }
    if (bind_mmap(fd) != 0) {
        close(fd);
        return -3;
    }
    close(fd);

    return 0;
}