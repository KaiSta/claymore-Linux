/*---------------------------------------------------------*\
| main_gui.cpp - the claymore-gui application               |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   One 30 Hz timer drives everything: the two live previews, |
|   the direct mode stream for a painted design, the sync     |
|   server poll and the debounced hardware apply. Keeping a   |
|   single timer means the preview and the keyboard can never |
|   disagree about where the animation is.                    |
\*---------------------------------------------------------*/

#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include <vector>

#include "claymore/design.hpp"
#include "claymore/hidraw.hpp"
#include "claymore/settings.hpp"
#include "icon.hpp"
#include "tray.hpp"

namespace claymore
{
    namespace gui
    {
        namespace
        {
            constexpr int TICK_MS = 33;
            constexpr int STREAM_ACK_TIMEOUT_MS = 2;

            gint64   last_tick_micros = 0;
            gboolean window_created   = FALSE;

            /*-------------------------------------------------*\
            | Called before the window's children are destroyed,  |
            | which is the only safe moment to stop work that      |
            | reaches into them.                                  |
            \*-------------------------------------------------*/
            void on_window_destroy(GtkWidget*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->running  = false;
            }

            gboolean on_tick(gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                /*-----------------------------------------*\
                | The window is gone: stop before touching   |
                | anything GTK owns.                         |
                \*-----------------------------------------*/
                if(!app->running)
                {
                    return G_SOURCE_REMOVE;
                }

                /*---------------------------------------------*\
                | Advance the shared clock so every preview uses |
                | the same instant.                              |
                \*---------------------------------------------*/
                const gint64 now_micros = g_get_monotonic_time();
                if(last_tick_micros == 0)
                {
                    last_tick_micros = now_micros;
                }
                const double delta = static_cast<double>(now_micros - last_tick_micros) / 1.0e6;
                last_tick_micros = now_micros;

                app->now += std::clamp(delta, 0.0, 0.25);

                if(app->layout == nullptr)
                {
                    return G_SOURCE_CONTINUE;
                }

                /*---------------------------------------------*\
                | Firmware effect preview                        |
                \*---------------------------------------------*/
                if(app->preview != nullptr)
                {
                    app->preview->set_frame(preview::render(app->spec, *app->layout, app->now));
                }

                /*---------------------------------------------*\
                | Custom design preview, and the stream that     |
                | mirrors it onto the hardware.                  |
                \*---------------------------------------------*/
                if(app->custom != nullptr)
                {
                    const Frame frame = current_custom_frame(*app);
                    app->custom->set_frame(frame);

                    /*-------------------------------------*\
                    | The design is streamed only once the   |
                    | user has actually edited it. Holding   |
                    | direct mode pauses the firmware        |
                    | effects, so claiming the board as soon |
                    | as this tab is built would make the    |
                    | Effects tab look broken. The first     |
                    | click on a key is the request.         |
                    \*-------------------------------------*/
                    const bool want_stream = app->stream_custom && app->design_edited && !frame.empty();

                    if(!want_stream)
                    {
                        app->streaming_now = false;
                    }
                    else if(app->device == nullptr)
                    {
                        /*---------------------------------*\
                        | Only complain when the board has   |
                        | been taken away from under us.     |
                        | Painting before connecting is      |
                        | harmless, and must not switch the  |
                        | user's preference off.             |
                        \*---------------------------------*/
                        if(app->streaming_now)
                        {
                            app->stream_custom = false;
                            if(app->stream_check != nullptr)
                            {
                                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->stream_check), FALSE);
                            }
                            set_status(*app, "keyboard disconnected, streaming stopped");
                        }
                        app->streaming_now = false;
                    }
                    else
                    {
                        const bool starting = !app->streaming_now;

                        /*-------------------------*\
                        | Direct mode cannot afford a |
                        | long wait per report.       |
                        \*-------------------------*/
                        app->device->set_ack_timeout_ms(STREAM_ACK_TIMEOUT_MS);

                        std::string error;
                        if(app->device->apply_frame(frame, error))
                        {
                            app->streaming_now = true;
                            app->design_kept  = true;

                            if(starting)
                            {
                                set_status(*app, "your design is on the keyboard, and it stays there "
                                                 "when you close this window (untick 'Show on keyboard' "
                                                 "to hand the keyboard back)");
                            }
                        }
                        else
                        {
                            app->stream_custom = false;
                            app->streaming_now = false;
                            if(app->stream_check != nullptr)
                            {
                                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->stream_check), FALSE);
                            }
                            set_status(*app, "streaming stopped: " + error);
                        }
                    }
                }

                /*---------------------------------------------*\
                | Sync server                                    |
                \*---------------------------------------------*/
                if(app->sync_running && app->sync != nullptr)
                {
                    std::string error;
                    const int applied = app->sync->poll(0, error);

                    if(applied < 0)
                    {
                        set_status(*app, "sync server error: " + error);
                        app->sync_running = false;
                    }
                    else
                    {
                        app->sync_messages += applied;

                        if(app->sync_view != nullptr)
                        {
                            app->sync_view->set_frame(app->sync->frame());
                        }

                        if(app->sync_label != nullptr)
                        {
                            char text[192];
                            std::snprintf(text, sizeof(text),
                                          "%d message%s received, %s",
                                          app->sync_messages,
                                          app->sync_messages == 1 ? "" : "s",
                                          app->sync->is_driving() ? "driving the keyboard" : "idle");
                            gtk_label_set_text(GTK_LABEL(app->sync_label), text);
                        }

                        if(app->sync->is_driving() && app->device != nullptr)
                        {
                            app->device->set_ack_timeout_ms(STREAM_ACK_TIMEOUT_MS);

                            std::string frame_error;
                            if(!app->device->apply_frame(app->sync->frame(), frame_error))
                            {
                                set_status(*app, "sync frame rejected: " + frame_error);
                                app->sync_running = false;
                            }
                        }
                    }
                }

                /*---------------------------------------------*\
                | Debounced hardware apply                       |
                \*---------------------------------------------*/
                if(app->apply_due >= 0.0 && app->now >= app->apply_due && !app->streaming_now)
                {
                    apply_effect_now(*app);
                }

                return G_SOURCE_CONTINUE;
            }

            void on_activate(GtkApplication* gtk_app, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(window_created)
                {
                    return;
                }
                window_created = TRUE;

                GtkWidget* window = gtk_application_window_new(gtk_app);
                gtk_window_set_title(GTK_WINDOW(window), "ROG Claymore");
                gtk_window_set_default_size(GTK_WINDOW(window), 1180, 860);
                set_window_icon(GTK_WINDOW(window));

                app->window = window;

                /*-----------------------------------------*\
                | Stop the redraw tick the moment the window |
                | starts going away, before its children are |
                | destroyed under the tick's feet.            |
                \*-----------------------------------------*/
                g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);

                GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
                gtk_container_add(GTK_CONTAINER(window), root);

                gtk_box_pack_start(GTK_BOX(root), build_header(*app), FALSE, FALSE, 0);

                GtkWidget* notebook = gtk_notebook_new();
                gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_effects_tab(*app),
                                         gtk_label_new("Effects"));
                gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_custom_tab(*app),
                                         gtk_label_new("Design your own"));
                gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_sync_tab(*app),
                                         gtk_label_new("Sync"));
                gtk_box_pack_start(GTK_BOX(root), notebook, TRUE, TRUE, 0);

                GtkWidget* status_frame = gtk_frame_new(nullptr);
                app->status_label = gtk_label_new("starting up");
                gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
                gtk_label_set_ellipsize(GTK_LABEL(app->status_label), PANGO_ELLIPSIZE_END);
                gtk_widget_set_margin_start(app->status_label, 8);
                gtk_widget_set_margin_end(app->status_label, 8);
                gtk_widget_set_margin_top(app->status_label, 4);
                gtk_widget_set_margin_bottom(app->status_label, 4);
                gtk_container_add(GTK_CONTAINER(status_frame), app->status_label);
                gtk_box_pack_start(GTK_BOX(root), status_frame, FALSE, FALSE, 0);

                /*-----------------------------------------*\
                | Voluntary redraws instead of letting GTK  |
                | guess: only the canvases animate.         |
                \*-----------------------------------------*/
                gtk_widget_show_all(window);

                /*-----------------------------------------*\
                | The canvases were created after the device |
                | was probed, so hand them the layout now.   |
                \*-----------------------------------------*/
                attach_views(*app);

                refresh_effect_controls(*app);
                refresh_design_picker(*app, app->design_name);

                if(app->preset_list != nullptr)
                {
                    GtkListBoxRow* first = gtk_list_box_get_row_at_index(
                        GTK_LIST_BOX(app->preset_list), 0);
                    if(first != nullptr)
                    {
                        gtk_list_box_select_row(GTK_LIST_BOX(app->preset_list), first);
                    }
                }

                /*-------------------------------------------------*\
                | The design the user asked to have at startup.       |
                |                                                    |
                | Loading counts as an edit, so the tick puts it on   |
                | the keyboard as well: "loaded at startup" is only   |
                | useful if the keyboard shows it.                    |
                \*-------------------------------------------------*/
                if(app->load_default_on_start && !default_design_name().empty())
                {
                    std::string error;
                    std::string name;

                    if(load_default_design(app->design, name, error))
                    {
                        app->design_name   = name;
                        app->design_edited = true;

                        /*-------------------------------------*\
                        | Deliberately not a status line: the    |
                        | tick sets one about the design being   |
                        | on the keyboard a frame later, and a   |
                        | message that lives for 30 ms is worse  |
                        | than one in the log.                   |
                        \*-------------------------------------*/
                        std::printf("[claymore-gui] loaded the startup design '%s'\n",
                                    name.c_str());
                    }
                    else
                    {
                        /*-----------------------------------------*\
                        | A broken or deleted startup design must not  |
                        | stop the window from opening; say so and let |
                        | the user paint over it.                      |
                        \*-----------------------------------------*/
                        set_status(*app, "could not load the startup design: " + error);
                    }
                }

                if(app->device == nullptr)
                {
                    set_status(*app, "no keyboard found; run 'claymore list' or install the udev rule, "
                                     "then press Reconnect");
                }

                if(app->start_hidden)
                {
                    hide_to_tray(*app);
                }
            }
        }
    }
}

