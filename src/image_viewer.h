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
    // Zoom so that the picture point under (px, py) - canvas coordinates -
    // stays there. Used by the wheel.
    void zoom_at(int px, int py, double new_zoom);
    void zoom_fit();
    void zoom_1to1();

    // The wheel zooms in the detail view: up closer, down further away.
    int handle(int event) override;

    bool has_image() const { return m_source != nullptr; }
    double zoom() const { return m_zoom; }
    const std::string &error() const { return m_error; }
    const std::string &path() const { return m_path; }

    void resize(int X, int Y, int W, int H) override;

private:
    void apply_zoom();
    void layout_children();

    // How far the view can scroll, and a move that stays inside that range.
    void scroll_limits(int &max_x, int &max_y) const;
    void scroll_clamped(int x, int y);

    friend class PanCanvas;

    // Decode the current file at full size when the view needs more pixels than
    // the initial, cheaper decode carries. Returns true when the source changed.
    bool upgrade_resolution();

    // Highest zoom that still keeps the scaled copy within a sane amount of
    // memory; the picture is loaded at full resolution, so this matters.
    double max_zoom() const;
    static constexpr double kMaxZoom = 16.0;

    // Effective scrollbar width in pixels: the widget's own override when one
    // is set, otherwise the global Fl::scrollbar_size() that the log view's
    // scrollbars also use.
    int scrollbar_px() const;

    Fl_Scroll *m_scroll = nullptr;
    Fl_Box *m_canvas = nullptr;

    // A shared image goes back to FLTK's cache with release(); one we built
    // ourselves (the libpng decode, the checkerboard composite) is deleted.
    Fl_Image *m_source = nullptr;   // decoded, possibly smaller than the file
    Fl_Image *m_scaled = nullptr;   // current scaled copy
    // The file's own dimensions: zoom is a multiple of these, so the zoom level
    // means the same thing before and after a full resolution upgrade.
    int  m_orig_w = 0, m_orig_h = 0;
    bool m_hi_res = false;          // m_source already carries every pixel
    double m_zoom = 1.0;
    std::string m_error;
    std::string m_path;
};
