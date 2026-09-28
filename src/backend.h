// backend.h - the generator binaries this GUI can drive
#pragma once

#include <string>

// Both generators were written by the same author and share one command-line
// shape, so the window stays a single form with a few per-backend controls.
// The backend decides which binary is launched, which model folder is offered
// and which extra image inputs the form shows.
enum class Backend {
    Qwen = 0,     // qwenimage-ncnn-vulkan - text-to-image and image editing
    ZImage = 1,   // zimage-ncnn-vulkan   - LanPaint inpaint/outpaint + ControlNet
};

struct BackendSpec {
    Backend id;
    const char *exe_name;   // looked up next to this executable
    const char *model_dir;  // default model folder name under the app dir
    const char *label;      // switcher entry; a product name, never translated
    int  max_inputs;        // -i accepts at most this many images
    bool edit_tools;        // offers -k / -x / -c / -t
};

const BackendSpec &backend_spec(Backend b);

// Which generators are present in `app_dir`. With only one installed the
// window pins itself to it and hides the switcher; with both installed the
// user picks. Neither present leaves the choice to the config file.
struct BackendAvailability {
    bool qwen = false;
    bool zimage = false;
    bool both() const { return qwen && zimage; }
    bool any() const { return qwen || zimage; }
};

BackendAvailability detect_backends(const std::string &app_dir);

// Which engine to open on. A binary that is actually installed outranks the
// stored choice: if only one generator is present, that is the one the window
// has to talk to - a config written back when both were installed must not pin
// the window to a program that is no longer there.
Backend pick_start_backend(const BackendAvailability &avail, const std::string &stored);

// Model folder to hand to -m: the first candidate that exists under app_dir,
// otherwise the conventional one. The conventional path is returned even when
// it is missing, so a failure names the location the program expects.
// `model_name` overrides the backend's default folder name - the Z-Image engine
// ships two models to choose from.
std::string resolve_model_path(const std::string &app_dir, Backend b,
                               const char *model_name = nullptr);
