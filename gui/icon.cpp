/*---------------------------------------------------------*\
| icon.cpp - the ROG style eye emblem                       |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "icon.hpp"

#include <algorithm>
#include <cmath>

namespace claymore
{
    namespace gui
    {
        namespace
        {
            /*-------------------------------------------------*\
            | Republic of Gamers red, and the near black plate.   |
            \*-------------------------------------------------*/
            constexpr double ROG_RED_R = 1.00;
            constexpr double ROG_RED_G = 0.00;
            constexpr double ROG_RED_B = 0.24;

            constexpr double PLATE_R = 0.07;
            constexpr double PLATE_G = 0.07;
            constexpr double PLATE_B = 0.09;

            /*-------------------------------------------------*\
            | Everything below is in fractions of the icon, so    |
            | that one drawing serves a 16 pixel panel icon and a |
            | 256 pixel window icon.                             |
            |                                                    |
            | The eye is tilted - it rises to the right - which   |
            | is what makes it read as the gaming eye rather than |
            | as a plain almond, and the tilt is what survives at |
            | small sizes when the internal detail does not.      |
            \*-------------------------------------------------*/
            struct Pt
            {
                double x;
                double y;
            };

            constexpr Pt EYE_LEFT  = { 0.040, 0.640 };
            constexpr Pt EYE_RIGHT = { 0.960, 0.360 };

            /*-------------------------------------------------*\
            | Perpendicular to the eye's axis, pointing up the    |
            | screen. Unit length: the axis is (0.910, -0.250),   |
            | which measures 0.9437.                              |
            \*-------------------------------------------------*/
            constexpr Pt EYE_UP = { -0.2649, -0.9643 };

            /*-------------------------------------------------*\
            | How fat the eye is at its middle, where the two     |
            | gaps sit, and how thick the gaps are.               |
            \*-------------------------------------------------*/
            constexpr double EYE_BULGE  = 0.285;
            constexpr double GAP_OFFSET = 0.100;
            constexpr double GAP_BULGE  = 0.024;

            /*-------------------------------------------------*\
            | How far along the axis the gaps run. Stopping short  |
            | of the ends is what leaves the tips joined, so the  |
            | silhouette stays a single pointed eye and the       |
            | stripes converge instead of floating apart.        |
            \*-------------------------------------------------*/
            constexpr double GAP_FROM = 0.055;
            constexpr double GAP_TO   = 0.945;

            Pt along(Pt a, Pt b, double t)
            {
                return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
            }

            /*-------------------------------------------------*\
            | One lens: two arcs meeting at sharp points, offset  |
            | from the eye's axis and `bulge` deep at the middle. |
            |                                                    |
            | A cubic whose control points sit bulge/0.75 off the |
            | chord passes bulge from the chord at its midpoint,  |
            | which is what keeps `bulge` meaningful here rather  |
            | than being at the mercy of the control points.      |
            \*-------------------------------------------------*/
            void lens(cairo_t* cr, Pt a, Pt b, double offset, double bulge, double s)
            {
                const double k = bulge / 0.75;

                const Pt near = { a.x + EYE_UP.x * offset, a.y + EYE_UP.y * offset };
                const Pt far  = { b.x + EYE_UP.x * offset, b.y + EYE_UP.y * offset };

                const Pt n1 = along(a, b, 0.33);
                const Pt n2 = along(a, b, 0.67);

                const Pt up1 = { n1.x + EYE_UP.x * (offset + k), n1.y + EYE_UP.y * (offset + k) };
                const Pt up2 = { n2.x + EYE_UP.x * (offset + k), n2.y + EYE_UP.y * (offset + k) };
                const Pt dn2 = { n2.x + EYE_UP.x * (offset - k), n2.y + EYE_UP.y * (offset - k) };
                const Pt dn1 = { n1.x + EYE_UP.x * (offset - k), n1.y + EYE_UP.y * (offset - k) };

                cairo_new_sub_path(cr);
                cairo_move_to(cr, near.x * s, near.y * s);
                cairo_curve_to(cr, up1.x * s, up1.y * s, up2.x * s, up2.y * s, far.x * s, far.y * s);
                cairo_curve_to(cr, dn2.x * s, dn2.y * s, dn1.x * s, dn1.y * s, near.x * s, near.y * s);
                cairo_close_path(cr);
            }

            /*-------------------------------------------------*\
            | A rounded rectangle, which cairo does not have.     |
            \*-------------------------------------------------*/
            void rounded_rect(cairo_t* cr, double x, double y,
                              double width, double height, double radius)
            {
                const double r = std::min(radius, std::min(width, height) / 2.0);

                cairo_new_sub_path(cr);
                cairo_arc(cr, x + width - r,  y + r,          r, -M_PI / 2.0, 0.0);
                cairo_arc(cr, x + width - r,  y + height - r, r, 0.0,         M_PI / 2.0);
                cairo_arc(cr, x + r,          y + height - r, r, M_PI / 2.0,  M_PI);
                cairo_arc(cr, x + r,          y + r,          r, M_PI,        3.0 * M_PI / 2.0);
                cairo_close_path(cr);
            }
        }

        GdkPixbuf* rog_emblem(int size)
        {
            if(size <= 0)
            {
                return nullptr;
            }

            const double s = static_cast<double>(size);

            cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
            cairo_t*         cr      = cairo_create(surface);

            cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

            /*---------------------------------------------*\
            | Plate. Almost black rather than pure black so  |
            | the shape survives on a dark panel.            |
            \*---------------------------------------------*/
            cairo_set_source_rgb(cr, PLATE_R, PLATE_G, PLATE_B);
            rounded_rect(cr, 0.0, 0.0, s, s, s * 0.22);
            cairo_fill(cr);

            /*---------------------------------------------*\
            | The eye, as a solid lens to begin with.         |
            \*---------------------------------------------*/
            cairo_set_source_rgb(cr, ROG_RED_R, ROG_RED_G, ROG_RED_B);
            lens(cr, EYE_LEFT, EYE_RIGHT, 0.0, EYE_BULGE, s);
            cairo_fill(cr);

            /*---------------------------------------------*\
            | Then two gaps are painted in the plate colour,  |
            | which is what turns the lens into three tapered |
            | stripes: an upper lid, an iris and a lower lid. |
            |                                                 |
            | A fixed fraction would disappear at 16 pixels - |
            | three quarters of a pixel of black is just grey |
            | - so the gaps keep a minimum thickness in real  |
            | pixels and the icon reads as a red eye with     |
            | lines through it rather than as a solid blob.   |
            \*---------------------------------------------*/
            const double gap = std::max(GAP_BULGE, 0.55 / s);

            cairo_set_source_rgb(cr, PLATE_R, PLATE_G, PLATE_B);

            for(const double side : { +GAP_OFFSET, -GAP_OFFSET })
            {
                lens(cr, along(EYE_LEFT, EYE_RIGHT, GAP_FROM),
                          along(EYE_LEFT, EYE_RIGHT, GAP_TO),
                          side, gap, s);
                cairo_fill(cr);
            }

            GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);

            cairo_destroy(cr);
            cairo_surface_destroy(surface);

            return pixbuf;
        }

        void set_window_icon(GtkWindow* window)
        {
            if(window == nullptr)
            {
                return;
            }

            /*---------------------------------------------*\
            | Several sizes: window managers pick whatever   |
            | they need for the title bar, the task list and |
            | alt-tab, and a single large icon looks blurry  |
            | in the small places.                          |
            \*---------------------------------------------*/
            GList* icons = nullptr;

            for(int size : { 16, 24, 32, 48, 64, 128 })
            {
                if(GdkPixbuf* pixbuf = rog_emblem(size))
                {
                    icons = g_list_append(icons, pixbuf);
                }
            }

            if(icons != nullptr)
            {
                gtk_window_set_icon_list(window, icons);
                g_list_free_full(icons, g_object_unref);
            }
        }
    }
}
