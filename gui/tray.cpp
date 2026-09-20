/*---------------------------------------------------------*\
| tray.cpp - the panel icon                                 |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "tray.hpp"

#include "app.hpp"
#include "icon.hpp"

/*---------------------------------------------------------*\
| GtkStatusIcon has been deprecated since GTK 3.14 in favour |
| of libappindicator/StatusNotifierItem. On an X11 desktop the |
| status icon still works everywhere, including on panels     |
| that only speak the older protocol, and libappindicator is  |
| not installed by default. Since the whole point of this     |
| program is to work without extra dependencies, the          |
| deprecated API is the right choice here - and the           |
| deprecation warning is silenced rather than left to scroll  |
| past on every build.                                       |
\*---------------------------------------------------------*/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace claymore
{
    namespace gui
    {
        namespace
        {
            /*-------------------------------------------------*\
            | The window and the panel icon are the same drawing, |
            | so they can never drift apart.                      |
            \*-------------------------------------------------*/
            GdkPixbuf* tray_pixbuf()
            {
                return rog_emblem(48);
            }

            void menu_show_cb(GtkMenuItem*, gpointer data)
            {
                show_window(*static_cast<AppState*>(data));
            }

            void menu_release_cb(GtkMenuItem*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                /*---------------------------------------------*\
                | Explicitly take the design off the keyboard and |
                | hand it back to its firmware. This is the way  |
                | out of "my keyboard is stuck on my design".    |
                \\*---------------------------------------------*/
                app->stream_custom = false;
                app->design_kept   = false;

                if(app->stream_check != nullptr)
                {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->stream_check), FALSE);
                }

                if(app->device != nullptr)
                {
                    std::string error;
                    if(app->device->release(error))
                    {
                        set_status(*app, "handed the keyboard back to its own effects");
                    }
                    else
                    {
                        set_status(*app, "could not release the keyboard: " + error);
                    }
                }
                else
                {
                    set_status(*app, "no keyboard connected");
                }
            }

            void menu_refresh_cb(GtkMenuItem*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                /*---------------------------------------------*\
                | Put the design back. Useful after something    |
                | else on the machine grabbed the lighting, and  |
                | the honest answer to "it stopped working".     |
                \\*---------------------------------------------*/
                app->stream_custom = true;
                app->design_edited = true;

                if(app->stream_check != nullptr)
                {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->stream_check), TRUE);
                }

                set_status(*app, "re-applying your design to the keyboard");
            }

            void menu_quit_cb(GtkMenuItem*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                /*---------------------------------------------*\
                | Quitting normally keeps the design: the         |
                | keyboard holds its last frame, and the shutdown |
                | path in main_gui.cpp saves the design for the   |
                | next login.                                     |
                \\*---------------------------------------------*/
                if(app->window != nullptr)
                {
                    GApplication* application = G_APPLICATION(g_application_get_default());
                    if(application != nullptr && g_application_get_is_registered(application))
                    {
                        g_application_quit(application);
                        return;
                    }
                }

                gtk_main_quit();
            }

            void status_icon_activate_cb(GtkStatusIcon*, gpointer data)
            {
                show_window(*static_cast<AppState*>(data));
            }

            void status_icon_popup_cb(GtkStatusIcon*, guint, guint, gpointer data)
            {
                AppState* app  = static_cast<AppState*>(data);
                GtkWidget* menu = GTK_WIDGET(g_object_get_data(G_OBJECT(app->tray_icon),
                                                               "claymore-menu"));

                if(menu == nullptr)
                {
                    return;
                }

                /*---------------------------------------------*\
                | Let the panel place the menu around its icon   |
                | rather than guessing where the pointer is.     |
                \*---------------------------------------------*/
                gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr,
                               gtk_status_icon_position_menu, app->tray_icon,
                               0, gtk_get_current_event_time());
            }

            GtkWidget* build_menu(AppState& app)
            {
                GtkWidget* menu = gtk_menu_new();

                GtkWidget* show = gtk_menu_item_new_with_label("Show the window");
                g_signal_connect(show, "activate", G_CALLBACK(menu_show_cb), &app);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), show);

                GtkWidget* refresh = gtk_menu_item_new_with_label("Re-apply my design");
                g_signal_connect(refresh, "activate", G_CALLBACK(menu_refresh_cb), &app);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh);

                GtkWidget* release = gtk_menu_item_new_with_label("Hand the keyboard back");
                g_signal_connect(release, "activate", G_CALLBACK(menu_release_cb), &app);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), release);

                gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

                GtkWidget* quit = gtk_menu_item_new_with_label("Quit and keep the design");
                g_signal_connect(quit, "activate", G_CALLBACK(menu_quit_cb), &app);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);

                gtk_widget_show_all(menu);
                return menu;
            }
        }

        void tray_show(AppState& app)
        {
            if(app.tray_icon != nullptr)
            {
                gtk_status_icon_set_visible(GTK_STATUS_ICON(app.tray_icon), TRUE);
                return;
            }

            GtkStatusIcon* icon = gtk_status_icon_new();

            /*---------------------------------------------*\
            | The local menu has to be built once and then    |
            | attached on every popup: a GtkMenu cannot be    |
            | shared between windows.                        |
            \*---------------------------------------------*/
            GtkWidget* menu = build_menu(app);

            gtk_status_icon_set_tooltip_text(icon, "ROG Claymore");
            gtk_status_icon_set_visible(icon, TRUE);

            g_signal_connect(icon, "activate", G_CALLBACK(status_icon_activate_cb), &app);

            g_object_set_data_full(G_OBJECT(icon), "claymore-menu", menu, g_object_unref);

            g_signal_connect(icon, "popup-menu", G_CALLBACK(status_icon_popup_cb), &app);

            if(GdkPixbuf* pixbuf = tray_pixbuf())
            {
                gtk_status_icon_set_from_pixbuf(icon, pixbuf);
                g_object_unref(pixbuf);
            }
            else
            {
                gtk_status_icon_set_from_icon_name(icon, "input-keyboard");
            }

            app.tray_icon = icon;
        }

        void tray_hide(AppState& app)
        {
            if(app.tray_icon == nullptr)
            {
                return;
            }

            g_object_unref(app.tray_icon);
            app.tray_icon = nullptr;
        }

        void show_window(AppState& app)
        {
            if(app.window == nullptr)
            {
                return;
            }

            gtk_widget_show_all(app.window);
            gtk_window_deiconify(GTK_WINDOW(app.window));
            gtk_window_present(GTK_WINDOW(app.window));
        }

        namespace
        {
            /*-------------------------------------------------*\
            | Hides the window once the icon has actually landed  |
            | in a panel.                                         |
            |                                                    |
            | A desktop with no system tray (GNOME without an     |
            | extension, for instance) would otherwise leave the  |
            | program running with no window and no icon: a       |
            | process the user cannot see, reach or quit.         |
            \*-------------------------------------------------*/
            gboolean confirm_embedded_cb(gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(app->tray_icon != nullptr &&
                   gtk_status_icon_is_embedded(GTK_STATUS_ICON(app->tray_icon)))
                {
                    if(app->window != nullptr)
                    {
                        gtk_widget_hide(app->window);
                    }

                    set_status(*app, "running in the panel; the design stays on the keyboard, "
                                     "click the icon to come back");
                    return G_SOURCE_REMOVE;
                }

                /*---------------------------------------------*\
                | No panel to hold it. Put things back the way   |
                | they were and say why.                         |
                \*---------------------------------------------*/
                tray_hide(*app);
                show_window(*app);
                set_status(*app, "this desktop has no panel tray, so the window stays open; "
                                 "the design is kept on the keyboard either way");
                return G_SOURCE_REMOVE;
            }
        }

        void hide_to_tray(AppState& app)
        {
            tray_show(app);

            if(app.tray_icon == nullptr)
            {
                return;
            }

            /*---------------------------------------------*\
            | Already in a panel? Then there is nothing to    |
            | wait for.                                       |
            \*---------------------------------------------*/
            if(gtk_status_icon_is_embedded(GTK_STATUS_ICON(app.tray_icon)))
            {
                if(app.window != nullptr)
                {
                    gtk_widget_hide(app.window);
                }

                set_status(app, "running in the panel; the design stays on the keyboard, "
                                "click the icon to come back");
                return;
            }

            g_timeout_add(600, confirm_embedded_cb, &app);
        }
    }
}

#pragma GCC diagnostic pop
