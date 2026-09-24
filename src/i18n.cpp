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
    {Str::AppTitle,             "Qwen-Image ncnn Vulkan GUI",       "Qwen-Image ncnn Vulkan 图形界面"},

    {Str::Language,             "Language:",                          "语言："},

    {Str::Binary,               "Binary:",                            "程序："},
    {Str::WorkDir,              "Work dir:",                          "工作目录："},
    {Str::Model,                "Model (-m):",                        "模型 (-m)："},
    {Str::Browse,               "…",                                  "…"},

    {Str::GroupGeneration,      "Generation settings",                "生成设置"},

    {Str::PromptRequired,       "Prompt (-p), required",              "提示词 (-p)，必填"},
    {Str::NegativeOptional,     "Negative prompt (-n), optional",     "负向提示词 (-n)，可选"},

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
    {Str::AddImage,             "Add image…",                         "添加图片…"},
    {Str::RemoveSelected,       "Remove selected",                    "移除所选"},

    {Str::PreviewLatest,        "Preview latest",                     "预览最新"},
    {Str::Reload,               "Reload",                             "重新加载"},
    {Str::OpenOutputDir,        "Open folder",                        "打开输出文件夹"},
    {Str::Fit,                  "Fit",                                "适应窗口"},
    {Str::ZoomIn,               "+",                                  "+"},
    {Str::ZoomOut,              "-",                                  "-"},
    {Str::Zoom1to1,             "1:1",                                "1:1"},

    {Str::Generate,             "Generate",                           "开始生成"},
    {Str::Stop,                 "Stop",                               "停止"},

    {Str::Ready,                "Ready",                              "就绪"},
    {Str::Running,              "running…",                           "运行中…"},
    {Str::Done,                 "done",                               "完成"},
    {Str::Failed,               "failed",                             "失败"},
    {Str::ReadyHint,            "Ready. Generator and model come from this application's own directory.",
                                "就绪。生成程序与模型目录都取自本程序所在目录。"},
    {Str::OptionsHint,          "Options follow `qwenimage-ncnn-vulkan -h`: -p -n -w -o -i -s -l -r -m -g -b",
                                "选项对应 `qwenimage-ncnn-vulkan -h`：-p -n -w -o -i -s -l -r -m -g -b"},

    {Str::SelectRefImages,      "Select reference image(s)",          "选择参考图片"},
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
    {Str::ErrPromptRequired,    "Prompt (-p) is required.",           "提示词 (-p) 不能为空。"},
    {Str::ErrCfg,               "CFG scale (-w) must be a non-negative number.",
                                "CFG 比例 (-w) 必须是非负数字。"},
    {Str::ErrOutput,            "Output path (-o) is required.",      "输出路径 (-o) 不能为空。"},
    {Str::ErrOutputDir,         "Cannot create output folder: ",      "无法创建输出目录："},
    {Str::ErrModel,             "Model path (-m) is required.",       "模型路径 (-m) 不能为空。"},
    {Str::ErrSizePositive,      "Image size (-s) must be positive.",  "图像尺寸 (-s) 必须为正数。"},
    {Str::ErrSizeMultiple,      "Image size must be a multiple of %d (%s). Got %dx%d.",
                                "图像尺寸必须是 %d 的倍数（%s）。当前为 %dx%d。"},
    {Str::ErrSteps,             "Denoise steps (-l) must be positive.","去噪步数 (-l) 必须为正数。"},
    {Str::ErrBatch,             "Batch size (-b) must be positive.",  "批量大小 (-b) 必须为正数。"},
    {Str::ErrTooManyImages,     "At most 10 reference images (-i) are allowed.",
                                "参考图 (-i) 最多允许 10 张。"},
    {Str::ErrExitCode,          "qwenimage-ncnn-vulkan exited with code %d",
                                "qwenimage-ncnn-vulkan 退出，返回码 %d"},
    {Str::StopSent,             "[stop] sending SIGTERM…",            "[停止] 正在发送 SIGTERM…"},
    {Str::StopNoProcess,        "[stop] pkill returned non-zero (process may already be gone)",
                                "[停止] pkill 返回非零（进程可能已结束）"},

    {Str::LogInvalid,           "[invalid] ",                         "[参数无效] "},
    {Str::LogPreviewLoaded,     "[preview] loaded ",                  "[预览] 已加载 "},
    {Str::LogPreviewNotFound,   "[preview] output not found: ",       "[预览] 未找到输出文件："},
    {Str::LogDone,              "[done] exit code 0",                 "[完成] 退出码 0"},
    {Str::LogFailed,            "[failed] exit code ",                "[失败] 退出码 "},
    {Str::InitBinary,           "[init] generator: ",                 "[初始化] 生成程序："},
    {Str::InitModel,            "[init] model dir: ",                 "[初始化] 模型目录："},
    {Str::InitConfig,           "[init] config file: ",               "[初始化] 配置文件："},

    {Str::PreviewOutputTitle,   "Qwen-Image Preview",                 "Qwen-Image 预览"},
    {Str::TextToImage,          "text-to-image",                      "文生图"},
    {Str::ImageEditing,         "image editing",                      "图像编辑"},

    {Str::NamedPreset,          "Config",                             "配置"},
    {Str::PresetDefault,        "Default",                            "默认"},
    {Str::PresetSave,           "Save",                               "保存"},
    {Str::PresetRename,         "Rename",                             "重命名"},
    {Str::PresetDelete,         "Delete",                             "删除"},
    {Str::PresetSaveTitle,      "Save preset",                        "保存配置"},
    {Str::PresetRenameTitle,    "Rename preset",                      "重命名配置"},
    {Str::PresetNameLabel,      "Preset name",                        "配置名称"},
    {Str::PresetDeleteAsk,      "Delete the preset \"%s\"?",          "确定删除配置 \"%s\"？"},
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
