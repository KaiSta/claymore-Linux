/*---------------------------------------------------------*\
| icon.hpp - the ROG style eye emblem                       |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <gtk/gtk.h>

namespace claymore
{
    namespace gui
    {
        /*-----------------------------------------------------*\
        | The ROG emblem, drawn with cairo in the Republic of     |
        | Gamers idiom: ROG red on a near black plate. Its three  |
        | contours - feather, middle cluster and blade - were     |
        | traced from the source bitmap. It is drawn from scratch |
        | rather than loaded, which is what lets the title bar,   |
        | the task bar and the panel icon be the same picture     |
        | with no files to install and no way for them to disagree|
        |                                                       |
        | assets/rog-claymore.svg is the same geometry as         |
        | vectors; 'make verify-install' fails if the two ever    |
        | drift apart.                                           |
        |                                                       |
        | The caller owns the returned pixbuf.                   |
        \*-----------------------------------------------------*/
        GdkPixbuf* rog_emblem(int size);

        void set_window_icon(GtkWindow* window);
    }
}
