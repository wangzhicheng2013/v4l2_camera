#include "v4l2_camera_mmap.hpp"
#include "v4l2_camera_userptr.hpp"
int main() {
    std::unique_ptr<v4l2_camera>v4l2_ptr(new v4l2_camera_mmap);
    int ret = v4l2_ptr->init();
    if (ret) {
        printf("init mmap:%d failed!\n", ret);
        return 0;
    }
    v4l2_ptr.reset(new v4l2_camera_userptr);
    ret = v4l2_ptr->init();
    if (ret) {
        printf("init userptr:%d failed!\n", ret);
        return 0;
    }
}