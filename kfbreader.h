#ifndef __KFBREADER__
#define __KFBREADER__

#include <map>
#include <memory>
#include <string>

#include "KFB.h"

using ll = long long int;
using BYTE = unsigned char;

// 全局库状态结构（内部使用，但需要在头文件声明以便 ImgHandle 引用）
struct KfbLibrary {
    void* handle;
    bool initialized;

    // 缓存的函数指针
    DLLInitImageFileFunc InitImageFile;
    DLLGetHeaderInfoFunc GetHeaderInfo;
    DLLGetImageStreamFunc GetImageStream;
    DLLGetImageDataRoiFunc GetImageDataRoi;
    DLLUnInitImageFileFunc UnInitImageFile;
    DLLDeleteImageDataFunc DeleteImageData;
    DLLGetImageFunc GetThumbnailImage;
    DLLGetImageFunc GetPreviewImage;
    DLLGetImageFunc GetLabelImage;

    KfbLibrary();
    ~KfbLibrary() = default;
};

// 附属图像数据结构（缩略图、标签、预览图）
struct AssoImage {
    int nBytes;
    int width;
    int height;
    std::shared_ptr<BYTE> buf;

    AssoImage();
    AssoImage(int nBytes, int width, int height, std::shared_ptr<BYTE> buf);
    ~AssoImage() = default;
};

// KFB 文件句柄，封装厂商库的状态和缓存的函数指针
struct ImgHandle {
    // 厂商库状态
    ImageInfoStruct* imgStruct;
    bool isInitialized;  // 标记 InitImageFile 是否成功

    // 图像元数据
    int maxLevel;
    int scanScale;
    int width;
    int height;

    // 属性和附属图像
    std::map<std::string, std::string> properties;
    std::map<std::string, AssoImage> assoImages;
    const char** assoNames;

    ImgHandle();
    ~ImgHandle();

    // 禁止拷贝
    ImgHandle(const ImgHandle&) = delete;
    ImgHandle& operator=(const ImgHandle&) = delete;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 全局初始化厂商库（只需调用一次）
 *
 * @param dllPath 厂商库路径 (libImageOperationLib.so)
 * @return 成功返回 true，失败返回 false
 */
bool kfbslide_init(const char* dllPath);

/**
 * 全局清理厂商库（程序退出时调用）
 */
void kfbslide_cleanup();

/**
 * 检查厂商库是否已初始化
 */
bool kfbslide_is_initialized();

/**
 * 打开 KFB 文件（需要先调用 kfbslide_init）
 *
 * @param filename KFB 文件路径
 * @return 成功返回 ImgHandle 指针，失败返回 nullptr
 */
ImgHandle* kfbslide_open(const char* filename);

/**
 * 打开 KFB 文件（向后兼容，内部自动调用 init）
 *
 * @param dllPath 厂商库路径 (libImageOperationLib.so)
 * @param filename KFB 文件路径
 * @return 成功返回 ImgHandle 指针，失败返回 nullptr
 */
ImgHandle* kfbslide_open_with_lib(const char* dllPath, const char* filename);

/**
 * 关闭 KFB 文件，释放所有资源
 */
void kfbslide_close(ImgHandle* s);

/**
 * 检测文件格式厂商
 */
const char* kfbslide_detect_vendor(const char*);

/**
 * 获取属性名列表
 */
const char** kfbslide_property_names(ImgHandle* preader);

/**
 * 获取属性值
 */
const char* kfbslide_property_value(ImgHandle* preader, const char* attribute_name);

/**
 * 获取指定层的下采样倍率
 */
double kfbslide_get_level_downsample(ImgHandle* s, int level);

/**
 * 根据下采样倍率获取最佳层级
 */
int kfbslide_get_best_level_for_downsample(ImgHandle* s, double downsample);

/**
 * 获取金字塔层数
 */
int kfbslide_get_level_count(ImgHandle* s);

/**
 * 获取指定层的尺寸
 */
ll kfbslide_get_level_dimensions(ImgHandle* s, int level, ll* width, ll* height);

/**
 * 获取 level 0 的尺寸
 */
ll kfbslide_get_level0_dimensions(ImgHandle* s, ll* width, ll* height);

/**
 * 读取附属图像
 * 注意：调用者必须调用 kfbslide_buffer_free 释放返回的 buffer
 */
BYTE* kfbslide_read_associated_image(ImgHandle* s, const char* name);

/**
 * 获取附属图像尺寸
 */
void kfbslide_get_associated_image_dimensions(
    ImgHandle* s, const char* name, ll* width, ll* height, ll* nBytes);

/**
 * 获取附属图像名称列表
 */
const char** kfbslide_get_associated_image_names(ImgHandle* s);

/**
 * 读取指定位置的图像块
 * 注意：x, y 是 Level 0 坐标（遵循 OpenSlide 标准）
 */
bool kfbslide_read_region(
    ImgHandle* s, int level, int x, int y, int* nBytes, BYTE** buf);

/**
 * 读取指定区域的图像数据（ROI）
 * 注意：x, y 是 Level 0 坐标（遵循 OpenSlide 标准，C++ 层负责坐标转换）
 *       width, height 是目标 level 的像素尺寸
 */
bool kfbslide_get_image_roi_stream(
    ImgHandle* s, int level, int x, int y, int width, int height,
    int* nBytes, BYTE** buf);

/**
 * 释放本库分配的 buffer（如 read_associated_image 返回的）
 * 使用 free() 释放
 */
bool kfbslide_buffer_free(BYTE* buf);

/**
 * 释放厂商 DLL 分配的 buffer（read_region/get_image_roi_stream 返回的）
 * 优先使用 DeleteImageData，回退到 free()
 */
bool kfbslide_region_buffer_free(BYTE* buf);

#ifdef __cplusplus
}
#endif

#endif
