# asciiplay

`asciiplay` 是一个使用 C++20 和 CMake 构建的跨平台字符画播放器/导出器。它使用 FFmpeg 进行音视频解码与编码，使用 miniaudio 进行跨平台 PCM 播放。

## 功能概览

- 支持灰度、ANSI 256 色、TrueColor 三档字符画模式
- 支持 Bayer 有序抖动、半块字符模式、终端吞吐优化
- 音频播放使用 miniaudio，以音频时钟驱动视频同步
- 三阶段流水线（解码 → 映射 → 渲染/写出）多线程处理
- 支持导出字符画 MP4，内置 8x16 等宽字体
- 支持实时 CLI 调整、统计输出

## 依赖

### Linux

- C++20 编译器（GCC 11+ 或 Clang 13+）
- CMake 3.16+
- FFmpeg 开发包（包含 libavformat, libavcodec, libavutil, libswscale, libswresample，建议使用 FFmpeg 6+，已验证于 FFmpeg 8）
- pkg-config

示例安装命令：

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install build-essential cmake pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev

# Fedora / RHEL
sudo dnf install @development-tools cmake pkgconf-pkg-config ffmpeg-libs ffmpeg-devel
```

### Windows

- Visual Studio 2022 (MSVC)
- CMake 3.16+
- [vcpkg](https://github.com/microsoft/vcpkg)

通过 vcpkg 安装 FFmpeg：

```powershell
cd <vcpkg-root>
./vcpkg install ffmpeg[core,swresample,swscale]
```

配置 CMake 时启用 vcpkg 工具链：

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

## 构建

### Linux 示例

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Windows (PowerShell)

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

生成的可执行文件位于 `build/asciiplay` 或 `build/Release/asciiplay.exe`。

## 使用示例

终端播放：

```bash
asciiplay input.mp4 --mode 256 --grid 200x60 --halfblock on --dither bayer4 --stats
```

静音灰度稳帧：

```bash
asciiplay input.mp4 --mode gray --grid 180x90 --halfblock on --no-audio
```

真彩高画质：

```bash
asciiplay input.mp4 --mode truecolor --grid 220x110 --halfblock on
```

导出字符画视频：

```bash
asciiplay input.mp4 --export out.mp4 --export-grid 240x120 --export-font 8x16 --mode 256 --dither bayer4 --export-crf 18
```

## 性能建议

- 200×60 半块 + ANSI 256 色通常可在现代终端保持 60 FPS
- TrueColor 模式对终端吞吐要求更高，建议适当降低网格或使用 `--maxwrite` 调整
- 导出模式下推荐使用 SSD 以避免编码瓶颈

## 运行提示

- Linux 环境启动时会提示“请全屏终端/最大化”
- Windows 下自动尝试启用虚拟终端序列并最大化窗口
- 运行时支持快捷键：`Space` 暂停/继续，`q` 退出，`c`/`d` 切换模式/抖动，`g/G`、`b/B` 调整 gamma/对比度，`1/2/3` 快速切换档位，`r` 重新适配终端

## 许可证

- 项目代码默认 MIT（如需更改请在此处注明）
- `miniaudio.h` 依据其自带的单头文件许可证发布
