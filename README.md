# qwenimage-gui

[`qwenimage-ncnn-vulkan`](https://github.com/nihui/qwenimage-ncnn-vulkan)（Qwen-Image-2.1 的
ncnn Vulkan 实现）的 FLTK 的简单图形界面。界面支持中英文切换，X11 与 Wayland 均可。

## 运行环境

本程序是普通的 FLTK 桌面程序，对系统没有特殊要求：

- Linux 桌面，X11 或 Wayland 均可
- 已安装 FLTK 1.4
- libpng（可选）：装上后透明 PNG 的预览才正确

它需要配合 `qwenimage-ncnn-vulkan` 使用。把生成程序和模型放在本程序所在目录即可：

```
某目录/
├── qwenimage-gui            <- 本程序
├── qwenimage-ncnn-vulkan    <- 生成程序
└── models/qwenimage21/      <- 模型
```

程序名和模型路径是固定的，都取自本程序所在目录，界面上不提供修改。

## 编译环境

- CMake、C++ 编译器（g++）
- **FLTK 1.4**：必需。1.3 不行——Wayland 后端从 1.4 才开始提供。
- libpng：可选，装上后透明 PNG 的预览才正确。

Kubuntu 26.04 / Ubuntu：

```bash
sudo apt install build-essential cmake libfltk1.4-dev libpng-dev
```

## 编译方法

```bash
./build.sh
```

或者手动：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

产物为 `build/qwenimage-gui`，直接运行即可。

FLTK 装在非默认位置（例如家目录下的自编译版本）时，用 `FLTK_DIR` 指向存放
`FLTKConfig.cmake` 的目录：

```bash
FLTK_DIR=/path/to/fltk/share/fltk ./build.sh
```
