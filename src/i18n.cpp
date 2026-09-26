// i18n.cpp - translation tables and lookup
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <cstring>

namespace {

struct Entry {
    Str key;
    const char *en;
    const char *zh;
};

// The single source of truth for all user-facing strings.
// Keep the two columns semantically identical; only wording should differ.
const Entry kTable[] = {
    {Str::AppTitle,             "Image-ncnn-Vulkan-UI",             "Image-ncnn-Vulkan-UI"},
    {Str::AppTitleZImage,       "Image-ncnn-Vulkan-UI",             "Image-ncnn-Vulkan-UI"},

    {Str::Language,             "Language:",                          "语言："},
    {Str::Backend,              "Engine:",                            "引擎："},

    {Str::Binary,               "Binary:",                            "程序："},
    {Str::WorkDir,              "Work dir:",                          "工作目录："},
    {Str::Model,                "Model (-m):",                        "模型 (-m)："},
    {Str::Browse,               "…",                                  "…"},

    {Str::GroupGeneration,      "Generation settings",                "生成设置"},

    {Str::PromptRequired,       "Prompt (-p), required",              "提示词 (-p)，必填"},
    {Str::PromptPlain,          "Prompt (-p)",                        "提示词 (-p)"},
    {Str::NegativeOptional,     "Negative prompt (-n), optional",     "负向提示词 (-n)，可选"},

    {Str::HintPrompt,           "Type the prompt you want",            "输入你所需的提示词"},
    {Str::HintNegative,         "what the picture should avoid",       "不希望出现的内容"},
    {Str::HintOutputDir,        "Pick an output folder",               "选择输出路径"},
    {Str::HintOutputName,       "Output file name",                    "输出文件名"},
    {Str::HintOutpaint,         "e.g. 128,128,128,128 (left,top,right,bottom)",
                                "例如：128,128,128,128（左,上,右,下）"},
    {Str::HintRefImages,        "Pick one or more reference images",   "选择单个或多个参考图"},
    {Str::HintZInputInpaint,    "Pick the image to repaint",           "选择需要重绘的图片"},
    {Str::HintZInputOutpaint,   "Pick the image to expand",            "选择要扩展的图片"},
    {Str::HintMask,             "Pick the mask; white gets repainted", "选择蒙版图，白色为重绘区域"},
    {Str::HintControl,          "Pick a control image (pose / edge / gray)",
                                "选择控制图（姿态 / 边缘 / 灰度）"},
    {Str::HintLowRes,           "Pick the image to upscale",           "选择需要放大的图片"},
    {Str::HintControlSize,      "The control image must be the same size as the output (-s).",
                                "控制图尺寸必须与输出尺寸（-s）一致。"},
    {Str::HintInpaintSize,      "Set Size (-s) to match the input image; both must be multiples of 16.",
                                "尺寸（-s）需要与输入图一致，且两者都必须是 16 的倍数。"},
    {Str::HintOutpaintSize,     "Set Size (-s) to the input image plus the margins.",
                                "尺寸（-s）需要等于输入图加上扩图边距。"},
    {Str::SizeAutoComputed,     "Filled in automatically: input image plus the outpaint margins.",
                                "自动计算：输入图尺寸 + 扩图边距。"},

    {Str::Cfg,                  "CFG (-w):",                          "CFG (-w)："},
    {Str::Output,               "Output (-o):",                       "输出 (-o)："},
    {Str::OutputDir,            "Output folder:",                     "输出目录："},
    {Str::OutputName,           "File name:",                         "文件名："},
    {Str::OutputFormat,         "Format:",                            "格式："},
    {Str::Size,                 "Size (-s):",                         "尺寸 (-s)："},
    {Str::Preset,               "Preset",                             "预设"},
    {Str::Steps,                "Steps (-l):",                        "步数 (-l)："},
    {Str::Seed,                 "Seed (-r):",                         "随机种子 (-r)："},
    {Str::Batch,                "Batch (-b):",                        "批量 (-b)："},
    {Str::RandomSeed,           "random seed each run",               "每次运行使用随机种子"},
    {Str::Gpu,                  "GPU (-g):",                          "GPU (-g)："},

    {Str::RefImages,            "Reference images (-i), up to 10 for image editing",
                                "参考图 (-i)，图像编辑最多 10 张"},
    {Str::RefImagesZImage,      "Input image (-i) for inpaint/outpaint",
                                "输入图 (-i)，用于重绘/扩图"},
    {Str::MaskImage,            "Mask (-k):",                        "蒙版 (-k)："},
    {Str::MaskHint,             "White areas are repainted, black areas are kept.",
                                "白色区域重绘，黑色区域保留。"},
    {Str::ControlImage,         "Control image (-c):",                 "控制图 (-c)："},
    {Str::ControlImageTile,     "Low-res image (-c):",                 "低分辨率图 (-c)："},
    {Str::ControlScale,         "Control scale (-w):",                 "控制强度 (-w)："},
    {Str::TileUpscale,          "Tile upscale (-t)",                  "Tile 放大 (-t)"},
    {Str::Outpaint,             "Outpaint (-x):",                     "扩图 (-x)："},
    {Str::OutpaintHint,         "Expands the canvas by left,top,right,bottom pixels.",
                                "按左,上,右,下扩展画布，单位像素。"},

    {Str::ZModel,               "Model:",                            "模型："},
    {Str::ZMode,                "Mode:",                              "模式："},
    {Str::ZModeText,            "Text to image",                      "文生图"},
    {Str::ZModeInpaint,         "Inpaint (LanPaint)",                 "局部重绘"},
    {Str::ZModeOutpaint,        "Outpaint (LanPaint)",                "扩图"},
    {Str::ZModeControl,         "ControlNet",                         "结构控制"},
    {Str::ZModeTile,            "Tile upscale",                       "超分放大"},
    {Str::ZImageInput,          "Input image (-i):",                  "输入图 (-i)："},
    {Str::ZTileHint,            "The target size is taken from the Size (-s) fields above.",
                                "目标尺寸取上方“尺寸 (-s)”的数值。"},
    {Str::AddImage,             "Add image…",                         "添加图片…"},
    {Str::RemoveSelected,       "Remove selected",                    "移除所选"},

    {Str::PreviewLatest,        "Latest",                             "最新"},
    {Str::About,                "About",                              "关于"},
    {Str::Reload,               "Refresh",                            "刷新"},
    {Str::OpenOutputDir,        "Open folder",                        "打开输出文件夹"},
    {Str::GalleryBack,          "All images",                         "全部图片"},
    {Str::Fit,                  "Fit",                                "适应窗口"},
    {Str::ZoomIn,               "+",                                  "+"},
    {Str::ZoomOut,              "-",                                  "-"},
    {Str::Zoom1to1,             "1:1",                                "1:1"},
    {Str::GalleryEmpty,         "No images in the output folder",     "输出目录中没有图片"},
    {Str::GalleryUnreadable,    "Cannot read folder: ",               "无法读取目录："},
    {Str::LogGalleryOpened,     "[preview] opened ",                  "[预览] 已打开 "},
    {Str::LogGalleryRefreshed,  "[preview] grid refreshed: %d image(s)", "[预览] 宫格已刷新：%d 张图片"},

    {Str::Generate,             "Generate",                           "开始生成"},
    {Str::Stop,                 "Stop",                               "停止"},

    {Str::Ready,                "Ready",                              "就绪"},
    {Str::Running,              "running…",                           "运行中…"},
    {Str::Done,                 "done",                               "完成"},
    {Str::Failed,               "failed",                             "失败"},
    {Str::Stopped,              "stopped by user",                    "已手动结束任务"},
    {Str::ReadyHint,            "Ready. Generator and model come from this application's own directory.",
                                "就绪。生成程序与模型目录都取自本程序所在目录。"},
    {Str::OptionsHint,          "Options follow `qwenimage-ncnn-vulkan -h`: -p -n -w -o -i -s -l -r -m -g -b",
                                "选项对应 `qwenimage-ncnn-vulkan -h`：-p -n -w -o -i -s -l -r -m -g -b"},

    {Str::SelectRefImages,      "Select reference image(s)",          "选择参考图片"},
    {Str::SelectInputImage,     "Select input image",                 "选择输入图"},
    {Str::OutputImage,          "Output image",                       "输出图片"},
    {Str::ModelDir,             "Model directory (contains processor/, text_encoder/, transformer/, vae/)",
                                "模型目录（包含 processor/、text_encoder/、transformer/、vae/）"},
    {Str::ChooseBinary,         "qwenimage-ncnn-vulkan binary",       "qwenimage-ncnn-vulkan 可执行文件"},
    {Str::ChooseWorkDir,        "Working directory",                  "工作目录"},

    {Str::PickerFolder,         "Folder:",                            "文件夹："},
    {Str::PickerFileName,       "File name:",                         "文件名："},
    {Str::PickerUp,             "Parent",                             "上级"},
    {Str::PickerVolume,         "Switch volume",                      "切换分区"},
    {Str::PickerHome,           "Home",                               "主目录"},
    {Str::PickerOk,             "OK",                                 "确定"},
    {Str::PickerChooseDir,      "Choose folder",                     "选择目录"},
    {Str::PickerConfirmSel,     "Confirm selection",                  "确认选择"},
    {Str::PickerCancel,         "Cancel",                             "取消"},
    {Str::ErrNoSelection,       "Nothing selected.",                  "没有选中任何项目。"},

    {Str::MaxRefImages,         "At most 10 reference images.",       "参考图最多 10 张。"},
    {Str::MaxRefImagesZImage,   "Z-Image takes a single input image.", "Z-Image 只支持一张输入图。"},
    {Str::ErrPromptRequired,    "Prompt (-p) is required.",           "提示词 (-p) 不能为空。"},
    {Str::ErrCfg,               "CFG scale (-w) must be a non-negative number.",
                                "CFG 比例 (-w) 必须是非负数字。"},
    {Str::ErrControlScale,      "Control scale (-w) must be a non-negative number.",
                                "控制强度 (-w) 必须是非负数字。"},
    {Str::ErrOutput,            "Output path (-o) is required.",      "输出路径 (-o) 不能为空。"},
    {Str::ErrOutputDir,         "Cannot create output folder: ",      "无法创建输出目录："},
    {Str::ErrModel,             "Model path (-m) is required.",       "模型路径 (-m) 不能为空。"},
    {Str::ErrSizePositive,      "Image size (-s) must be positive.",  "图像尺寸 (-s) 必须为正数。"},
    {Str::ErrSizeMultiple,      "Image size must be a multiple of %d (%s). Got %dx%d.",
                                "图像尺寸必须是 %d 的倍数（%s）。当前为 %dx%d。"},
    {Str::ErrSteps,             "Denoise steps (-l) must be positive.","去噪步数 (-l) 必须为正数。"},
    {Str::ErrStepsAuto,         "Denoise steps (-l) must be positive, or empty for auto.",
                                "去噪步数 (-l) 必须为正数，留空表示自动。"},
    {Str::ErrSizeTooSmall,      "This size is below what the model accepts: (width/16) x (height/16) must be at least 32.",
                                "尺寸小于模型要求：(宽/16) × (高/16) 至少为 32。"},
    {Str::ErrSizeTooLarge,      "This generator accepts at most %d x %d per side; got %d x %d.",
                                "该生成程序每边最大 %d × %d，当前为 %d × %d。"},
    {Str::ErrOutpaintTooLarge,  "Outpaint result is %d x %d, over the %d x %d limit: the picture plus the -x margins must stay within it. Use a smaller picture or smaller margins.",
                                "扩图结果为 %d × %d，超过每边 %d × %d 的上限（输入图尺寸加上 -x 边距不能超过它）。请换用更小的图片或减小边距。"},
    {Str::ErrBatch,             "Batch size (-b) must be positive.",  "批量大小 (-b) 必须为正数。"},
    {Str::ErrTooManyImages,     "At most 10 reference images (-i) are allowed.",
                                "参考图 (-i) 最多允许 10 张。"},
    {Str::ErrTileNeedsControl,  "Tile upscale (-t) needs a control image (-c).",
                                "Tile 放大 (-t) 需要控制图 (-c)。"},
    {Str::ErrOutpaintFormat,    "Outpaint (-x) must be four numbers: left,top,right,bottom.",
                                "扩图 (-x) 需要四个数字：左,上,右,下。"},
    {Str::ErrZInputNeeded,      "This mode needs an input image (-i).",
                                "该模式需要指定输入图 (-i)。"},
    {Str::ErrZMaskNeeded,       "Inpaint needs a mask (-k).",         "局部重绘需要指定蒙版 (-k)。"},
    {Str::ErrZOutpaintNeeded,   "Outpaint needs the margins (-x).",   "扩图需要填写扩展边距 (-x)。"},
    {Str::ErrZControlNeeded,    "This mode needs a control image (-c).",
                                "该模式需要指定控制图 (-c)。"},
    {Str::ErrExitCode,          "qwenimage-ncnn-vulkan exited with code %d",
                                "qwenimage-ncnn-vulkan 退出，返回码 %d"},
    {Str::StopSent,             "[stop] sending SIGTERM…",            "[停止] 正在发送 SIGTERM…"},
    {Str::StopNoProcess,        "[stop] pkill returned non-zero (process may already be gone)",
                                "[停止] pkill 返回非零（进程可能已结束）"},

    {Str::LogInvalid,           "[invalid] ",                         "[参数无效] "},
    {Str::LogOutputRenamed,     "[output] name taken, using ",        "[输出] 文件名已存在，改用 "},
    {Str::LogPreviewLoaded,     "[preview] loaded ",                  "[预览] 已加载 "},
    {Str::LogPreviewNotFound,   "[preview] output not found: ",       "[预览] 未找到输出文件："},
    {Str::LogDone,              "[done] exit code 0",                 "[完成] 退出码 0"},
    {Str::LogFailed,            "[failed] exit code ",                "[失败] 退出码 "},
    {Str::LogStopped,           "[stop] the run was ended by the user",
                                "[停止] 任务已被手动结束"},
    {Str::LogZModelNoControl,   "[model] this model has no ControlNet weights; mode set back to text-to-image",
                                "[模型] 该模型没有 ControlNet 权重，模式已改回文生图"},
    {Str::InitBinary,           "[init] generator: ",                 "[初始化] 生成程序："},
    {Str::InitModel,            "[init] model dir: ",                 "[初始化] 模型目录："},
    {Str::InitConfig,           "[init] config file: ",               "[初始化] 配置文件："},

    {Str::PreviewOutputTitle,   "Qwen-Image Preview",                 "Qwen-Image 预览"},
    {Str::TextToImage,          "text-to-image",                      "文生图"},
    {Str::ImageEditing,         "image editing",                      "图像编辑"},
    {Str::OptionsHintZImage,    "Options follow `zimage-ncnn-vulkan -h`: -p -n -o -i -k -x -c -w -t -s -l -r -m -g -b",
                                "选项对应 `zimage-ncnn-vulkan -h`：-p -n -o -i -k -x -c -w -t -s -l -r -m -g -b"},

    {Str::NamedPreset,          "Config",                             "配置"},
    {Str::PresetDefault,        "Default",                            "默认"},
    {Str::PresetSave,           "Save",                               "保存"},
    {Str::PresetRename,         "Rename",                             "重命名"},
    {Str::PresetDelete,         "Delete",                             "删除"},
    {Str::PresetSaveTitle,      "Save preset",                        "保存配置"},
    {Str::PresetRenameTitle,    "Rename preset",                      "重命名配置"},
    {Str::PresetNameLabel,      "Preset name",                        "配置名称"},
    {Str::PresetDeleteAsk,      "Delete the preset \"%s\"?",          "确定删除配置 \"%s\"？"},
    {Str::PresetOverwrite,      "Overwrite",                          "覆盖"},
    {Str::PresetOverwriteAsk,   "Overwrite the preset \"%s\" with the current settings?",
                                "用当前设置覆盖配置 \"%s\"？"},
    {Str::PresetNeedSelection,  "Select a preset in the list first.",  "请先从列表中选择一个配置。"},
    {Str::PresetSaved,          "[preset] saved: ",                   "[预设] 已保存："},
    {Str::PresetApplied,        "[preset] applied: ",                 "[预设] 已应用："},
    {Str::PresetDeleted,        "[preset] deleted: ",                 "[预设] 已删除："},
    {Str::PresetRenamed,        "[preset] renamed to ",               "[预设] 已重命名为 "},
    {Str::PresetNameTaken,      "That name is already in use. Delete that preset first, or choose another name.",
                                "该名称已被使用。请先删除该配置，或换一个名称。"},
};

constexpr int kTableSize = sizeof(kTable) / sizeof(kTable[0]);

Lang g_lang = Lang::English;

const char *lookup(Str key, Lang lang) {
    for (int i = 0; i < kTableSize; ++i) {
        if (kTable[i].key == key)
            return (lang == Lang::ChineseSimplified) ? kTable[i].zh : kTable[i].en;
    }
    return ""; // unknown key -> empty string
}

} // namespace

void i18n_set_language(Lang lang) { g_lang = lang; }
Lang i18n_language() { return g_lang; }

const char *tr(Str s) { return lookup(s, g_lang); }
std::string trs(Str s) { return std::string(lookup(s, g_lang)); }

const char *language_name(Lang lang) {
    switch (lang) {
        case Lang::English:           return "English";
        case Lang::ChineseSimplified: return "简体中文";
    }
    return "English";
}

void configure_ui_font() {
    // FLTK's built-in Helvetica has no CJK coverage, so Chinese labels would
    // come out as empty boxes. Point the base faces at a family that has it;
    // if that family is missing, fontconfig substitutes the closest match.
    Fl::set_font(FL_HELVETICA,      "Noto Sans CJK SC");
    Fl::set_font(FL_HELVETICA_BOLD, "Noto Sans CJK SC:style=Bold");
    Fl::set_font(FL_COURIER,        "Noto Sans Mono CJK SC");
    Fl::set_font(FL_COURIER_BOLD,   "Noto Sans Mono CJK SC:style=Bold");
}
