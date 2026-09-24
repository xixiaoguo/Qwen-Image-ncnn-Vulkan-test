// i18n.h - tiny runtime translation layer (English / Simplified Chinese)
#pragma once

#include <string>
#include <vector>

// Supported languages. Add new ones here and in i18n.cpp's tables.
enum class Lang {
    English = 0,
    ChineseSimplified = 1,
};

// A translatable string is identified by a stable key. Keep the enum in sync
// with the table in i18n.cpp (the compiler will not catch a mismatch, but the
// lookup falls back to the key text so it degrades gracefully).
enum class Str {
    AppTitle,

    // top bar
    Language,

    // paths
    Binary,
    WorkDir,
    Model,
    Browse,

    // group headers
    GroupGeneration,

    // prompt
    PromptRequired,
    NegativeOptional,

    // numeric / options
    Cfg,
    Output,
    OutputDir,
    OutputName,
    OutputFormat,
    Size,
    Preset,
    Steps,
    Seed,
    Batch,
    RandomSeed,
    Gpu,

    // reference images
    RefImages,
    AddImage,
    RemoveSelected,

    // preview toolbar
    PreviewLatest,
    Reload,
    OpenOutputDir,
    Fit,
    ZoomIn,
    ZoomOut,
    Zoom1to1,

    // actions
    Generate,
    Stop,

    // progress / status
    Ready,
    Running,
    Done,
    Failed,
    ReadyHint,
    OptionsHint,

    // dialogs - titles
    SelectRefImages,
    OutputImage,
    ModelDir,
    ChooseBinary,
    ChooseWorkDir,

    // self-drawn picker
    PickerFolder,
    PickerFileName,
    PickerUp,
    PickerHome,
    PickerVolume,
    PickerOk,
    PickerCancel,
    PickerChooseDir,
    PickerConfirmSel,
    ErrNoSelection,

    // dialogs - messages
    MaxRefImages,
    ErrPromptRequired,
    ErrCfg,
    ErrOutput,
    ErrOutputDir,
    ErrModel,
    ErrSizePositive,
    ErrSizeMultiple,
    ErrSteps,
    ErrBatch,
    ErrTooManyImages,
    ErrExitCode,
    StopSent,
    StopNoProcess,

    // log prefixes
    LogInvalid,
    LogPreviewLoaded,
    LogPreviewNotFound,
    LogDone,
    LogFailed,
    InitBinary,
    InitModel,
    InitConfig,

    // misc
    PreviewOutputTitle,
    TextToImage,
    ImageEditing,

    // named presets
    NamedPreset,
    PresetDefault,
    PresetSave,
    PresetRename,
    PresetDelete,
    PresetSaveTitle,
    PresetRenameTitle,
    PresetNameLabel,
    PresetDeleteAsk,
    PresetNeedSelection,
    PresetSaved,
    PresetApplied,
    PresetDeleted,
    PresetRenamed,
    PresetNameTaken,
};

// Set the active language. Safe to call at runtime; the UI must be rebuilt or
// retranslated by the caller (MainWindow::apply_language does this).
void i18n_set_language(Lang lang);
Lang i18n_language();

// Translate a key into the active language (UTF-8).
const char *tr(Str s);

// Convenience overload for std::string formatting.
std::string trs(Str s);

// Human-readable, localized name of a language (for the dropdown).
const char *language_name(Lang lang);

// Point FLTK's built-in faces at a family that covers CJK glyphs, so Chinese
// labels do not render as empty boxes. Call once before creating widgets.
void configure_ui_font();