int main(int argc, char** argv)
{
    using namespace claymore;
    using namespace claymore::gui;

    AppState app;

    /*---------------------------------------------------------*\
    | Whether to open a window starts as the saved preference, so  |
    | that an autostart entry can be just the binary name. The     |
    | flags below override it for a single run, which is what      |
    | rescues the user who turned the setting on and then expects  |
    | a window when they launch it by hand.                        |
    \*---------------------------------------------------------*/
    app.start_hidden = start_in_tray();

    /*---------------------------------------------------------*\
    | Turn our own flags into state, and drop them from the list   |
    | GTK is given: GtkApplication treats an option it does not     |
    | recognise as a fatal error, so --tray would stop the window   |
    | from ever opening.                                           |
    |                                                            |
    | The argument strings are copied because g_application_run     |
    | wants a writable argv that outlives the call.                 |
    \*---------------------------------------------------------*/
    bool                     show_help   = false;
    std::string              export_icons;
    std::vector<std::string> for_gtk;

    for(int i = 0; i < argc; i++)
    {
        const std::string arg = argv[i];

        if(i > 0 && arg == "--tray")
        {
            app.start_hidden = true;
        }
        else if(i > 0 && arg == "--window")
        {
            app.start_hidden = false;
        }
        else                if(i > 0 && arg == "--no-design")
        {
            app.load_default_on_start = false;
        }
        else if(i > 0 && arg == "--export-icons" && i + 1 < argc)
        {
            export_icons   = argv[i + 1];
            i             += 1;
        }
        else
        {
            if(i > 0 && (arg == "--help" || arg == "-h"))
            {
                show_help = true;
            }

            for_gtk.push_back(arg);
        }
    }

    /*---------------------------------------------------------*\
    | The icon, written out as PNGs for the desktop's icon        |
    | theme.                                                    |
    |                                                            |
    | This is the same drawing the window and the panel show, and |
    | it needs no display, which is what lets 'make install' and  |
    | the package put the real icon in the icon theme instead of  |
    | shipping a second copy of it as a file and hoping the two   |
    | never drift apart.                                          |
    \*---------------------------------------------------------*/
    if(!export_icons.empty())
    {
        int written = 0;

        /*-----------------------------------------------------*\
        | Create the directory: this is called from a Makefile    |
        | into a build directory that may not exist yet, and      |
        | "wrote 0 icon files" is a worse way to learn that than  |
        | just doing it.                                         |
        \*-----------------------------------------------------*/
        if(g_mkdir_with_parents(export_icons.c_str(), 0755) != 0)
        {
            std::fprintf(stderr, "claymore-gui: cannot create %s\n", export_icons.c_str());
            return 1;
        }

        for(const int size : { 16, 24, 32, 48, 64, 128, 256 })
        {
            GdkPixbuf* pixbuf = rog_emblem(size);
            if(pixbuf == nullptr)
            {
                continue;
            }

            char path[1024];
            std::snprintf(path, sizeof(path), "%s/rog-claymore-%d.png",
                          export_icons.c_str(), size);

            if(gdk_pixbuf_save(pixbuf, path, "png", nullptr, nullptr))
            {
                written++;
            }
            else
            {
                std::fprintf(stderr, "claymore-gui: cannot write %s\n", path);
            }

            g_object_unref(pixbuf);
        }

        /*-----------------------------------------------------*\
        | The name without a size is what an icon theme uses when |
        | nothing else matches.                                   |
        \*-----------------------------------------------------*/
        if(GdkPixbuf* pixbuf = rog_emblem(256))
        {
            const std::string path = export_icons + "/rog-claymore.png";

            if(gdk_pixbuf_save(pixbuf, path.c_str(), "png", nullptr, nullptr))
            {
                written++;
            }

            g_object_unref(pixbuf);
        }

        std::printf("[claymore-gui] wrote %d icon files into %s\n",
                    written, export_icons.c_str());

        return written > 0 ? 0 : 1;
    }

    if(show_help)
    {
        std::printf("usage: claymore-gui [--tray|--window] [--no-design]\n"
                    "       claymore-gui --export-icons <dir>\n\n"
                    "  --tray       start in the panel with the window hidden\n"
                    "  --window     open the window even if 'start-in-tray' is on\n"
                    "  --no-design  ignore the design chosen to load at startup\n"
                    "  --export-icons <dir>  write the icon as PNGs and exit; needs no\n"
                    "                        display, and is what 'make install' uses\n\n"
                    "'Start in the panel' on the Design tab, or 'claymore settings\n"
                    "start-in-tray on', makes the window stay away by default -\n"
                    "which is what you want from an autostart entry.\n");
        return 0;
    }

    std::vector<char*> gtk_argv;
    gtk_argv.reserve(for_gtk.size() + 1);
    for(std::string& text : for_gtk)
    {
        gtk_argv.push_back(text.data());
    }
    gtk_argv.push_back(nullptr);

    const int gtk_argc = static_cast<int>(for_gtk.size());

    /*---------------------------------------------------------*\
    | Start from a preset so the interface is never in an empty  |
    | state, then try to reach the keyboard.                     |
    \*---------------------------------------------------------*/
    const std::vector<EffectPreset> presets = builtin_presets();
    if(!presets.empty())
    {
        app.spec            = presets.front().spec;
        app.selected_preset = presets.front().id;
    }

    app.socket_path = default_socket_path();

    connect_device(app, "");

    GtkApplication* gtk_app = gtk_application_new("org.claymore.linux",
#if GLIB_CHECK_VERSION(2, 74, 0)
        G_APPLICATION_DEFAULT_FLAGS
#else
        G_APPLICATION_FLAGS_NONE
#endif
        );

    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &app);

    g_timeout_add(TICK_MS, on_tick, &app);

    const int status = g_application_run(G_APPLICATION(gtk_app), gtk_argc, gtk_argv.data());

    /*---------------------------------------------------------*\
    | Before we go, decide who owns the keyboard.                 |
    |                                                            |
    | If a design is on it, leave it there. The Claymore keeps    |
    | the last frame it was handed, so simply not revoking is     |
    | what makes a painted design survive the window closing the  |
    | same way a firmware effect does - revoking here is what     |
    | used to throw the design away, and it was the whole reason  |
    | a design felt temporary.                                    |
    |                                                            |
    | Anything else (a stream we did not keep, or the sync         |
    | server) hands the board back, because the firmware effect    |
    | is a better thing to leave a keyboard doing than a frozen    |
    | frame nobody asked for.                                     |
    \*---------------------------------------------------------*/
    if(app.device != nullptr && app.design_kept && app.layout != nullptr)
    {
        /*-----------------------------------------------------*\
        | Save it too. An unnamed canvas gets a name of its own  |
        | rather than being dropped, because the file is the     |
        | only copy and the next launch can load it.            |
        |                                                       |
        | Best effort: a failure here must not be noisy on exit. |
        \*-----------------------------------------------------*/
        std::string       error;
        const std::string name = default_design_name_for_new(app);

        if(save_design(app, name, error))
        {
            std::printf("[claymore-gui] saved the design as '%s'\n", app.design_name.c_str());
        }
        else
        {
            std::printf("[claymore-gui] could not save the design: %s\n", error.c_str());
        }

        std::printf("[claymore-gui] the design stays on the keyboard; "
                    "run 'claymore release' to hand it back\n");
    }
    else if(app.device != nullptr && (app.streaming_now || app.sync_running))
    {
        std::string error;
        if(app.device->release(error))
        {
            std::printf("[claymore-gui] released remote control on exit\n");
        }
    }

    g_object_unref(gtk_app);

    delete app.preview;
    delete app.custom;
    delete app.sync_view;

    return status;
}
