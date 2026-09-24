// image_viewer.h - scrollable / zoomable image preview widget
#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Image.H>
#include <string>

// A preview widget built from Fl_Scroll + Fl_Box.
// Supports zoom in/out/fit and keeps the original pixel buffer so that we can
// rescale without reloading from disk.
class ImageViewer : public Fl_Group {
public:
    ImageViewer(int X, int Y, int W, int H, const char *L = nullptr);
    ~ImageViewer() override;

    // Load from a file (png/jpg/jpeg/webp/bmp/gif) using FLTK's image system.
    // Returns false and sets error() when loading fails.
    bool load(const std::string &path);
    void clear();

    void zoom_in();
    void zoom_out();
    void zoom_fit();
    void zoom_1to1();

    bool has_image() const { return m_source != nullptr; }
    double zoom() const { return m_zoom; }
    const std::string &error() const { return m_error; }
    const std::string &path() const { return m_path; }

    void resize(int X, int Y, int W, int H) override;

private:
    void apply_zoom();
    void layout_children();

    // Effective scrollbar width in pixels: the widget's own override when one
    // is set, otherwise the global Fl::scrollbar_size() that the log view's
    // scrollbars also use.
    int scrollbar_px() const;

    Fl_Scroll *m_scroll = nullptr;
    Fl_Box *m_canvas = nullptr;

    Fl_Image *m_source = nullptr;   // original image (owned)
    Fl_RGB_Image *m_scaled = nullptr; // current scaled copy (owned)
    double m_zoom = 1.0;
    std::string m_error;
    std::string m_path;
};
