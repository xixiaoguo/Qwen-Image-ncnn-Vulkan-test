// gallery.h - the preview pane: every image of the output folder as a
// thumbnail grid, a detail view for one of them, and a film strip to move
// between the images without going back to the grid.
#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Image.H>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Fl_Scroll;
class ImageViewer;
class GridPane;
class FilmStrip;

// One image found in the output folder. The thumbnails are owned by
// GalleryView and stay valid until the next refresh().
struct GalleryItem {
    std::string path;
    std::string name;
    long long   mtime = 0;          // Unix time; the grid is sorted on this
    Fl_RGB_Image *thumb = nullptr;  // grid cell
    Fl_RGB_Image *strip = nullptr;  // film strip
};

// Both views share one item list, so returning from the detail view lands on
// the same picture that was open, and the film strip is simply another way of
// walking that list.
class GalleryView : public Fl_Group {
public:
    enum class Mode { Grid, Detail };

    GalleryView(int X, int Y, int W, int H, const char *L = nullptr);
    ~GalleryView() override;

    void set_directory(const std::string &dir);
    const std::string &directory() const { return m_dir; }

    // Rescan the folder, queue thumbnails and rebuild both views. The picture
    // that was open stays open when it is still on disk.
    void refresh();

    void show_latest();          // open the newest image in the detail view
    // Open one entry of the list. `from_strip` means the click came from the
    // film strip itself, where the entry is by definition already on screen -
    // so the strip must not be scrolled at all, or a click would move the row
    // the user just scrolled to.
    void open(int index, bool from_strip = false);
    // Highlight an entry without switching views. Used after a run, when the
    // newest picture should be the one the grid points at.
    void select(int index);
    // Move the selection by `delta` entries. Used by the film strip's wheel -
    // flipping through the list one picture at a time is what the strip is for.
    void step(int delta);

    Mode mode() const { return m_mode; }
    void set_mode(Mode m);

    int count() const { return (int)m_items.size(); }
    int current() const { return m_current; }
    // Path of the picture on screen, or "" when none is selected.
    std::string current_path() const;
    // Non-empty when the last scan failed (unreadable folder).
    const std::string &scan_error() const { return m_error; }
    // Thumbnails still waiting to be decoded.
    int pending() const { return m_pending; }
    // How many entries already have their thumbnails (the rest are still being
    // decoded, or their file is not a format we can read).
    int thumbs_loaded() const;

    void zoom_in();
    void zoom_out();
    void zoom_fit();
    void zoom_1to1();

    // Fired after the mode changed, so the owner can relabel its toolbar.
    std::function<void()> on_mode_change;
    // Fired when a picture could not be decoded, with the reason.
    std::function<void(const std::string &)> on_error;

    void resize(int X, int Y, int W, int H) override;
    void show() override;   // re-hides whichever view the mode does not show

private:
    friend class GridPane;
    friend class FilmStrip;

    struct Job { int index; std::string path; };
    struct Thumb {
        int index = -1;
        unsigned generation = 0;
        Fl_RGB_Image *grid = nullptr;
        Fl_RGB_Image *strip = nullptr;
    };

    void relayout();
    void apply_mode_visibility();
    void update_info();
    void layout_grid_canvas();
    void layout_strip_canvas();
    void scroll_grid_to_current();
    void scroll_strip_to_current();    void scan();
    void drop_items();
    void release_thumb(Thumb &t);
    void queue_jobs();
    void collect_thumbs();
    void start_worker();
    void stop_worker();
    void worker_loop();
    static void on_thumb_ready(void *data);

    std::string m_dir;
    std::string m_error;
    std::vector<GalleryItem> m_items;
    int m_current = -1;
    Mode m_mode = Mode::Grid;
    int m_pending = 0;            // jobs of the current generation not answered yet

    // Cell metrics, recomputed whenever the pane is resized.
    int m_cell_w = 200, m_cell_h = 170, m_img_h = 140, m_gap = 10, m_cols = 1;
    // Height the worker decodes strip thumbnails at. Fixed once the window is
    // built: the worker reads it while the UI thread lays the strip out, so it
    // must not change behind its back.
    int m_strip_thumb_h = 88;

    // Where every strip entry sits on its canvas (index n holds the end).
    std::vector<int> m_strip_x;
    std::vector<int> m_strip_w;

    Fl_Scroll  *m_grid_scroll = nullptr;
    GridPane   *m_grid = nullptr;
    Fl_Group   *m_detail = nullptr;
    ImageViewer *m_viewer = nullptr;
    // Under the picture: "name · 1024x1024 · PNG · 1.2 MB".
    Fl_Box     *m_info = nullptr;
    Fl_Scroll  *m_strip_scroll = nullptr;
    FilmStrip  *m_strip = nullptr;

    // Thumbnail decoding runs on its own thread: a folder full of 1024x1024
    // PNGs takes long enough that doing it inline would freeze the window for
    // seconds on every refresh.
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::vector<Thumb> m_ready;
    bool m_quit = false;
    unsigned m_generation = 0;
};
