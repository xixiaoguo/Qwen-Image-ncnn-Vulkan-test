// runner.h - build a command line and run the generator asynchronously
#pragma once

#include <string>
#include <vector>

#include "backend.h"

// All user-facing options of both generators. The shared options mean the same
// thing in each; the ones marked with a backend are only passed to that one.
struct GenOptions {
    Backend backend = Backend::Qwen;

    std::string prompt;
    std::string negative_prompt;
    double cfg_scale = 1.0;      // -w, qwen: true CFG scale
    double control_scale = 1.0;  // -w, zimage: ControlNet scale
    std::string output_path = "out.png"; // -o
    std::vector<std::string> inputs;      // -i
    std::string mask_path;        // -k, zimage: white repaints, black is kept
    std::string control_path;     // -c, zimage: ControlNet control image
    std::string outpaint;         // -x, zimage: "left,top,right,bottom"
    bool tile_upscale = false;    // -t, zimage: tile ControlNet upscale
    int width = 1024;             // -s
    int height = 1024;
    int steps = 40;               // -l (0 = leave the choice to the generator)
    long long seed = 42;          // -r
    std::string model_path = "models/qwenimage21"; // -m
    int gpu_id = 2147483647;      // -g, INT_MAX means "auto"
    int batch = 1;                // -b
};

// Build argv for the binary. argv[0] will be the executable path.
std::vector<std::string> build_argv(const std::string &exe, const GenOptions &opt);

// Pretty-print the command for display in the UI (shell-quoted)
std::string build_command_string(const std::string &exe, const GenOptions &opt);

// Result of a single run
struct RunResult {
    bool finished = false;   // process exited
    int exit_code = 0;
    std::string error;       // non-empty if we could not even start
    bool launch_failed = false;
};

// Run synchronously (call from a worker thread). Output lines are delivered
// through the callback. Return value describes how the process ended.
RunResult run_process(const std::vector<std::string> &argv,
                      const std::string &workdir,
                      void (*on_line)(const std::string &line, void *user),
                      void *user);

// Query available Vulkan GPUs with `-g` style probing is not supported by the
// binaries; we just offer auto / cpu / 0..3 in the UI.
