// ui_main.h - main window of the qwenimage GUI
#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Scroll.H>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "i18n.h"
#include "image_viewer.h"
#include "presets.h"
#include "runner.h"
#include "settings.h"

// Base UI font size detected at startup (or overridden via the config file).
// Other windows derive their own metrics from it.
int ui_font_base();

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int W, int H, const char *L = nullptr);
    ~MainWindow() override;

    void append_log(const std::string &line);
    void set_running(bool running);
    void on_process_finished(int exit_code, const std::string &error);

    // Re-apply all translatable labels for the active language.
    void apply_language(Lang lang);

    // Declaring show() would hide the inherited show(argc, argv) overload.
    using Fl_Double_Window::show;
    void resize(int X, int Y, int W, int H) override;
    void show() override;   // puts the caret in the prompt box
    void hide() override;   // settings are written when the window closes

private:
    // ---- widgets ----
    // Fl_Text_Editor (buffer based) rather than Fl_Multiline_Input: the latter
    // re-lays-out the whole string per keystroke, which wraps badly and turns
    // long prompts into a stall.
    Fl_Text_Editor *m_prompt = nullptr;
    Fl_Text_Editor *m_negative = nullptr;
    Fl_Text_Buffer *m_prompt_buf = nullptr;
    Fl_Text_Buffer *m_negative_buf = nullptr;
    Fl_Float_Input *m_cfg = nullptr;       // -w
    Fl_Hold_Browser *m_inputs = nullptr;   // -i list
    Fl_Int_Input *m_width = nullptr;       // -s
    Fl_Int_Input *m_height = nullptr;
    Fl_Int_Input *m_steps = nullptr;       // -l
    Fl_Int_Input *m_seed = nullptr;        // -r
    Fl_Check_Button *m_random_seed = nullptr;
    Fl_Choice *m_gpu = nullptr;            // -g
    Fl_Int_Input *m_batch = nullptr;       // -b

    // Output selection: folder + base name + format are joined into -o.
    Fl_Input *m_output_dir = nullptr;
    Fl_Input *m_output_name = nullptr;
    Fl_Choice *m_output_format = nullptr;

    Fl_Choice *m_lang_choice = nullptr;    // language selector

    // Named, reusable presets: one row per part of the form (prompt, negative
    // prompt, generation parameters). Each row keeps its own list.
    PresetRow m_prompt_presets;
    PresetRow m_negative_presets;
    PresetRow m_param_presets;

    // Fixed locations. The generator binary and the model folder are always
    // taken from the directory that holds this executable, so the UI has no
    // path controls for them.
    std::string m_app_dir;
    std::string m_exe_path;                // <app dir>/qwenimage-ncnn-vulkan
    std::string m_model_path;              // <app dir>/models/qwenimage21

    // Every option is persisted to a config file in the application directory.
    std::unique_ptr<Settings> m_settings;

    // The whole parameter column lives in a scroll container: on short screens
    // (a scaled-down logical desktop, for instance) its natural height exceeds
    // the window and the user can still reach every control.
    Fl_Scroll *m_left_scroll = nullptr;
    Fl_Group  *m_left_col = nullptr;   // content container inside the scroller

    // Decoration: separators, group header, the "x" between width and height.
    Fl_Box *m_sep_top = nullptr;
    Fl_Box *m_grp_gen = nullptr;
    Fl_Box *m_sep_ref = nullptr;
    Fl_Box *m_size_x = nullptr;

    // Labels that must be retranslated live: the owning Fl_Widget keeps the
    // text, so we remember the widgets whose labels are translated.
    Fl_Widget *m_lbl_prompt = nullptr;
    Fl_Widget *m_lbl_negative = nullptr;
    Fl_Widget *m_lbl_cfg = nullptr;
    Fl_Widget *m_lbl_output_dir = nullptr;
    Fl_Widget *m_lbl_output_name = nullptr;
    Fl_Widget *m_lbl_output_format = nullptr;
    Fl_Widget *m_lbl_size = nullptr;
    Fl_Widget *m_lbl_steps = nullptr;
    Fl_Widget *m_lbl_seed = nullptr;
    Fl_Widget *m_lbl_batch = nullptr;
    Fl_Widget *m_lbl_gpu = nullptr;
    Fl_Widget *m_lbl_ref = nullptr;
    Fl_Widget *m_lbl_lang = nullptr;

    Fl_Menu_Button *m_preset_menu = nullptr;
    Fl_Button *m_btn_browse_output = nullptr;
    Fl_Button *m_btn_add_image = nullptr;
    Fl_Button *m_btn_del_image = nullptr;
    Fl_Button *m_btn_preview = nullptr;
    Fl_Button *m_btn_reload = nullptr;
    Fl_Button *m_btn_open_dir = nullptr;   // reveal the output folder
    Fl_Button *m_btn_fit = nullptr;
    Fl_Button *m_btn_zoom_in = nullptr;
    Fl_Button *m_btn_zoom_out = nullptr;
    Fl_Button *m_btn_1to1 = nullptr;

    Fl_Button *m_btn_generate = nullptr;
    Fl_Button *m_btn_stop = nullptr;
    Fl_Text_Display *m_log_view = nullptr;
    Fl_Text_Buffer *m_log_buf = nullptr;
    Fl_Progress *m_progress = nullptr;

    ImageViewer *m_viewer = nullptr;

    std::atomic<bool> m_running{false};
    std::thread m_worker;
    pid_t m_child_pid = -1;

    // ---- helpers ----
    void build_widgets();     // instantiate every control
    void style_widgets();     // colours and fonts
    void layout_widgets();    // position everything for the current size
    int  sc(int design) const;   // design units -> live pixels

    void load_settings();     // apply the config file to the controls
    void save_settings();     // write the controls back to the config file
    std::string output_path() const;   // folder + name + format

    // ---- named presets ----
    void build_preset_row(PresetRow &row, const char *key);
    void style_preset_row(const PresetRow &row);
    void layout_preset_row(const PresetRow &row, int x, int y, int w, int h);
    void load_preset_rows();
    void store_preset_rows();
    Preset capture_preset(const PresetRow &row) const;
    void apply_preset(PresetRow &row, int index);
    void apply_default_values(PresetRow &row);   // the built-in "Default" entry
    void save_current_as_preset(PresetRow &row);
    void rename_selected_preset(PresetRow &row);
    void delete_selected_preset(PresetRow &row);

    GenOptions collect_options();
    void log(const std::string &s);
    void start_generation();
    void stop_generation();
    void preview_latest_output();
    void open_output_dir();
    bool validate(GenOptions &opt, std::string &err);

    void pick_input_images();
    void remove_selected_input();
    void pick_output_dir();

    // Translate the message of a native file chooser.
    std::string T(Str s) const;

    static void cb_generate(Fl_Widget *, void *);
    static void cb_stop(Fl_Widget *, void *);
    static void cb_preview(Fl_Widget *, void *);
    static void cb_add_input(Fl_Widget *, void *);
    static void cb_del_input(Fl_Widget *, void *);
    static void cb_browse_output(Fl_Widget *, void *);
    static void cb_open_output_dir(Fl_Widget *, void *);
    static void cb_zoom_in(Fl_Widget *, void *);
    static void cb_zoom_out(Fl_Widget *, void *);
    static void cb_zoom_fit(Fl_Widget *, void *);
    static void cb_zoom_1to1(Fl_Widget *, void *);
    static void cb_language(Fl_Widget *, void *);
    static void cb_preset_choice(Fl_Widget *, void *);
    static void cb_preset_save(Fl_Widget *, void *);
    static void cb_preset_rename(Fl_Widget *, void *);
    static void cb_preset_delete(Fl_Widget *, void *);

    // log callback bridging to FLTK main thread
    static void on_line_cb(const std::string &line, void *user);
};
