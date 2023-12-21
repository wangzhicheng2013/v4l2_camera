#include "v4l2_camera_mmap.hpp"
int main() {
    std::unique_ptr<v4l2_camera>v4l2_ptr(new v4l2_camera_mmap);
    int ret = v4l2_ptr->init();
    if (ret) {
        printf("init mmap:%d failed!\n", ret);
        return 0;
    }
    int frame_count = 10;
    for (int i = 0;i < frame_count;i++) {
        if (true == v4l2_ptr->get_frame()) {
            printf("get frame result success!\n");
        }
    }

    return 0;
}
