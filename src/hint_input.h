// hint_input.h - input fields that show a grey hint while they are empty
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>

#include <string>

// FLTK hands wheel events to widgets that the pointer is nowhere near, and the
// text widgets scroll on whatever wheel event they are given. These guards keep
// the scrolling where the eye is: the wheel only counts while the pointer is
// actually over the widget.

// FLTK has no placeholder text, so this adds one: while the field is empty the
// hint is drawn in a low-contrast grey inside the box. It never becomes a
// value - it only says what the generator uses when the option is left out,
// which is exactly what the two manuals document per option ("default=40",
// "default=auto", ...).
inline Fl_Color hint_colour() { return fl_rgb_color(0x9a, 0xa1, 0xad); }

template <class Base>
class Hint_InputT : public Base {
public:
    Hint_InputT(int X, int Y, int W, int H, const char *L = nullptr)
        : Base(X, Y, W, H, L) {}

    void hint(const std::string &h) { m_hint = h; this->redraw(); }
    const std::string &hint() const { return m_hint; }

    void draw() override {
        Base::draw();
        if (m_hint.empty()) return;

        const char *v = this->value();
        if (v && *v) return;          // something is typed: no hint to show

        fl_color(hint_colour());
        fl_font(this->textfont(), this->textsize());
        const int dx = Fl::box_dx(this->box()) + 2;
        fl_draw(m_hint.c_str(), this->x() + dx, this->y(), this->w() - 2 * dx, this->h(),
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }

private:
    std::string m_hint;
};

using Hint_Input       = Hint_InputT<Fl_Input>;
using Hint_Int_Input   = Hint_InputT<Fl_Int_Input>;
using Hint_Float_Input = Hint_InputT<Fl_Float_Input>;

// The same idea for the multi-line prompt boxes: they are Fl_Text_Editor, whose
// emptiness lives in its buffer rather than in a value() string.
template <class Base>
class Hint_EditorT : public Base {
public:
    Hint_EditorT(int X, int Y, int W, int H, const char *L = nullptr)
        : Base(X, Y, W, H, L) {}

    void hint(const std::string &h) { m_hint = h; this->redraw(); }
    const std::string &hint() const { return m_hint; }

    int handle(int event) override {
        if (event == FL_MOUSEWHEEL && !Fl::event_inside(this)) return 0;
        return Base::handle(event);
    }

    void draw() override {
        Base::draw();
        if (m_hint.empty()) return;

        const Fl_Text_Buffer *b = this->buffer();
        if (b && b->length() > 0) return;

        fl_color(hint_colour());
        fl_font(this->textfont(), this->textsize());
        const int d = Fl::box_dx(this->box()) + 3;
        fl_draw(m_hint.c_str(), this->x() + d, this->y() + d,
                this->w() - 2 * d, this->h() - 2 * d,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
    }

private:
    std::string m_hint;
};

using Hint_Text_Editor = Hint_EditorT<Fl_Text_Editor>;

// The reference-image list is a browser rather than a field, so its hint shows
// while the list still has no rows.
template <class Base>
class Hint_Browser_T : public Base {
public:
    Hint_Browser_T(int X, int Y, int W, int H, const char *L = nullptr)
        : Base(X, Y, W, H, L) {}

    void hint(const std::string &h) { m_hint = h; this->redraw(); }
    const std::string &hint() const { return m_hint; }

    int handle(int event) override {
        if (event == FL_MOUSEWHEEL && !Fl::event_inside(this)) return 0;
        return Base::handle(event);
    }

    void draw() override {
        Base::draw();
        if (m_hint.empty() || this->size() > 0) return;

        fl_color(hint_colour());
        fl_font(this->textfont(), this->textsize());
        const int d = 6;
        fl_draw(m_hint.c_str(), this->x() + d, this->y() + d,
                this->w() - 2 * d, this->h() - 2 * d,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
    }

private:
    std::string m_hint;
};

using Hint_Browser = Hint_Browser_T<Fl_Hold_Browser>;
