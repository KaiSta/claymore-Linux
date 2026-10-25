/*---------------------------------------------------------*\
| tray.hpp - the panel icon                                 |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <gtk/gtk.h>

namespace claymore
{
    namespace gui
    {
        struct AppState;

        /*-----------------------------------------------------*\
        | Moving to the panel means the window goes away but the |
        | program does not: it keeps the design applied and the  |
        | previews ticking, which is what makes a design with    |
        | movement in it keep moving.                            |
        \*-----------------------------------------------------*/
        void tray_show(AppState& app);
        void tray_hide(AppState& app);

        /*-----------------------------------------------------*\
        | Puts the window away and shows the panel icon, with a  |
        | status line that says how to get it back. Used both by |
        | the button and by --tray at startup.                   |
        \*-----------------------------------------------------*/
        void hide_to_tray(AppState& app);
        void show_window(AppState& app);
    }
}
