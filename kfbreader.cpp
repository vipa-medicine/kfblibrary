#include "kfbreader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <mutex>

using std::default_delete;
using std::make_shared;
using std::min;
using std::max;
using std::shared_ptr;
using std::string;
using std::to_string;

// 全局库实例和互斥锁
static KfbLibrary g_kfb_lib;
static std::mutex g_lib_mutex;

// KfbLibrary 构造函数
KfbLibrary::KfbLibrary()
    : handle(nullptr),
      initialized(false),
      InitImageFile(nullptr),
      GetHeaderInfo(nullptr),
      GetImageStream(nullptr),
      GetImageDataRoi(nullptr),
      UnInitImageFile(nullptr),
      DeleteImageData(nullptr),
      GetThumbnailImage(nullptr),
      GetPreviewImage(nullptr),
      GetLabelImage(nullptr) {}

// AssoImage 构造函数实现
AssoImage::AssoImage() : nBytes(0), width(0), height(0), buf(nullptr) {}

AssoImage::AssoImage(int nBytes, int width, int height, shared_ptr<BYTE> buf)
    : nBytes(nBytes), width(width), height(height), buf(buf) {}

// ImgHandle 构造函数实现
ImgHandle::ImgHandle()
    : imgStruct(new ImageInfoStruct),
      isInitialized(false),
      maxLevel(0),
      scanScale(0),
      width(0),
      height(0),
      assoNames(nullptr) {}

ImgHandle::~ImgHandle() {
    // 只有初始化成功过，才调用反初始化
    if (isInitialized && g_kfb_lib.UnInitImageFile && imgStruct) {
        g_kfb_lib.UnInitImageFile(imgStruct);
    }
    delete imgStruct;
    if (assoNames) delete[] assoNames;
    // 注意：不再 dlclose，由全局 cleanup 管理
}

// 辅助函数：创建带有正确删除器的 shared_ptr
// 优先使用 DeleteImageData，回退到 free()（C 库惯例）
static shared_ptr<BYTE> make_dll_managed_ptr(BYTE* buf) {
    return shared_ptr<BYTE>(buf, [](BYTE* p) {
        if (p) {
            if (g_kfb_lib.DeleteImageData) {
                g_kfb_lib.DeleteImageData(p);
            } else {
                free(p);  // C 库通常使用 malloc，回退到 free
            }
        }
    });
}

// 内部辅助函数：加载关联图像
static void load_associated_images(ImgHandle* s) {
    if (!g_kfb_lib.GetThumbnailImage || !g_kfb_lib.GetPreviewImage || !g_kfb_lib.GetLabelImage) {
        return;
    }

    s->assoNames = new const char*[4]{nullptr};
    BYTE* buf = nullptr;
    int ret[3] = {0, 0, 0};
    int cnt = 0;

    if (g_kfb_lib.GetLabelImage(s->imgStruct, &buf, ret, ret + 1, ret + 2) && buf) {
        s->assoImages["label"] = AssoImage{ret[0], ret[1], ret[2],
            make_dll_managed_ptr(buf)};
        s->assoNames[cnt++] = "label";
    }

    if (g_kfb_lib.GetThumbnailImage(s->imgStruct, &buf, ret, ret + 1, ret + 2) && buf) {
        s->assoImages["thumbnail"] = AssoImage{ret[0], ret[1], ret[2],
            make_dll_managed_ptr(buf)};
        s->assoNames[cnt++] = "thumbnail";
    }

    if (g_kfb_lib.GetPreviewImage(s->imgStruct, &buf, ret, ret + 1, ret + 2) && buf) {
        s->assoImages["macro"] = AssoImage{ret[0], ret[1], ret[2],
            make_dll_managed_ptr(buf)};
        s->assoNames[cnt++] = "macro";
    }
}

// ============== 全局初始化 API ==============

