/*---------------------------------------------------------*\
| keyboard_view.cpp - keyboard canvas widget                |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "keyboard_view.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace claymore
{
    namespace gui
    {
        namespace
        {
            /*---------------------------------------------------------*\
            | Opt-in trace for the click path. Pointing a debugger at a   |
            | live GtkDrawingArea is awkward, and "clicking does nothing"  |
            | can mean the event never arrived, the key lookup failed, or  |
            | the callback never wrote the colour. Set CLAYMORE_GUI_TRACE  |
            | to see which one it is.                                      |
            \*---------------------------------------------------------*/
            bool tracing()
            {
                static const bool enabled = (std::getenv("CLAYMORE_GUI_TRACE") != nullptr);
                return enabled;
            }

            void trace(const char* format, ...)
            {
                if(!tracing())
                {
                    return;
                }

                va_list args;
                va_start(args, format);
                std::fputs("[trace] ", stderr);
                std::vfprintf(stderr, format, args);
                std::fputc('\n', stderr);
                va_end(args);
            }

            /*---------------------------------------------*\
            | Grid constants, shared with the hit test in    |
            | layout.cpp. The drawing and the click handling  |
            | must agree, so they read the same values.       |
            \*---------------------------------------------*/
            constexpr KeyGrid GRID{};
            constexpr double  RADIUS = 4.0;

            /*---------------------------------------------*\
            | How far outside a key a click still counts.     |
            | The gaps are only a few pixels wide, and a      |
            | click that lands in one would otherwise look    |
            | exactly like the canvas ignoring the user.      |
            \*---------------------------------------------*/
            constexpr double SNAP = 5.0;

            void rounded_rect(cairo_t* cr, double x, double y, double width, double height, double radius)
            {
                const double r = std::min(radius, std::min(width, height) / 2.0);

                cairo_new_sub_path(cr);
                cairo_arc(cr, x + width - r,  y + r,          r, -M_PI / 2.0, 0.0);
                cairo_arc(cr, x + width - r,  y + height - r, r, 0.0,         M_PI / 2.0);
                cairo_arc(cr, x + r,          y + height - r, r, M_PI / 2.0,  M_PI);
                cairo_arc(cr, x + r,          y + r,          r, M_PI,        3.0 * M_PI / 2.0);
                cairo_close_path(cr);
            }

            double luminance(const Rgb& color)
            {
                return (0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b) / 255.0;
            }

            /*---------------------------------------------*\
            | Short label so long names still fit in a key.  |
            \*---------------------------------------------*/
            std::string short_label(const char* name)
            {
                if(name == nullptr)
                {
                    return {};
                }

                std::string text = name;
                /*-----------------------------------------*\
                | "Left Shift" reads better as "L Shift".     |
                \*-----------------------------------------*/
                const auto replace = [&text](const std::string& from, const std::string& to)
                {
                    if(text.rfind(from, 0) == 0)
                    {
                        text = to + text.substr(from.size());
                    }
                };

                replace("Left ",  "L ");
                replace("Right ", "R ");
                replace("Iso ",   "");
                replace("Back Tick", "`");
                replace("Scroll Lock", "ScrLk");
                replace("Print Screen", "PrtSc");
                replace("Page Up", "PgUp");
                replace("Page Down", "PgDn");
                replace("Caps Lock", "Caps");
                replace("Backspace", "Bksp");
                replace("Forward Slash", "/");
                replace("Semicolon", ";");
                replace("Quote", "'");
                replace("Comma", ",");
                replace("Period", ".");
                replace("Equals", "=");
                replace("Minus", "-");
                replace("Space", "Space");
                replace("Escape", "Esc");
                replace("Windows", "Win");
                replace("Function", "Fn");
                replace("Delete", "Del");
                replace("Insert", "Ins");
                replace("Left Bracket", "[");
                replace("Right Bracket", "]");
                replace("Back Slash", "\\");
                return text;
            }
        }

        KeyboardView::KeyboardView()
        {
            area = gtk_drawing_area_new();
            gtk_widget_set_size_request(area, 320, 160);
            gtk_widget_set_hexpand(area, TRUE);
            gtk_widget_set_vexpand(area, TRUE);
            gtk_widget_set_has_tooltip(area, TRUE);

            gtk_widget_add_events(area,
                GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);

            g_signal_connect(area, "draw",                  G_CALLBACK(draw_cb),    this);
            g_signal_connect(area, "button-press-event",    G_CALLBACK(press_cb),   this);
            g_signal_connect(area, "button-release-event",  G_CALLBACK(release_cb), this);
            g_signal_connect(area, "motion-notify-event",   G_CALLBACK(motion_cb),  this);
            g_signal_connect(area, "leave-notify-event",    G_CALLBACK(leave_cb),   this);
        }

        KeyboardView::~KeyboardView() = default;

        void KeyboardView::set_layout(const LayoutVariant* variant)
        {
            active_layout = variant;
            current_frame.assign(static_cast<std::size_t>(variant ? variant->key_count : 0), Rgb(0, 0, 0));
            hover_slot = -1;
            gtk_widget_queue_draw(area);

            trace("view %p set_layout %s (%d keys), editable=%d",
                  static_cast<void*>(this),
                  variant ? variant->id : "null",
                  variant ? variant->key_count : 0,
                  editable ? 1 : 0);
        }

        void KeyboardView::set_frame(Frame frame)
        {
            current_frame = std::move(frame);
            gtk_widget_queue_draw(area);
        }

        void KeyboardView::key_rect(int row, int col, double& x, double& y,
                                    double& width, double& height) const
        {
            if(active_layout == nullptr)
            {
                x = y = width = height = 0.0;
                return;
            }

            const KeyRect rect = claymore::key_rect(*active_layout, row, col,
                                                    gtk_widget_get_allocated_width(area),
                                                    gtk_widget_get_allocated_height(area),
                                                    GRID);
            x      = rect.x;
            y      = rect.y;
            width  = rect.width;
            height = rect.height;
        }

        int KeyboardView::slot_at(double px, double py) const
        {
            if(active_layout == nullptr)
            {
                return -1;
            }

            return hit_test_slot(*active_layout, px, py,
                                 gtk_widget_get_allocated_width(area),
                                 gtk_widget_get_allocated_height(area),
                                 GRID, SNAP);
        }

        void KeyboardView::apply_brush_at(double px, double py)
        {
            trace("view %p brush_at %.1f,%.1f editable=%d layout=%s",
                  static_cast<void*>(this), px, py, editable ? 1 : 0,
                  active_layout ? active_layout->id : "null");

            if(!editable || active_layout == nullptr)
            {
                return;
            }

            const int slot = slot_at(px, py);
            trace("  slot_at -> %d (alloc %dx%d), brush=%d", slot,
                  gtk_widget_get_allocated_width(area), gtk_widget_get_allocated_height(area),
                  static_cast<int>(active_brush));
            if(slot < 0 || slot == last_painted)
            {
                return;
            }

            last_painted = slot;

            switch(active_brush)
            {
                case Brush::Paint:
                    if(on_paint) on_paint(slot, brush_color);
                    break;

                case Brush::Erase:
                    if(on_paint) on_paint(slot, Rgb(0, 0, 0));
                    break;

                case Brush::Picker:
                {
                    if(static_cast<std::size_t>(slot) < current_frame.size())
                    {
                        const Rgb picked = current_frame[static_cast<std::size_t>(slot)];
                        brush_color = picked;
                        if(on_message)
                        {
                            on_message("picked " + picked.to_hex() + " from " +
                                       active_layout->keys[slot].name);
                        }
                    }
                    break;
                }
            }
        }

        gboolean KeyboardView::draw_cb(GtkWidget*, cairo_t* cr, gpointer data)
        {
            KeyboardView* self = static_cast<KeyboardView*>(data);

            /*---------------------------------------------*\
            | Backdrop.                                      |
            \*---------------------------------------------*/
            gtk_widget_get_style_context(self->area);
            cairo_set_source_rgb(cr, 0.11, 0.12, 0.14);
            cairo_paint(cr);

            if(self->active_layout == nullptr)
            {
                cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, 13);
                cairo_move_to(cr, 16, 30);
                cairo_show_text(cr, "No keyboard connected");
                return TRUE;
            }

            const LayoutVariant& variant = *self->active_layout;

            for(int row = 0; row < variant.rows; row++)
            {
                for(int col = 0; col < variant.cols; col++)
                {
                    const int slot = variant.matrix[row * variant.cols + col];
                    if(slot < 0)
                    {
                        continue;
                    }

                    double x = 0, y = 0, w = 0, h = 0;
                    self->key_rect(row, col, x, y, w, h);

                    Rgb color(0, 0, 0);
                    if(static_cast<std::size_t>(slot) < self->current_frame.size())
                    {
                        color = self->current_frame[static_cast<std::size_t>(slot)];
                    }

                    /*-----------------------------------------*\
                    | Key body. Keys are drawn a hair brighter   |
                    | than their colour so that a dark design    |
                    | still shows where the keys are.            |
                    \*-----------------------------------------*/
                    const double lift = color.is_black() ? 0.10 : 0.06;
                    cairo_set_source_rgb(cr,
                        std::min(1.0, color.r / 255.0 + lift),
                        std::min(1.0, color.g / 255.0 + lift),
                        std::min(1.0, color.b / 255.0 + lift));
                    rounded_rect(cr, x, y, w, h, RADIUS);
                    cairo_fill(cr);

                    /*-----------------------------------------*\
                    | Outline, brightened when hovered.          |
                    \*-----------------------------------------*/
                    if(slot == self->hover_slot && self->editable)
                    {
                        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                        cairo_set_line_width(cr, 2.0);
                    }
                    else
                    {
                        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
                        cairo_set_line_width(cr, 1.0);
                    }
                    rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, RADIUS);
                    cairo_stroke(cr);

                    /*-----------------------------------------*\
                    | Label, only when there is room for one.    |
                    \*-----------------------------------------*/
                    if(self->show_labels && w >= 26.0 && h >= 16.0)
                    {
                        const std::string label = short_label(variant.keys[slot].name);
                        if(!label.empty())
                        {
                            cairo_select_font_face(cr, "Sans",
                                                   CAIRO_FONT_SLANT_NORMAL,
                                                   CAIRO_FONT_WEIGHT_NORMAL);
                            cairo_set_font_size(cr, std::clamp(h * 0.38, 7.0, 11.0));

                            cairo_text_extents_t extents;
                            cairo_text_extents(cr, label.c_str(), &extents);

                            if(extents.width <= w - 4.0)
                            {
                                const bool dark_text = luminance(color) > 0.55;
                                if(dark_text)
                                {
                                    cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, 0.85);
                                }
                                else
                                {
                                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.80);
                                }

                                cairo_move_to(cr,
                                    x + (w - extents.width) / 2.0 - extents.x_bearing,
                                    y + (h - extents.height) / 2.0 - extents.y_bearing);
                                cairo_show_text(cr, label.c_str());
                            }
                        }
                    }
                }
            }

            return TRUE;
        }

        gboolean KeyboardView::press_cb(GtkWidget*, GdkEventButton* event, gpointer data)
        {
            KeyboardView* self = static_cast<KeyboardView*>(data);

            trace("view %p press button=%u at %.1f,%.1f editable=%d",
                  static_cast<void*>(self), event->button, event->x, event->y,
                  self->editable ? 1 : 0);

            if(!self->editable)
            {
                return FALSE;
            }

            /*---------------------------------------------*\
            | Middle click always erases, whatever the brush |
            | is set to, which is the convention in painting |
            | tools.                                         |
            \*---------------------------------------------*/
            if(event->button == 2)
            {
                const Brush previous = self->active_brush;
                self->active_brush = Brush::Erase;
                self->dragging = true;
                self->last_painted = -1;
                self->apply_brush_at(event->x, event->y);
                self->active_brush = previous;
                self->dragging = false;
                return TRUE;
            }

            if(event->button != 1)
            {
                return FALSE;
            }

            self->dragging = true;
            self->last_painted = -1;
            self->apply_brush_at(event->x, event->y);
            return TRUE;
        }

        gboolean KeyboardView::release_cb(GtkWidget*, GdkEventButton*, gpointer data)
        {
            KeyboardView* self = static_cast<KeyboardView*>(data);
            self->dragging = false;
            self->last_painted = -1;
            return TRUE;
        }

        gboolean KeyboardView::motion_cb(GtkWidget*, GdkEventMotion* event, gpointer data)
        {
            KeyboardView* self = static_cast<KeyboardView*>(data);

            const int slot = self->slot_at(event->x, event->y);

            if(slot != self->hover_slot)
            {
                self->hover_slot = slot;

                if(slot >= 0 && self->active_layout != nullptr)
                {
                    /*-------------------------------------*\
                    | Show the key identity and its indices  |
                    | right on the canvas.                   |
                    \*-------------------------------------*/
                    const KeyName& key = self->active_layout->keys[slot];
                    char text[192];
                    std::snprintf(text, sizeof(text), "%s  slot %d  led 0x%02X",
                                  key.name, slot, key.index);
                    gtk_widget_set_tooltip_text(self->area, text);
                }

                gtk_widget_queue_draw(self->area);
            }

            if(self->dragging)
            {
                self->apply_brush_at(event->x, event->y);
            }

            return TRUE;
        }

        gboolean KeyboardView::leave_cb(GtkWidget*, GdkEventCrossing*, gpointer data)
        {
            KeyboardView* self = static_cast<KeyboardView*>(data);
            self->hover_slot = -1;
            self->dragging = false;
            self->last_painted = -1;
            gtk_widget_queue_draw(self->area);
            return TRUE;
        }
    }
}
