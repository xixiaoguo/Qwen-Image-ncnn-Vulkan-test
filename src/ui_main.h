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

#include "backend.h"
#include "gallery.h"
#include "hint_input.h"
#include "i18n.h"
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

    // Switch the generator this window drives. Both binaries are looked up in
    // the application directory; when only one of them is installed the
    // switcher is hidden and this is called once at startup.
    void set_backend(Backend b);
    Backend backend() const { return m_backend; }

    // Declaring show() would hide the inherited show(argc, argv) overload.
    using Fl_Double_Window::show;
    void resize(int X, int Y, int W, int H) override;
    void show() override;   // puts the caret in the prompt box
    void hide() override;   // settings are written when the window closes
    int  handle(int event) override;   // Esc leaves the detail view

private:
    // ---- widgets ----
    // Fl_Text_Editor (buffer based) rather than Fl_Multiline_Input: the latter
    // re-lays-out the whole string per keystroke, which wraps badly and turns
    // long prompts into a stall.
    Hint_Text_Editor *m_prompt = nullptr;
    Hint_Text_Editor *m_negative = nullptr;
    Fl_Text_Buffer *m_prompt_buf = nullptr;
    Fl_Text_Buffer *m_negative_buf = nullptr;
    Hint_Float_Input *m_cfg = nullptr;     // -w
    Hint_Browser *m_inputs = nullptr;      // -i list
    Hint_Int_Input *m_width = nullptr;     // -s
    Hint_Int_Input *m_height = nullptr;
    Hint_Int_Input *m_steps = nullptr;     // -l
    Hint_Int_Input *m_seed = nullptr;      // -r
    Fl_Check_Button *m_random_seed = nullptr;
    Fl_Choice *m_gpu = nullptr;            // -g
    Hint_Int_Input *m_batch = nullptr;     // -b

    // Output selection: folder + base name + format are joined into -o.
    Hint_Input *m_output_dir = nullptr;
    Hint_Input *m_output_name = nullptr;
    Fl_Choice *m_output_format = nullptr;

    Fl_Choice *m_lang_choice = nullptr;    // language selector

    // ---- engine (generator) ----
    Backend m_backend = Backend::Qwen;
    BackendAvailability m_avail;           // which binaries exist next to us
    Fl_Widget *m_lbl_backend = nullptr;
    Fl_Choice *m_backend_choice = nullptr; // only built when both are installed

    // Named, reusable presets: one row per part of the form (prompt, negative
    // prompt, generation parameters). Each row keeps its own list.
    PresetRow m_prompt_presets;
    PresetRow m_negative_presets;
    PresetRow m_param_presets;

    // Z-Image only: the working mode decides which of the LanPaint and
    // ControlNet inputs below are used, so the window shows just the rows that
    // the chosen mode passes on the command line.
    Fl_Widget *m_lbl_zmodel = nullptr;
    Fl_Choice *m_zmodel = nullptr;         // z-image-turbo or z-image
    Fl_Widget *m_lbl_zmode = nullptr;
    Fl_Choice *m_zmode = nullptr;          // 0..4, see ZMode in ui_main.cpp
    Hint_Input *m_zinput = nullptr;        // -i
    Fl_Button *m_btn_browse_zinput = nullptr;
    Hint_Input *m_mask = nullptr;          // -k
    Fl_Button *m_btn_browse_mask = nullptr;
    Hint_Input *m_control = nullptr;       // -c
    Fl_Button *m_btn_browse_control = nullptr;
    Hint_Float_Input *m_control_scale = nullptr;  // -w for zimage
    Hint_Input *m_outpaint = nullptr;      // -x
    Fl_Widget *m_lbl_mask = nullptr;
    Fl_Widget *m_lbl_control = nullptr;
    Fl_Widget *m_lbl_control_scale = nullptr;
    Fl_Widget *m_lbl_outpaint = nullptr;

    // Z-Image: the generator takes -c alongside -i/-k/-x, so the control image
    // is not exclusive with inpaint and outpaint - it rides along when this
    // switch is on. The ControlNet and tile modes need it outright, and there
    // the switch is not shown. Without it the form would be stricter than the
    // binary, which is what it used to be.
    Fl_Check_Button *m_enable_zctl = nullptr;
    bool zcontrol_on() const;      // is -c part of what the mode passes on?

    // Qwen only: which optional inputs the form shows. The parameters are
    // independent - -c and --lora can be combined, with or without -i - so the
    // mode decides what is on screen, not what gets passed on.
    // Two independent switches instead of a picker: plain text-to-image is
    // both of them off, and ControlNet with a LoRA is both of them on - which
    // is a combination the generator supports and a picker cannot express
    // without pretending one of them is "the mode".
    Fl_Check_Button *m_qctl = nullptr;     // -c + --control-scale
    Fl_Check_Button *m_qlora = nullptr;    // --lora + --lora-scale
    Hint_Input *m_lora = nullptr;          // --lora
    Fl_Button *m_btn_browse_lora = nullptr;
    Hint_Float_Input *m_lora_scale = nullptr;  // --lora-scale
    Fl_Widget *m_lbl_lora = nullptr;
    Fl_Widget *m_lbl_lora_scale = nullptr;

    // Optional inputs carry a switch of their own, drawn right after the
    // field's caption. An optional option that cannot be turned off is not
    // really optional: with the box unticked the field is dead and, more to the
    // point, stays off the command line even when it still holds a path from an
    // earlier run. That is the difference between "can be left empty" and
    // "will not be sent".
    //
    // Only Qwen's reference list has one. Z-Image's -i is required in the two
    // modes that use it and refused outright by the others ("input image
    // requires -k/--mask or --outpaint"), so there is nothing optional to
    // switch there.
    Fl_Check_Button *m_enable_inputs = nullptr;  // Qwen reference list (-i)
    bool ref_list_active() const;   // is the reference list off the run?

    // Fixed locations. The generator binary and the model folder are always
    // taken from the directory that holds this executable, so the UI has no
    // path controls for them.
    std::string m_app_dir;
    std::string m_exe_path;                // <app dir>/<backend binary>
    std::string m_model_path;              // <app dir>/<backend model folder>

    // Per-engine copies of the shared generation parameters. The two binaries
    // document different defaults for some of them (-l is 40 steps for Qwen but
    // "auto" for Z-Image, and -r is 42 against "rand"), so each engine keeps
    // its own set: switching engines must not carry values across.
    struct Params {
        std::string cfg = "1.0";
        std::string width = "1024";
        std::string height = "1024";
        std::string steps = "40";
        std::string seed = "42";
        std::string batch = "1";
        bool        random_seed = false;
        std::string gpu = "auto";
    };
    Params m_params[2];             // indexed by (int)Backend
    static const Params &default_params(Backend b);
    void store_params(Backend b);   // read the controls into m_params[b]
    void load_params(Backend b);    // write m_params[b] into the controls

    // Qwen's Fun Acc / PDD LoRA adapters run a fixed 4-step schedule, so -l is
    // pinned to that while such an adapter is selected and the field cannot be
    // edited. The count the user actually chose is parked here for the
    // duration: leaving the mode puts it back, and everything that writes a
    // step count down - the config file, a saved preset - reads
    // effective_steps() rather than the field, so the pinned 4 is never what
    // gets stored.
    std::string m_steps_user;
    bool steps_pinned() const;             // Qwen with the LoRA box ticked
    std::string effective_steps() const;   // -l as the user set it
    void update_steps_pin();               // apply or release the pin

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

    Fl_Button *m_btn_browse_output = nullptr;
    Fl_Button *m_btn_add_image = nullptr;
    Fl_Button *m_btn_del_image = nullptr;
    Fl_Button *m_btn_gallery = nullptr;   // back to the grid
    Fl_Button *m_btn_latest = nullptr;    // open the newest picture
    Fl_Button *m_btn_refresh = nullptr;   // rescan the output folder
    Fl_Button *m_btn_open_dir = nullptr;   // reveal the output folder
    Fl_Button *m_btn_fit = nullptr;
    Fl_Button *m_btn_1to1 = nullptr;
    Fl_Button *m_btn_about = nullptr;   // far top-left corner, opens the repo

    Fl_Button *m_btn_generate = nullptr;
    Fl_Button *m_btn_stop = nullptr;
    Fl_Text_Display *m_log_view = nullptr;
    Fl_Text_Buffer *m_log_buf = nullptr;
    // Style buffer that runs alongside the log text: 'A' plain, 'B' red, so
    // errors stand out from the generator's ordinary chatter.
    Fl_Text_Buffer *m_log_style = nullptr;
    Fl_Progress *m_progress = nullptr;

    // The preview pane: thumbnail grid of the output folder, detail view and
    // film strip. It replaces the single-image viewer this window used to have.
    GalleryView *m_gallery = nullptr;

    std::atomic<bool> m_running{false};
    // Set while the user's Stop request is on its way. A run that ends after
    // that is not a failure, and the window must not report it as one.
    std::atomic<bool> m_stop_requested{false};
    // When the current run started, for the progress line's clock. Mutable
    // because Fl::seconds_since() insists on a non-const reference, and the
    // helper that reads it is const.
    mutable Fl_Timestamp m_run_started{};
    static void cb_run_tick(void *data);
    std::thread m_worker;
    pid_t m_child_pid = -1;

    // ---- helpers ----
    void build_widgets();     // instantiate every control
    void style_widgets();     // colours and fonts
    void layout_widgets();    // position everything for the current size
    // Narrowest window that still shows four columns in the grid.
    int  grid_min_width() const;
    // One layout pass. `extra_prompt_h` is height added to the two prompt
    // boxes; `bottom_out` receives the lowest pixel any control reached.
    void layout_pass(int extra_prompt_h, int *bottom_out);
    int  sc(int design) const;   // design units -> live pixels
    // Puts a small "enable this option" switch at the far right of a row -
    // past the field, the dropdown and the buttons, not right after the
    // caption - so they all line up down the panel's right edge.
    void place_switch(Fl_Widget *box, int lx, int ly, int row_h, int lw);

    void load_settings();     // apply the config file to the controls
    void save_settings();     // write the controls back to the config file
    std::string output_path() const;        // folder + name + format
    std::string unique_output_path() const; // same, bumped to a free name
    std::string output_dir() const;         // folder the gallery scans

    // ---- engine / preview ----
    void update_backend_ui();      // show or hide the per-backend controls
    void update_param_hints();     // grey "default=..." text in empty fields
    void update_auto_size();

    // Outpaint and inpaint both pin Size (-s) to the input picture, and both
    // may need a scaled copy of it - inpaint whenever the picture is over the
    // 2048 cap or not a multiple of 16, outpaint when the margins push it over.
    // These remember what the temporary files were made from, so a pass over
    // the form does not redo the work (and reprint the warning) per keystroke.
    std::string m_auto_input;      // scaled picture, "" when the original fits
    std::string m_auto_mask;       // scaled mask, must match m_auto_input
    std::string m_auto_control;    // scaled control image, sized to the output
    std::string m_auto_key;        // what they were made from
    // Inpaint only: the last "mask does not match the picture" combination that
    // was reported, so the warning appears once instead of on every keystroke.
    std::string m_mask_warn_key;
    // Same for the control image: one notice per (file, target size) pair.
    std::string m_control_warn_key;
    // Tile upscale: one notice per (source, target size) pair.
    std::string m_tile_warn_key;
    // Size of the scaled control copy, so the output size can follow it.
    int m_control_w = 0;
    int m_control_h = 0;
    int m_auto_w = 0;
    int m_auto_h = 0;

    void update_zimage_rows();     // show the rows the chosen mode needs
    void update_qwen_rows();       // same idea for Qwen's ControlNet/LoRA rows
    // Print the geometry of the scroller and of the optional rows. For the
    // cases where the layout has to be checked without a screenshot - a
    // Wayland session exposes nothing to an X11 grab.
    void dump_layout_state(const char *tag = nullptr);
    // Drive the optional switches through on/off from a script
    // (QWEN_TEST_TOGGLE): what a click does to the layout. The order is the
    // one a user walks - turn each on, then turn each off again - because the
    // rows appearing and disappearing is where the layout can go wrong.
    void test_toggle_sequence();
    static void test_toggle_cb(void *d);
    static void test_quit_cb(void *d);
    void update_zmodel_modes();    // enable the modes the chosen model can run
    void update_model_path();      // -m for the current engine and model
    void update_view_buttons();    // zoom buttons follow the gallery mode
    void refresh_gallery();        // rescan the output folder and redraw it
    void pick_image_into(Fl_Input *field, Str title,
                         const char *pattern = "*.{png,jpg,jpeg,webp,bmp}");

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
    // Replace the values of an entry that already exists, keeping its name and
    // its slot in the dropdown.
    void overwrite_preset(PresetRow &row, int index, const Preset &values);
    void rename_selected_preset(PresetRow &row);
    void delete_selected_preset(PresetRow &row);

    GenOptions collect_options();
    void log(const std::string &s);
    std::string elapsed_suffix() const;   // " 01:23" for the progress line
    void start_generation();
    void stop_generation();
    void open_output_dir();
    bool validate(GenOptions &opt, std::string &err);

    void pick_input_images();
    void remove_selected_input();
    void pick_output_dir();

    // Translate the message of a native file chooser.
    std::string T(Str s) const;

    static void cb_generate(Fl_Widget *, void *);
    static void cb_stop(Fl_Widget *, void *);
    static void cb_latest(Fl_Widget *, void *);
    static void cb_refresh(Fl_Widget *, void *);
    static void cb_gallery_back(Fl_Widget *, void *);
    static void cb_add_input(Fl_Widget *, void *);
    static void cb_del_input(Fl_Widget *, void *);
    static void cb_browse_mask(Fl_Widget *, void *);
    static void cb_browse_control(Fl_Widget *, void *);
    static void cb_browse_zinput(Fl_Widget *, void *);
    static void cb_zinput_changed(Fl_Widget *, void *);
    static void cb_mask_changed(Fl_Widget *, void *);
    static void cb_size_changed(Fl_Widget *, void *);
    static void cb_control_changed(Fl_Widget *, void *);
    static void cb_outpaint_changed(Fl_Widget *, void *);
    static void cb_zmode(Fl_Widget *, void *);
    static void cb_qmode(Fl_Widget *, void *);
    static void cb_enable_optional(Fl_Widget *, void *);
    static void cb_browse_lora(Fl_Widget *, void *);
    static void cb_zmodel(Fl_Widget *, void *);
    static void cb_browse_output(Fl_Widget *, void *);
    static void cb_open_output_dir(Fl_Widget *, void *);
    static void cb_about(Fl_Widget *, void *);
    static void cb_zoom_fit(Fl_Widget *, void *);
    static void cb_zoom_1to1(Fl_Widget *, void *);
    static void cb_language(Fl_Widget *, void *);
    static void cb_backend(Fl_Widget *, void *);
    static void cb_preset_choice(Fl_Widget *, void *);
    static void cb_preset_save(Fl_Widget *, void *);
    static void cb_preset_rename(Fl_Widget *, void *);
    static void cb_preset_delete(Fl_Widget *, void *);

    // log callback bridging to FLTK main thread
    static void on_line_cb(const std::string &line, void *user);
};
