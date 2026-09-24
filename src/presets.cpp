// presets.cpp - named preset storage plus the small "name it" dialog
#include "presets.h"

#include "i18n.h"
#include "settings.h"
#include "ui_main.h"     // ui_font_base()

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <cstdio>

std::string Preset::value(const std::string &key) const {
    for (const auto &kv : fields)
        if (kv.first == key) return kv.second;
    return "";
}

// Menu-item callback. Re-picking the entry you are already on is a real use:
// it is how hand edits are discarded and the entry's own values come back, and
// how "Default" still restores the compiled-in values after the selected
// preset has been deleted. Fl_Choice alone would not fire then -- the picked
// entry is already the current one -- so build_preset_row() sets
// FL_WHEN_NOT_CHANGED on the dropdown to make every pick count.
static void preset_item_cb(Fl_Widget *w, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    if (!row || !row->on_pick) return;
    // Entry 0 is the built-in Default, which maps to index -1.
    row->on_pick(static_cast<Fl_Choice *>(w)->value() - 1);
}

void PresetRow::rebuild_choice() {
    if (!choice) return;
    choice->clear();

    // Entry 0 is the built-in "Default": compiled in, never written to the
    // config file, and picking it restores the values the program starts with.
    char def[64];
    std::snprintf(def, sizeof(def), "%s", tr(Str::PresetDefault));
    choice->add(def, 0, preset_item_cb, this, 0);

    for (const Preset &p : items)
        choice->add(p.name.c_str(), 0, preset_item_cb, this, 0);

    choice->value(0);
}

int PresetRow::picked() const {
    if (!choice) return -1;
    // Entry 0 is Default, so -1 means "Default" and 0.. index into items.
    return choice->value() - 1;
}

std::vector<Preset> load_presets(const Settings &cfg, const std::string &key,
                                 const std::vector<std::string> &fields) {
    std::vector<Preset> out;
    const int n = cfg.get_int(key + "_count", 0);
    for (int i = 0; i < n; ++i) {
        const std::string pre = key + "_" + std::to_string(i);
        Preset p;
        p.name = cfg.get(pre + "_name", "");
        if (p.name.empty()) continue;      // a hole in the numbering: skip it
        for (const std::string &f : fields) {
            const std::string v = cfg.get(pre + "_" + f, "");
            if (!v.empty()) p.fields.emplace_back(f, v);
        }
        out.push_back(std::move(p));
    }
    return out;
}

void save_presets(Settings &cfg, const std::string &key,
                  const std::vector<Preset> &items) {
    // Wipe the old keys first. The list may have shrunk, and a leftover
    // "<key>_3_name" would resurrect a deleted preset on the next load.
    cfg.remove_prefix(key + "_");
    cfg.set_int(key + "_count", (int)items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string pre = key + "_" + std::to_string(i);
        cfg.set(pre + "_name", items[i].name);
        for (const auto &kv : items[i].fields)
            cfg.set(pre + "_" + kv.first, kv.second);
    }
}

// --------------------------------------------------------------- dialog ----

namespace {

struct NameDialog {
    Fl_Double_Window *win = nullptr;
    Fl_Input *input = nullptr;
    bool accepted = false;
};

void cb_name_ok(Fl_Widget *, void *data) {
    auto *dlg = static_cast<NameDialog *>(data);
    dlg->accepted = true;
    dlg->win->hide();
}

void cb_name_cancel(Fl_Widget *, void *data) {
    static_cast<NameDialog *>(data)->win->hide();
}

} // namespace

bool ask_preset_name(const char *title, const char *label, const char *ok_label,
                     std::string &value) {
    const int fs = ui_font_base();
    const int pad = std::max(10, fs);
    const int row = fs * 21 / 10;          // same row height as the main window
    const int w = std::max(340, fs * 26);
    const int h = pad + row + row / 2 + row + pad + row + pad;

    NameDialog dlg;
    dlg.win = new Fl_Double_Window(w, h, title);
    dlg.win->set_modal();

    Fl_Box *lbl = new Fl_Box(pad, pad, w - 2 * pad, row, label);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lbl->labelsize(fs);

    dlg.input = new Fl_Input(pad, pad + row + row / 2, w - 2 * pad, row);
    dlg.input->textsize(fs);
    dlg.input->value(value.c_str());
    dlg.input->when(FL_WHEN_ENTER_KEY_ALWAYS);   // Enter confirms
    dlg.input->callback(cb_name_ok, &dlg);

    // A plain Fl_Button, not Fl_Return_Button: the latter draws a return-arrow
    // glyph on the right, which looks out of place next to the other buttons.
    // Enter still confirms, via the input's FL_WHEN_ENTER_KEY_ALWAYS.
    const int bw = std::max(fs * 5, 72);
    Fl_Button *ok = new Fl_Button(w - pad - bw, h - pad - row, bw, row, ok_label);
    ok->labelsize(fs);
    ok->callback(cb_name_ok, &dlg);

    Fl_Button *cancel = new Fl_Button(
        w - pad - bw - pad - bw, h - pad - row, bw, row, tr(Str::PickerCancel));
    cancel->labelsize(fs);
    cancel->shortcut(FL_Escape);
    cancel->callback(cb_name_cancel, &dlg);

    dlg.win->end();
    // Closing the window (titlebar, Esc) is a cancel.
    dlg.win->callback(cb_name_cancel, &dlg);

    if (Fl_Window *parent = Fl::first_window())
        dlg.win->position(parent->x() + (parent->w() - w) / 2,
                          parent->y() + (parent->h() - h) / 3);

    dlg.win->show();
    dlg.input->take_focus();
    while (dlg.win->shown()) Fl::wait();

    const char *text = dlg.input->value();
    const bool got = dlg.accepted && text && *text;
    if (got) value = text;
    delete dlg.win;
    return got;
}
