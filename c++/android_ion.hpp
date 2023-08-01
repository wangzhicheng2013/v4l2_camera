#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <assert.h>
#include <map>
#include "ion.h"
#define ION_FLAG_CACHED_NEEDS_SYNC 2
typedef int (*ion_open_type)();
typedef int (*ion_close_type)(int);
typedef int (*ion_alloc_fd_type)(int, size_t, size_t, unsigned int, unsigned int, int*);
typedef int (*ion_query_heap_cnt_type)(int, int*);
typedef int (*ion_query_get_heaps_type)(int, int, struct ion_heap_data*);

enum android_ion_error_code {
    ION_LIB_OPEN_FAILED = 1,
    ION_OPEN_LOAD_FAILED,
    ION_CLOSE_LOAD_FAILED,
    ION_ALLOC_FD_FAILED,
    ION_QUERY_HEAP_CNT_FAILED,
    ION_QUERY_GET_HEAPS_FAILED,
    ION_DEVICE_OPEN_FAILED,
    ION_GET_HEAP_COUNT_FAILED,
    ION_GET_HEAP_INFO_FAILED,
    ION_GET_SYSTEM_HEAP_FAILED,
    ION_MMAP_FAILED,
    ION_ADDRESS_NULL,
};
struct memory_info {
    memory_info(size_t sz, void* ptr) : size(sz), address(ptr) {
    }
    size_t size = 0;
    void *address = nullptr;
};
class android_ion {
private:
    const char* ion_lib_path_ = "/system/lib64/libion.so";
private:
    ion_open_type ion_open_;
    ion_close_type ion_close_;
    ion_alloc_fd_type ion_alloc_fd_;
    ion_query_heap_cnt_type ion_query_heap_cnt_;
    ion_query_get_heaps_type ion_query_get_heaps_;

    void *handle_ = nullptr;
    int ion_fd_ = -1;
    unsigned int heap_id_mask_ = 0;
    int heap_cnt_ = 0;
    std::map<int, memory_info>memory_info_map_;   // key -- memory fd
private:
    android_ion() = default;
    virtual ~android_ion() {
        close_ion();
        close_lib();
        for (auto& e : memory_info_map_) {
            free_memory(e.first);
        }
    }
    inline bool open_lib() {
        handle_ = dlopen(ion_lib_path_, RTLD_LAZY | RTLD_GLOBAL);
        return handle_ != nullptr;
    }
    inline void close_lib() {
        if (handle_ != nullptr) {
            dlclose(handle_);
            handle_ = nullptr;
        }
    }
    inline void close_ion() {
        if (ion_fd_ != -1) {
            ion_close_(ion_fd_);
            ion_fd_ = -1;
        }
    }
    int load_funs() {
        char *error = nullptr;
        ion_open_ = (ion_open_type)dlsym(handle_, "ion_open");
        if ((error = dlerror()) != nullptr) {
            printf("load ion_open failed!\n");
            return ION_OPEN_LOAD_FAILED;
        }
        ion_close_ = (ion_close_type)dlsym(handle_, "ion_close");
        if ((error = dlerror()) != nullptr) {
            printf("load ion_close failed!\n");
            return ION_CLOSE_LOAD_FAILED;
        }
        ion_alloc_fd_ = (ion_alloc_fd_type)dlsym(handle_, "ion_alloc_fd");
        if ((error = dlerror()) != nullptr) {
            printf("load ion_alloc_fd failed!\n");
            return ION_ALLOC_FD_FAILED;
        }
        ion_query_heap_cnt_ = (ion_query_heap_cnt_type)dlsym(handle_, "ion_query_heap_cnt");
        if ((error = dlerror()) != nullptr) {
            printf("load ion_query_heap_cnt failed!\n");
            return ION_QUERY_HEAP_CNT_FAILED;
        }
        ion_query_get_heaps_ = (ion_query_get_heaps_type)dlsym(handle_, "ion_query_get_heaps");
        if ((error = dlerror()) != nullptr) {
            printf("load ion_query_get_heaps failed!\n");
            return ION_QUERY_GET_HEAPS_FAILED;
        }
        return 0;
    }
    bool open_ion() {
        ion_fd_ = ion_open_();
        if (ion_fd_ < 0) {
            printf("failed to open ION device!\n");
            return false;
        }
        return true;
    }
    int get_heap_id_mask() {
        int ret = ion_query_heap_cnt_(ion_fd_, &heap_cnt_);
        if (ret < 0) {
            printf("failed to get heap count!\n");
            return ION_GET_HEAP_COUNT_FAILED;
        }
        struct ion_heap_data *heaps = (struct ion_heap_data *)calloc(sizeof(struct ion_heap_data), heap_cnt_);
        assert(heaps != nullptr);
        ret = ion_query_get_heaps_(ion_fd_, heap_cnt_, heaps);
        if (ret < 0) {
            free(heaps);
            printf("failed to get heaps!\n");
            return ION_GET_HEAP_INFO_FAILED;
        }
        int i = 0;
        for (;i < heap_cnt_;i++) {
            if (heaps[i].type != ION_HEAP_TYPE_SYSTEM) {
                continue;
            }
            heap_id_mask_ |= (1 << heaps[i].heap_id);
            break;
        }
        free(heaps);
        if (i >= heap_cnt_) {
            return ION_GET_SYSTEM_HEAP_FAILED;
        }
        return 0;
    }
public:
    static android_ion& get_instance() {
        static android_ion instance;
        return instance;
    }
    void set_lib_path(const char* path) {
        ion_lib_path_ = path;
    }
    inline int get_heap_count() const {
        return heap_cnt_;
    }
    int init() {
        if (false == open_lib()) {
            return ION_OPEN_LOAD_FAILED;
        }
        int error_code = load_funs();
        if (error_code) {
            return error_code;
        }
        if (false == open_ion()) {
            return ION_DEVICE_OPEN_FAILED;
        }
        return get_heap_id_mask();
    }
    int alloc_memory(size_t mem_size, int *mem_fd, void **mem_virtual_addr) {
        if ((nullptr == mem_fd) || (nullptr == mem_virtual_addr)) {
            return ION_ADDRESS_NULL;
        }
        auto it = memory_info_map_.find(*mem_fd);
        if (it != memory_info_map_.end()) {
            *mem_virtual_addr = it->second.address;
            return 0;
        }
        int ret = ion_alloc_fd_(ion_fd_, mem_size, 0, heap_id_mask_, ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC, mem_fd);
        if (ret != 0) {
            return ION_ALLOC_FD_FAILED;
        }
        void *ptr = mmap(nullptr, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, *mem_fd, 0);
        if (MAP_FAILED == ptr) {
            return ION_MMAP_FAILED;
        }
        *mem_virtual_addr = ptr;
        memory_info_map_.emplace(*mem_fd, memory_info(mem_size, ptr));
        return 0;
    }
    void free_memory(int mem_fd) {
        auto it = memory_info_map_.find(mem_fd);
        if (memory_info_map_.end() == it) {
            return;
        }
        memory_info& info = it->second;
        if (info.address != nullptr) {
            munmap(info.address, info.size);
            info.address = nullptr;
            info.size = 0;
            printf("memery fd:%d munmap!\n", it->first);
        }
        close(mem_fd);
    }
};
#define ANDROID_ION android_ion::get_instance()


