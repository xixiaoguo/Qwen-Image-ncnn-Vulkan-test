// picker.h - self-drawn file / directory chooser
#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Browser.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Input.H>

#include <string>
#include <vector>

// A pane that draws its image scaled to fit while keeping the aspect ratio
// (Fl_Box would stretch it to fill the widget).
class ImagePane : public Fl_Widget {
public:
    ImagePane(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
    ~ImagePane() override;
    void set_image(Fl_Shared_Image *img);   // takes ownership of the reference
    void resize(int X, int Y, int W, int H) override;
protected:
    void draw() override;
private:
    void rescale();
    Fl_Shared_Image *m_img = nullptr;       // reference counted
    // Plain Fl_Image: Fl_Shared_Image::copy() may return a Fl_Shared_Image,
    // which derives from Fl_Image directly (not from Fl_RGB_Image), so casting
    // it down would corrupt the delete that follows.
    Fl_Image *m_scaled = nullptr;           // pre-scaled copy, owned
};

// A chooser the application draws itself. The desktop's native dialog ignores
// our font size and stays in English, so everything lives inside FLTK where it
// follows this program's own scale and translations.
class Picker : public Fl_Double_Window {
public:
    // Chosen directory, or "" when cancelled.
    // *last_dir (optional) receives the folder that was being browsed, whether
    // the dialog was accepted or cancelled, so callers can remember it.
    static std::string choose_directory(const char *title, const std::string &start,
                                        const char *ok_label,
                                        std::string *last_dir = nullptr);

    // Chosen files, empty when cancelled.
    static std::vector<std::string> choose_files(const char *title,
                                                 const std::string &start,
                                                 const char *filter,
                                                 bool multi,
                                                 const char *ok_label,
                                                 std::string *last_dir = nullptr);

private:
    Picker(int W, int H, const char *title, bool dir_mode, bool multi,
           const char *filter, const char *ok_label);
    ~Picker() override;

    void build();
    void enter_dir(const std::string &path);
    void refresh_list();
    void update_preview();
    void sync_mount();      // highlight the volume the current folder lives on
    void accept();
    void go_up();
    void go_home();

    std::string picked_name() const;
    std::string list_name(int row) const;
    std::string full_path(const char *name) const;

    static void cb_browser(Fl_Widget *, void *);
    static void cb_ok(Fl_Widget *, void *);
    static void cb_cancel(Fl_Widget *, void *);
    static void cb_up(Fl_Widget *, void *);
    static void cb_home(Fl_Widget *, void *);
    static void cb_path(Fl_Widget *, void *);
    static void cb_mount(Fl_Widget *, void *);
    static void cb_name(Fl_Widget *, void *);

    bool m_dir_mode = false;
    bool m_multi = false;
    std::string m_filter;
    std::string m_ok_label;
    bool m_accepted = false;
    std::string m_dir;
    std::vector<std::string> m_picked;

    Fl_Box *m_dir_label = nullptr;
    Fl_Input *m_path = nullptr;
    Fl_Choice *m_mount = nullptr;      // jump straight to another volume
    Fl_Button *m_up = nullptr;
    Fl_Button *m_home = nullptr;
    Fl_File_Browser *m_browser = nullptr;
    ImagePane *m_preview = nullptr;    // thumbnail of the highlighted image
    Fl_Box *m_name_label = nullptr;
    Fl_Input *m_name = nullptr;
    Fl_Button *m_ok = nullptr;
    Fl_Button *m_cancel = nullptr;
};
