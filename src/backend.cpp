// backend.cpp - backend table, detection and model folder lookup
#include "backend.h"

#include <sys/stat.h>
#include <vector>

namespace {

bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The launcher needs an executable file, not just any file with that name: a
// leftover text file or a directory would only fail later, inside fork/exec.
bool is_executable_file(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111);
}

} // namespace

const BackendSpec &backend_spec(Backend b) {
    static const BackendSpec kQwen = {
        Backend::Qwen, "qwenimage-ncnn-vulkan", "qwenimage21",
        "Qwen-Image-2.1", 10, false};
    static const BackendSpec kZImage = {
        Backend::ZImage, "zimage-ncnn-vulkan", "z-image-turbo",
        "Z-Image", 1, true};
    return b == Backend::ZImage ? kZImage : kQwen;
}

BackendAvailability detect_backends(const std::string &app_dir) {
    BackendAvailability a;
    a.qwen = is_executable_file(app_dir + "/" + backend_spec(Backend::Qwen).exe_name);
    a.zimage = is_executable_file(app_dir + "/" + backend_spec(Backend::ZImage).exe_name);
    return a;
}

std::string resolve_model_path(const std::string &app_dir, Backend b,
                               const char *model_name) {
    // The layouts that show up in practice, most likely first:
    //   qwen:   models/qwenimage21                (upstream source tree)
    //           qwenimage21                      (next to the binary)
    //   zimage: models/z-image-ncnn/<model>       (the release package puts
    //           the model and its ControlNet folders under one parent)
    //           <model>                          (next to the binary)
    //           models/<model>
    const std::string dir = (model_name && *model_name)
                                ? std::string(model_name)
                                : std::string(backend_spec(b).model_dir);
    std::vector<std::string> candidates;
    if (b == Backend::Qwen) {
        candidates = {app_dir + "/models/" + dir,
                      app_dir + "/" + dir};
    } else {
        candidates = {app_dir + "/models/z-image-ncnn/" + dir,
                      app_dir + "/" + dir,
                      app_dir + "/models/" + dir};
    }
    for (const std::string &c : candidates)
        if (is_dir(c)) return c;
    return candidates[0];   // report the conventional path when none exists
}
