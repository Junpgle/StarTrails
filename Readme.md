
# 🌌 StarTrail Pro（星轨工坊）

一个基于 **C++ / Qt** 和 **OpenCV** 的高性能星空延时堆栈与处理工具。

StarTrail Pro 是专为星空摄影师设计的后期处理软件。它不仅能将延时摄影素材（视频或图片序列）合成为壮观的星轨视频，还**独家支持生成 Google Motion Photo（实况照片）**，让你的星轨作品可以直接以动态照片的形式分享到社交平台。

---

## ✨ 主要功能

### 🌠 彗星模式星轨
- 支持“长尾”效果，通过滑动窗口算法生成类似流星雨的动态拖尾。
- 可自定义拖尾长度与柔和度。

### 📱 实况照片生成（Live Photo）
- **独家算法**支持生成包含嵌入式视频的 **Motion Photo (JPG)**。
- 完美兼容 **Google Photos** 和现代安卓相册，**长按即可播放星轨形成过程**。
- 内置封面选择器，自由指定展示帧。

### 🎞️ 全格式支持
- **视频导入**：MP4、MOV、MKV。
- **RAW 序列导入**：直接处理 DNG、CR2、NEF、ARW、TIFF 等专业格式。
- 自动处理 DNG 多页图像，确保读取全分辨率数据。

### ⚡ 极速渲染
- **OpenCL GPU 加速**：利用显卡进行大规模像素运算。
- **异步多线程**：读取、计算、编码写入并行处理，极大缩短渲染时间。
- **内存优化**：智能内存管理，支持处理 **8K 级高分辨率序列**。

### 🎨 强大的编辑能力
- **可视化裁剪**：支持拖拽框选感兴趣区域，消除地景干扰。
- **预设比例**：一键设置为 9:16（抖音/Reels）、16:9、1:1、4:5 等。
- **时间轴修剪**：实时预览并截取视频片段。
- **自定义 FPS**：调整输出视频帧率，实现快慢动作控制。

---

## 🚀 快速开始

### 依赖环境
- **Qt 6.x**（推荐 6.5+）
- **OpenCV 4.x**（推荐 4.5+，Windows 下需包含 `opencv_world` 库）
- **C++17 编译器**（MSVC 2019+ / GCC / Clang）

### 编译指南（Windows）

1. 克隆仓库：
   ```bash
   git clone https://github.com/Junpgle/StarTrail-Pro.git
   ```

2. 修改 `.pro` 文件中的 OpenCV 路径：
   ```qmake
   INCLUDEPATH += D:/opencv/build/include
   LIBS += -LD:/opencv/build/x64/vc15/lib
   ```

3. 使用 **Qt Creator** 打开项目并构建 **Release** 版本。

> **注意**：运行时需将 `opencv_worldxxxx.dll` 复制到可执行文件同级目录。

---

## 🧩 核心算法说明

### Google Motion Photo 实现
软件实现了 `MotionPhotoMuxer`，遵循 **Google MicroVideo V1 标准**：
- 解析 JPEG 结构，定位 APP1 数据段。
- 注入包含 `GCamera:MicroVideo` 和 `GCamera:MicroVideoOffset` 的 XMP 元数据。
- 将生成的 MP4 数据无损追加到 JPEG 文件尾部。

### DNG 序列处理
针对 DNG/Raw 格式在 OpenCV 中的兼容性问题，软件实现了自定义读取器：
- 使用 `imreadmulti` 遍历 TIFF 容器中的所有子图。
- 自动识别并丢弃缩略图，锁定最大分辨率的 Raw 数据层。
- 自动进行位深映射（16-bit → 8-bit）以适配视频编码器。

---

## 📝 贡献

欢迎提交 **Issue** 和 **Pull Request**！

--- 