bool kfbslide_init(const char* dllPath) {
    std::lock_guard<std::mutex> lock(g_lib_mutex);

    if (g_kfb_lib.initialized) {
        return true;  // 已初始化，直接返回成功
    }

    void* handle = dlopen(dllPath, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[KFB Error] dlopen failed: %s\n", dlerror());
        return false;
    }

    g_kfb_lib.handle = handle;

    // 解析所有符号
    g_kfb_lib.InitImageFile = (DLLInitImageFileFunc)dlsym(handle, "InitImageFileFunc");
    g_kfb_lib.GetHeaderInfo = (DLLGetHeaderInfoFunc)dlsym(handle, "GetHeaderInfoFunc");
    g_kfb_lib.GetImageStream = (DLLGetImageStreamFunc)dlsym(handle, "GetImageStreamFunc");
    g_kfb_lib.GetImageDataRoi = (DLLGetImageDataRoiFunc)dlsym(handle, "GetImageDataRoiFunc");
    g_kfb_lib.UnInitImageFile = (DLLUnInitImageFileFunc)dlsym(handle, "UnInitImageFileFunc");
    g_kfb_lib.DeleteImageData = (DLLDeleteImageDataFunc)dlsym(handle, "DeleteImageDataFunc");
    g_kfb_lib.GetThumbnailImage = (DLLGetImageFunc)dlsym(handle, "GetThumnailImageFunc");
    g_kfb_lib.GetPreviewImage = (DLLGetImageFunc)dlsym(handle, "GetPriviewInfoFunc");
    g_kfb_lib.GetLabelImage = (DLLGetImageFunc)dlsym(handle, "GetLableInfoFunc");

    // 检查必要符号
    if (!g_kfb_lib.InitImageFile || !g_kfb_lib.GetHeaderInfo || !g_kfb_lib.UnInitImageFile) {
        fprintf(stderr, "[KFB Error] dlsym failed: 缺少必要的符号\n");
        dlclose(handle);
        g_kfb_lib.handle = nullptr;
        return false;
    }

    g_kfb_lib.initialized = true;
    return true;
}

void kfbslide_cleanup() {
    std::lock_guard<std::mutex> lock(g_lib_mutex);

    if (g_kfb_lib.handle) {
        dlclose(g_kfb_lib.handle);
        g_kfb_lib.handle = nullptr;
    }
    g_kfb_lib.initialized = false;

    // 重置所有函数指针
    g_kfb_lib.InitImageFile = nullptr;
    g_kfb_lib.GetHeaderInfo = nullptr;
    g_kfb_lib.GetImageStream = nullptr;
    g_kfb_lib.GetImageDataRoi = nullptr;
    g_kfb_lib.UnInitImageFile = nullptr;
    g_kfb_lib.DeleteImageData = nullptr;
    g_kfb_lib.GetThumbnailImage = nullptr;
    g_kfb_lib.GetPreviewImage = nullptr;
    g_kfb_lib.GetLabelImage = nullptr;
}

bool kfbslide_is_initialized() {
    return g_kfb_lib.initialized;
}

// ============== 文件操作 API ==============

ImgHandle* kfbslide_open(const char* filename) {
    if (!g_kfb_lib.initialized) {
        fprintf(stderr, "[KFB Error] 库未初始化，请先调用 kfbslide_init\n");
        return nullptr;
    }

    ImgHandle* s = new ImgHandle;

    if (!g_kfb_lib.InitImageFile(s->imgStruct, filename)) {
        fprintf(stderr, "[KFB Error] InitImageFile failed: %s\n", filename);
        delete s;
        return nullptr;
    }

    s->isInitialized = true;

    HeaderInfoStruct headerInfo;
    int ret = g_kfb_lib.GetHeaderInfo(s->imgStruct,
                            &headerInfo.Height,
                            &headerInfo.Width,
                            &headerInfo.ScanScale,
                            &headerInfo.SpendTime,
                            &headerInfo.ScanTime,
                            &headerInfo.CapRes,
                            &headerInfo.BlockSize);
    if (!ret) {
        fprintf(stderr, "[KFB Error] GetHeaderInfo failed\n");
        delete s;
        return nullptr;
    }

    s->properties["openslide.mpp-x"] = to_string(headerInfo.CapRes);
    s->properties["openslide.mpp-t"] = to_string(headerInfo.CapRes);
    s->properties["openslide.vendor"] = "Kfbio";
    s->properties["scanScale"] = to_string(headerInfo.ScanScale);

    s->scanScale = headerInfo.ScanScale;
    s->height = headerInfo.Height;
    s->width = headerInfo.Width;
    s->maxLevel = min(6, static_cast<int>(log(max(s->height, s->width)) / log(2)));

    load_associated_images(s);

    return s;
}

ImgHandle* kfbslide_open_with_lib(const char* dllPath, const char* filename) {
    // 向后兼容：自动初始化
    if (!g_kfb_lib.initialized) {
        if (!kfbslide_init(dllPath)) {
            return nullptr;
        }
    }
    return kfbslide_open(filename);
}

void kfbslide_close(ImgHandle* s) {
    if (!s) return;
    delete s;
}

const char* kfbslide_detect_vendor(const char*) {
    return "kfbio";
}

const char* kfbslide_property_value(ImgHandle* preader, const char* attribute_name) {
    if (!preader || !attribute_name) return nullptr;
    auto iter = preader->properties.find(attribute_name);
    if (iter != preader->properties.end()) {
        return iter->second.c_str();
    }
    return nullptr;
}

