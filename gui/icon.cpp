/*---------------------------------------------------------*\
| icon.cpp - the ROG emblem                                 |
|                                                           |
|   Part of Claymore@Linux                                  |
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

            /*------------------------------------------------*\
            | The emblem's outline, in 512 unit space. It was|
            | traced from the red mask of the source bitmap: |
            | three closed contours - the feather, the middle|
            | cluster and the blade - each a chain of cubic  |
            | Beziers, and the straight bits are degenerate  |
            | cubics whose control points sit on the chord.  |
            |                                                |
            | assets/claymore-linux.svg holds the very same  |
            | numbers, so 'make verify-install' fails if the |
            | two ever drift apart.                          |
            \*------------------------------------------------*/

            struct Seg
            {
                double x1, y1; // first control point
                double x2, y2; // second control point
                double x3, y3; // end point
            };

            struct Path
            {
                double     x0, y0; // start point
                const Seg* segs;
                int        n;
            };

            constexpr Seg EMBLEM_SEGS_0[] =
            {
                {     23.90,     241.89,      28.08,     255.47,      32.32,     269.16 },
                {     35.90,     277.12,      39.81,     284.83,      45.79,     291.80 },
                {     53.26,     299.28,      61.85,     304.33,      70.26,     309.55 },
                {     81.92,     314.75,      93.02,     321.38,     105.15,     325.46 },
                {     92.08,     308.41,      75.53,     292.62,      61.70,     275.89 },
                {     47.66,     260.28,      34.94,     242.10,      21.31,     227.54 },
            };

            constexpr Seg EMBLEM_SEGS_1[] =
            {
                {    464.41,     158.04,     439.99,     170.77,     414.81,     181.64 },
                {    382.49,     197.76,     350.42,     214.17,     318.73,     231.83 },
                {    302.41,     241.41,     286.09,     251.00,     269.77,     260.59 },
                {    253.45,     270.99,     237.13,     281.40,     220.81,     291.80 },
                {    201.92,     305.77,     180.30,     317.81,     163.29,     333.42 },
                {    179.97,     343.09,     198.51,     348.35,     216.53,     354.83 },
                {    232.42,     359.39,     248.64,     363.30,     264.87,     367.07 },
                {    287.14,     370.92,     309.62,     373.95,     332.80,     375.64 },
                {    338.92,     375.64,     345.04,     375.64,     351.16,     375.64 },
                {    354.83,     374.42,     358.51,     373.19,     362.18,     371.97 },
                {    371.68,     366.48,     380.26,     359.16,     388.49,     350.55 },
                {    400.38,     336.86,     410.15,     322.37,     419.70,     307.71 },
                {    430.65,     289.18,     440.04,     270.11,     449.08,     250.80 },
                {    456.20,     232.27,     464.21,     214.08,     468.66,     194.50 },
                {    437.17,     207.99,     407.63,     226.34,     376.87,     241.62 },
                {    356.06,     253.45,     335.25,     265.28,     314.44,     277.11 },
                {    293.84,     289.15,     273.24,     301.18,     252.63,     313.22 },
                {    252.34,     316.57,     256.12,     312.67,     257.53,     313.22 },
                {    280.99,     303.02,     304.45,     292.82,     327.91,     282.62 },
                {    351.37,     272.83,     374.83,     263.04,     398.29,     253.25 },
                {    391.51,     272.75,     379.81,     292.51,     369.52,     311.38 },
                {    366.26,     315.67,     362.99,     319.95,     359.73,     324.24 },
                {    354.81,     329.70,     349.42,     333.86,     343.82,     337.70 },
                {    336.50,     341.61,     328.68,     344.30,     320.56,     346.27 },
                {    312.09,     347.43,     303.30,     347.71,     294.25,     347.49 },
                {    279.99,     346.12,     266.37,     343.39,     252.63,     340.76 },
                {    238.84,     336.55,     224.24,     334.22,     211.02,     328.52 },
                {    228.05,     315.47,     246.02,     304.59,     263.65,     293.02 },
                {    280.99,     282.42,     298.33,     271.81,     315.67,     261.20 },
                {    343.01,     246.29,     369.82,     230.25,     397.67,     216.53 },
                {    424.38,     201.62,     451.81,     188.40,     479.07,     174.91 },
                {    483.21,     171.50,     485.49,     167.51,     487.63,     163.29 },
                {    489.04,     158.96,     490.40,     154.61,     490.69,     149.82 },
            };

            constexpr Seg EMBLEM_SEGS_2[] =
            {
                {    416.24,     120.85,     400.53,     122.49,     384.82,     124.12 },
                {    372.99,     126.77,     361.16,     129.42,     349.33,     132.07 },
                {    337.62,     136.22,     325.46,     139.08,     314.44,     144.93 },
                {    305.87,     150.38,     298.07,     157.88,     289.96,     164.51 },
                {    267.49,     185.58,     244.71,     208.78,     223.87,     228.77 },
                {    201.55,     250.44,     179.40,     272.86,     155.94,     291.80 },
                {    149.73,     294.40,     143.62,     291.87,     137.58,     290.58 },
                {    122.69,     282.62,     107.80,     274.67,      92.91,     266.71 },
                {     90.55,     267.13,      84.26,     262.03,      83.73,     265.49 },
                {     96.43,     282.78,     104.02,     302.24,     114.33,     320.56 },
                {    120.56,     330.28,     126.81,     339.97,     133.91,     349.33 },
                {    141.47,     357.99,     149.40,     366.61,     158.39,     373.81 },
                {    168.75,     381.23,     179.12,     388.66,     190.82,     392.78 },
                {    182.64,     379.98,     169.78,     368.91,     160.23,     356.67 },
                {    156.44,     351.43,     153.06,     346.12,     151.05,     340.15 },
                {    150.34,     335.21,     152.29,     331.79,     154.11,     327.91 },
                {    158.39,     321.59,     164.20,     315.85,     170.02,     310.16 },
                {    197.47,     286.58,     225.14,     263.00,     253.25,     241.01 },
                {    277.52,     222.80,     302.20,     205.80,     327.30,     189.60 },
                {    352.78,     174.19,     379.01,     160.07,     405.63,     147.37 },
                {    429.03,     136.57,     455.65,     128.42,     478.45,     119.22 },
                {    462.95,     119.22,     447.45,     119.22,     431.94,     119.22 },
            };

            constexpr Path EMBLEM_PATHS[] =
            {
                {  21.31, 227.54, EMBLEM_SEGS_0,  6 },
                { 490.69, 149.82, EMBLEM_SEGS_1, 33 },
                { 431.94, 119.22, EMBLEM_SEGS_2, 22 },
            };

            /*------------------------------------------------*\
            | The three contours as one path, scaled from 512|
            | units to `s` pixels; one fill paints them all. |
            \*------------------------------------------------*/
            void draw_emblem(cairo_t* cr, double s)
            {
                const double k = s / 512.0;

                for(const Path& p : EMBLEM_PATHS)
                {
                    cairo_move_to(cr, p.x0 * k, p.y0 * k);

                    for(int i = 0; i < p.n; ++i)
                    {
                        const Seg& g = p.segs[i];

                        cairo_curve_to(cr, g.x1 * k, g.y1 * k,
                                       g.x2 * k, g.y2 * k,
                                       g.x3 * k, g.y3 * k);
                    }
                    cairo_close_path(cr);
                }
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

            /*-------------------------------------------*\
            | The emblem, in the Republic of Gamers red.|
            \*-------------------------------------------*/
            cairo_set_source_rgb(cr, ROG_RED_R, ROG_RED_G, ROG_RED_B);
            draw_emblem(cr, s);
            cairo_fill(cr);

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
