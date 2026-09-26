// presets.h - named, reusable presets for the prompt, the negative prompt and
// the generation parameters.
#pragma once

#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class Settings;

// One saved preset. Everything is a (key -> value) list, so a single type
// serves a prompt preset ("text" -> the words) and a parameter preset
// ("steps" -> "40") alike.
struct Preset {
    std::string name;
    std::vector<std::pair<std::string, std::string>> fields;

    std::string value(const std::string &key) const;
};

// The widgets and data behind one preset row.
//
// Entry 0 of the dropdown is a placeholder carrying the row's own name
// ("Preset"), so the control reads as a preset picker before anything has been
// saved; entry i+1 is items[i].
struct PresetRow {
    std::string key;                 // settings prefix, e.g. "prompt_preset"
    std::vector<Preset> items;

    Fl_Choice *choice = nullptr;
    Fl_Button *save = nullptr;       // capture the live values under a name
    Fl_Button *rename = nullptr;     // rename the selected preset
    Fl_Button *del = nullptr;        // delete the selected preset
    void *owner = nullptr;           // opaque back-pointer, set by the window

    // Called with the index of the picked entry. Wired to the menu items rather
    // than to the widget; see rebuild_choice() for why.
    std::function<void(int)> on_pick;

    void rebuild_choice();
    // Index of the picked entry, or -1 for the built-in "Default" entry.
    int  picked() const;
};

// Read/write a whole list under "<key>_count" and "<key>_<i>_<field>".
// `fields` names the keys a preset of this kind carries ("text" for a prompt).
std::vector<Preset> load_presets(const Settings &cfg, const std::string &key,
                                 const std::vector<std::string> &fields);
void save_presets(Settings &cfg, const std::string &key,
                  const std::vector<Preset> &items);

// Modal one-line "name" dialog, drawn with this application's own font and
// metrics (a native dialog would ignore both). Returns false when the user
// cancels or leaves the field empty.
bool ask_preset_name(const char *title, const char *label, const char *ok_label,
                     std::string &value);

// Modal yes/no question, also drawn by this application. fl_choice() is not
// used because its first button is a Fl_Return_Button, and the return-arrow
// glyph it draws looks out of place next to plain buttons.
bool ask_confirm(const char *title, const char *message, const char *ok_label,
                 const char *cancel_label);