const char** kfbslide_property_names(ImgHandle* preader) {
    static const char* names[] = {
        "openslide.mpp-x", "openslide.mpp-t", "openslide.vendor", "scanScale", nullptr
    };
    return names;
}

double kfbslide_get_level_downsample(ImgHandle* s, int level) {
    if (!s || level < 0 || level >= s->maxLevel) return 0.0;
    return static_cast<double>(1LL << level);
}

int kfbslide_get_best_level_for_downsample(ImgHandle* s, double downsample) {
    if (!s || downsample < 1) return 0;
    for (int i = 0; i < s->maxLevel; i++) {
        if ((1LL << (i + 1)) > downsample) return i;
    }
    return s->maxLevel - 1;
}

int kfbslide_get_level_count(ImgHandle* s) {
    return s ? s->maxLevel : 0;
}

ll kfbslide_get_level_dimensions(ImgHandle* s, int level, ll* width, ll* height) {
    if (!s || level < 0 || level >= s->maxLevel || !width || !height) {
        return 0;
    }
    *width = s->width >> level;
    *height = s->height >> level;
    return *height;
}

ll kfbslide_get_level0_dimensions(ImgHandle* s, ll* width, ll* height) {
    return kfbslide_get_level_dimensions(s, 0, width, height);
}

BYTE* kfbslide_read_associated_image(ImgHandle* s, const char* name) {
    if (!s || !name) return nullptr;
    auto iter = s->assoImages.find(name);
    if (iter != s->assoImages.end() && iter->second.buf) {
        // 使用 malloc 分配，与 kfbslide_buffer_free 的 free() 配对
        BYTE* buf = static_cast<BYTE*>(malloc(iter->second.nBytes));
        if (!buf) return nullptr;
        memcpy(buf, iter->second.buf.get(), iter->second.nBytes);
        return buf;
    }
    return nullptr;
}

void kfbslide_get_associated_image_dimensions(
    ImgHandle* s, const char* name, ll* width, ll* height, ll* nBytes) {
    if (!width || !height || !nBytes) return;
    *width = *height = *nBytes = 0;

    if (!s || !name) return;
    auto iter = s->assoImages.find(name);
    if (iter != s->assoImages.end()) {
        *width = iter->second.width;
        *height = iter->second.height;
        *nBytes = iter->second.nBytes;
    }
}

const char** kfbslide_get_associated_image_names(ImgHandle* s) {
    if (!s) return nullptr;
    return s->assoNames;
}

bool kfbslide_read_region(
    ImgHandle* s, int level, int x, int y, int* nBytes, BYTE** buf) {
    if (!s || !nBytes || !buf) return false;
    if (level < 0 || level >= s->maxLevel) return false;
    if (!g_kfb_lib.GetImageStream) return false;

    // x, y 是 Level 0 坐标，需要转换为目标 level 坐标
    double downsample = kfbslide_get_level_downsample(s, level);
    int level_x = static_cast<int>(x / downsample);
    int level_y = static_cast<int>(y / downsample);

    float fScale = s->scanScale / downsample;
    g_kfb_lib.GetImageStream(s->imgStruct, fScale, level_x, level_y, nBytes, buf);
    return *nBytes > 0;
}

bool kfbslide_get_image_roi_stream(
    ImgHandle* s, int level, int x, int y, int width, int height,
    int* nBytes, BYTE** buf) {
    if (!s || !nBytes || !buf) return false;
    if (level < 0 || level >= s->maxLevel) return false;
    if (!g_kfb_lib.GetImageDataRoi) return false;

    // x, y 是 Level 0 坐标，需要转换为目标 level 坐标
    // 遵循 OpenSlide 标准：调用者传入 Level 0 坐标
    double downsample = kfbslide_get_level_downsample(s, level);
    int level_x = static_cast<int>(x / downsample);
    int level_y = static_cast<int>(y / downsample);

    float fScale = s->scanScale / downsample;

    return g_kfb_lib.GetImageDataRoi(
        s->imgStruct, fScale, level_x, level_y, width, height, buf, nBytes, true);
}

bool kfbslide_buffer_free(BYTE* buf) {
    if (!buf) return false;
    // 用于本库分配的 buffer（如 read_associated_image）
    // 使用 free() 因为分配时用的是 malloc
    free(buf);
    return true;
}

bool kfbslide_region_buffer_free(BYTE* buf) {
    if (!buf) return false;
    // 用于厂商 DLL 分配的 buffer（read_region/get_image_roi_stream）
    // 优先使用厂商库的释放函数
    if (g_kfb_lib.DeleteImageData) {
        g_kfb_lib.DeleteImageData(buf);
        return true;
    }
    // C 库通常使用 malloc，回退到 free
    free(buf);
    return true;
}
