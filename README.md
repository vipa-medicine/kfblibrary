# kfblibrary

KFB 格式全切片图像的第三方读取库。

## 使用方法

编译此项目并替换 `libkfbreader.so`：

```bash
g++ -shared -fPIC -O2 -std=c++17 -o libkfbreader.so kfbreader.cpp -ldl
```

API 使用方式请参考 `main.cpp` 或 Python 绑定 `image_processor/src/wsi_processing/kfb_reader/`。

## 为什么重新实现 libkfbslide？

原始 `libkfbslide.so` 存在内存泄漏问题，导致模型训练进程崩溃。因此重新实现了该库，添加了内存释放 API 和自动 buffer 释放机制。

原始库的问题：
1. `kfbslide_open()` 会加载关联图像，影响性能
2. `libImageOperationLib.so` 分配的图像数据 buffer 不会被释放

## 重构改进（2025-01）

### 全局初始化 API

新增全局库初始化机制，避免每次打开文件都重复 `dlsym`：

```c
// 程序启动时初始化一次
kfbslide_init("/path/to/libImageOperationLib.so");

// 打开文件（无需再传 dllPath）
ImgHandle* slide = kfbslide_open("test.kfb");

// 程序退出时清理
kfbslide_cleanup();
```

### 内存管理改进

- 关联图像使用 `shared_ptr` + 自定义删除器（`DeleteImageData` 或 `free()`）
- `kfbslide_read_associated_image` 使用 `malloc` 分配，与 `free()` 配对
- 两个独立的释放函数：
  - `kfbslide_buffer_free(buf)` - 释放本库分配的 buffer（如 `read_associated_image`）
  - `kfbslide_region_buffer_free(buf)` - 释放厂商 DLL 分配的 buffer（如 `read_region`）

### 坐标系统

遵循 OpenSlide 标准：`read_region` 和 `get_image_roi_stream` 的 `x, y` 参数接受 Level 0 坐标，C++ 层负责坐标转换。

### 线程安全

- 全局初始化/清理操作使用 `std::mutex` 保护，防止并发初始化
- `KFBSlide` 实例可在多线程中共享使用（已通过 32 线程 × 200 次迭代测试验证，厂商库本身支持并发读取）

### C 风格导出

使用 `extern "C"` 导出函数，避免 C++ name mangling，简化 Python ctypes 绑定。

## 未实现的功能

以下 `libImageOperationLib.so` 中的函数未实现：

```
// 属性读取
GetGetImageBarCodeFunc
GetImageChannelIdentifierFunc
GetMachineSerialNumFunc
GetScanLevelInfoFunc
GetScanTimeDurationFunc
GetVersionInfoFunc

// 路径读取
GetLableInfoPathFunc
GetPriviewInfoPathFunc
GetThumnailImageOnlyPathFunc
GetThumnailImagePathFunc

// 并发加载
GetLUTImageManyPassageway
GetImageStreamManyPassagewayFunc
GetImageDataRoiManyPassagewayFunc

// 其他
GetLUTImage
GetImageRGBDataStreamFunc
UnCompressBlockDataInfoFunc
```

可使用 IDA Pro 分析并实现这些 API。

## 致谢

部分代码来自 [KFB_Convert_TIFF](https://github.com/babiking/KFB_Convert_TIFF)。如有问题请通过 issue 联系！
