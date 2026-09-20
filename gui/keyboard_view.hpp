/*---------------------------------------------------------*\
| keyboard_view.hpp - keyboard canvas widget                |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   Draws a layout as a real keyboard picture and, when      |
|   editable, turns mouse input into per-key painting.       |
\*---------------------------------------------------------*/

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>

#include "claymore/color.hpp"
#include "claymore/effects.hpp"
#include "claymore/layout.hpp"

namespace claymore
{
    namespace gui
    {
        enum class Brush
        {
            Paint,
            Erase,
            Picker,
        };

        class KeyboardView
        {
        public:
            KeyboardView();
            ~KeyboardView();

            KeyboardView(const KeyboardView&)            = delete;
            KeyboardView& operator=(const KeyboardView&) = delete;

            GtkWidget* widget() const { return area; }

            void set_layout(const LayoutVariant* variant);
            const LayoutVariant* layout() const { return active_layout; }

            void set_frame(Frame frame);
            const Frame& frame() const { return current_frame; }

            void set_editable(bool value) { editable = value; }
            void set_brush(Brush value) { active_brush = value; }
            void set_brush_color(const Rgb& color) { brush_color = color; }
            void set_show_labels(bool value) { show_labels = value; }

            /*-------------------------------------------------*\
            | Called whenever the user paints, erases or picks a |
            | colour.                                            |
            \*-------------------------------------------------*/
            std::function<void(int slot, const Rgb& color)> on_paint;
            std::function<void(const std::string& message)> on_message;

        private:
            static gboolean draw_cb(GtkWidget* widget, cairo_t* cr, gpointer data);
            static gboolean press_cb(GtkWidget* widget, GdkEventButton* event, gpointer data);
            static gboolean release_cb(GtkWidget* widget, GdkEventButton* event, gpointer data);
            static gboolean motion_cb(GtkWidget* widget, GdkEventMotion* event, gpointer data);
            static gboolean leave_cb(GtkWidget* widget, GdkEventCrossing* event, gpointer data);

            void key_rect(int row, int col, double& x, double& y, double& width, double& height) const;
            int  slot_at(double x, double y) const;
            void apply_brush_at(double x, double y);

            GtkWidget*           area           = nullptr;
            const LayoutVariant* active_layout  = nullptr;
            Frame                current_frame;
            Brush                active_brush   = Brush::Paint;
            Rgb                  brush_color    = Rgb(0xFF, 0x00, 0x3C);
            bool                 editable       = false;
            bool                 show_labels    = true;
            bool                 dragging       = false;
            int                  hover_slot     = -1;
            int                  last_painted   = -1;
        };
    }
}
