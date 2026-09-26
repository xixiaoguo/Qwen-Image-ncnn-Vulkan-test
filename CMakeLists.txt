cmake_minimum_required(VERSION 3.10)
project(Image-ncnn-Vulkan-UI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# FindOpenGL (reached through FindFLTK) otherwise picks the legacy libGL "for
# compatibility with CMake 3.10 and below" and prints a developer warning. GLVND
# is what a modern desktop actually uses, so ask for it explicitly. The guard
# keeps older CMake versions that do not know the policy happy.
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

# FLTK 1.4 is the first release with a Wayland backend, and this program runs
# on both X11 and Wayland. The version bound makes an older install fail here
# with a clear message instead of somewhere deep in the compile.
find_package(FLTK 1.4 REQUIRED)
find_package(Threads REQUIRED)

add_executable(Image-ncnn-Vulkan-UI
    src/main.cpp
    src/i18n.cpp
    src/ui_main.cpp
    src/runner.cpp
    src/image_info.cpp
    src/backend.cpp
    src/gallery.cpp
    src/image_viewer.cpp
    src/settings.cpp
    src/picker.cpp
    src/presets.cpp
)

target_include_directories(Image-ncnn-Vulkan-UI PRIVATE src)
target_link_libraries(Image-ncnn-Vulkan-UI PRIVATE ${FLTK_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PNG QUIET libpng)
        if(PNG_FOUND)
            target_include_directories(Image-ncnn-Vulkan-UI PRIVATE ${PNG_INCLUDE_DIRS})
            target_link_libraries(Image-ncnn-Vulkan-UI PRIVATE ${PNG_LIBRARIES})
            target_compile_definitions(Image-ncnn-Vulkan-UI PRIVATE HAVE_LIBPNG=1)
        endif()

        # FLTK 1.4's own headers pull in <cairo.h>, but its cmake package does
        # not pass cairo's include directory down to us. Against a system FLTK
        # that shows up as "cairo.h: No such file or directory", so ask
        # pkg-config for the path.
        pkg_check_modules(CAIRO QUIET cairo)
        if(CAIRO_FOUND)
            target_include_directories(Image-ncnn-Vulkan-UI PRIVATE ${CAIRO_INCLUDE_DIRS})
        endif()
    endif()
endif()

if(FLTK_INCLUDE_DIRS)
    target_include_directories(Image-ncnn-Vulkan-UI PRIVATE ${FLTK_INCLUDE_DIRS})
endif()
