/*---------------------------------------------------------*\
| app.cpp - shared GUI state helpers                        |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "claymore/design.hpp"

namespace claymore
{
    namespace gui
    {
        namespace
        {
            GdkRGBA to_gdk(const Rgb& color)
            {
                GdkRGBA rgba;
                rgba.red   = color.r / 255.0;
                rgba.green = color.g / 255.0;
                rgba.blue  = color.b / 255.0;
                rgba.alpha = 1.0;
                return rgba;
            }

            Rgb from_gdk(const GdkRGBA& rgba)
            {
                const auto channel = [](double value)
                {
                    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
                };
                return Rgb(channel(rgba.red), channel(rgba.green), channel(rgba.blue));
            }

            /*-------------------------------------------------*\
            | Identity of an effect configuration, used to skip  |
            | redundant commits.                                  |
            \*-------------------------------------------------*/
            std::string spec_signature(const EffectSpec& spec)
            {
                std::ostringstream out;
                out << protocol::mode_name(spec.mode)
                    << '|' << spec.speed_percent
                    << '|' << (spec.brightness.has_value() ? std::to_string(*spec.brightness)
                                                          : std::string("-"))
                    << '|' << static_cast<int>(spec.direction)
                    << '|' << (spec.random_colors ? 1 : 0)
                    << '|';

                for(const Rgb& color : spec.normalized_colors())
                {
                    out << color.to_hex() << ',';
                }
                return out.str();
            }

            void update_labels(AppState& app)
            {
                if(app.device_label != nullptr)
                {
                    std::string text;
                    if(app.device != nullptr)
                    {
                        const DeviceStatus& status = app.device->status();
                        std::string numpad = "no numpad";
                        switch(status.numpad)
                        {
                            case Numpad::Left:  numpad = "numpad left";  break;
                            case Numpad::Right: numpad = "numpad right"; break;
                            case Numpad::None:  numpad = "no numpad";    break;
                        }

                        text = status.hidraw_node + "  -  " +
                               (status.firmware_version.empty() ? std::string("firmware unknown")
                                                                : "firmware " + status.firmware_version) +
                               "  -  " + (app.layout != nullptr ? app.layout->id : std::string("no layout")) +
                               (app.layout != nullptr ? "  -  " + std::to_string(app.layout->key_count) + " LEDs"
                                                      : std::string()) +
                               "  -  " + numpad;
                    }
                    else
                    {
                        text = "not connected  -  previewing ";
                        text += (app.layout != nullptr ? app.layout->id : std::string("no layout"));
                    }

                    /* The header widget is an entry, not a label. */
                    gtk_entry_set_text(GTK_ENTRY(app.device_label), text.c_str());
                }

                if(app.preset_title != nullptr)
                {
                    std::string name = "Custom";
                    for(const EffectPreset& preset : builtin_presets())
                    {
                        if(preset.id == app.selected_preset)
                        {
                            name = preset.name;
                        }
                    }
                    gtk_label_set_text(GTK_LABEL(app.preset_title), name.c_str());
                }
            }

            void add_color_button_row(AppState& app);
        }

        void set_status(AppState& app, const std::string& message)
        {
            if(app.status_label == nullptr)
            {
                return;
            }

            gtk_label_set_text(GTK_LABEL(app.status_label), message.c_str());
            std::printf("[claymore-gui] %s\n", message.c_str());
            std::fflush(stdout);
        }

        void attach_views(AppState& app)
        {
            if(app.layout == nullptr)
            {
                return;
            }

            const std::size_t keys = static_cast<std::size_t>(app.layout->key_count);
            if(app.design.size() != keys)
            {
                app.design.assign(keys, Rgb(0, 0, 0));
            }

            if(app.preview)
            {
                app.preview->set_layout(app.layout);
            }
            if(app.custom)
            {
                app.custom->set_layout(app.layout);
                app.custom->set_brush(app.brush);
                app.custom->set_brush_color(app.brush_color);
            }
            if(app.sync_view)
            {
                app.sync_view->set_layout(app.layout);
            }
        }

        bool connect_device(AppState& app, const std::string& device_path)
        {
            app.device.reset();
            app.detect.device_path = device_path;

            std::string error;
            std::string warning;
            std::unique_ptr<ClaymoreDevice> device = ClaymoreDevice::open(app.detect, error, &warning);

            if(!device)
            {
                /*---------------------------------------------*\
                | Keep the interface usable. Without a device    |
                | the previews still need a layout, so fall back |
                | to the requested one (or a sensible default).  |
                | This is what lets you explore effects and      |
                | paint a design before the udev rule lands.     |
                \*---------------------------------------------*/
                const std::string wanted = app.detect.layout_id.empty()
                    ? variant_id(app.detect.numpad.value_or(Numpad::None),
                                 app.detect.region.value_or(Region::US))
                    : app.detect.layout_id;

                app.layout = find_variant(wanted);

                if(app.layout != nullptr)
                {
                    attach_views(app);
                }

                update_labels(app);
                set_status(app, error + " - previewing layout " + wanted +
                                 " offline; connect a keyboard to apply anything");
                return false;
            }

            device->set_ack_timeout_ms(20);
            app.device = std::move(device);
            app.layout = &app.device->layout();

            /*---------------------------------------------*\
            | A fresh device may have a different LED count, |
            | so start from a clean canvas.                  |
            \*---------------------------------------------*/
            attach_views(app);

            update_labels(app);

            std::string note = "connected to " + app.device->status().hidraw_node +
                               " as " + app.layout->id;
            if(!warning.empty())
            {
                note += " (" + warning + ")";
            }
            set_status(app, note);
            return true;
        }

        void apply_effect_now(AppState& app)
        {
            if(app.device == nullptr || app.layout == nullptr)
            {
                set_status(app, "no keyboard connected, nothing to apply");
                return;
            }

            if(app.streaming_now)
            {
                /*-----------------------------------------*\
                | A firmware effect and direct frames cannot  |
                | both own the keyboard, and an explicit      |
                | apply means the effect is what the user     |
                | wants now. Refusing instead would leave     |
                | them stuck until they noticed a checkbox    |
                | they may have forgotten about, so the       |
                | design stream steps aside.                  |
                \*-----------------------------------------*/
                app.stream_custom = false;
                app.streaming_now = false;

                if(app.stream_check != nullptr)
                {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.stream_check), FALSE);
                }

                set_status(app, "stopped showing the design, applying the effect instead");
            }

            /*---------------------------------------------*\
            | The effect now owns the board, so nothing should |
            | be re-applied on exit.                          |
            \*---------------------------------------------*/
            app.design_kept = false;

            const std::string signature = spec_signature(app.spec);
            app.apply_due = -1.0;

            if(signature == app.last_applied)
            {
                set_status(app, std::string("already applied ") + protocol::mode_name(app.spec.mode) +
                                " with these settings");
                return;
            }

            app.device->set_ack_timeout_ms(20);

            std::string error;
            if(app.device->apply_effect(app.spec, error))
            {
                app.last_applied = signature;
                set_status(app, std::string("applied ") + protocol::mode_name(app.spec.mode) +
                                " and stored it in the keyboard");
            }
            else
            {
                set_status(app, "apply failed: " + error);
            }
        }

        void schedule_apply(AppState& app)
        {
            if(app.live_apply)
            {
                app.apply_due = app.now + 0.35;
            }
        }

        Frame current_custom_frame(const AppState& app)
        {
            if(app.design.empty())
            {
                return {};
            }
            return preview::render_custom(app.design, app.spec);
        }

        /*-----------------------------------------------------*\
        | One swatch per colour the current effect accepts, plus  |
        | buttons to add and remove colours.                      |
        \*-----------------------------------------------------*/
        namespace
        {
            void color_changed_cb(GtkColorButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "slot-index"));

                GdkRGBA rgba;
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
                const Rgb color = from_gdk(rgba);

                if(index == -1)
                {
                    /*-------------------------------------*\
                    | The standalone brush colour on the     |
                    | custom tab.                            |
                    \*-------------------------------------*/
                    app->brush_color = color;
                    return;
                }

                if(index >= 0 && index < static_cast<int>(app->spec.colors.size()))
                {
                    app->spec.colors[static_cast<std::size_t>(index)] = color;
                    set_status(*app, "colour " + std::to_string(index + 1) + " is now " + color.to_hex());
                    schedule_apply(*app);
                }
            }

            void add_color_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                const int maximum = app->spec.colors_max();
                if(maximum <= 0)
                {
                    return;
                }

                if(static_cast<int>(app->spec.colors.size()) >= maximum)
                {
                    set_status(*app, "this effect accepts at most " + std::to_string(maximum) + " colours");
                    return;
                }

                /*-----------------------------------------*\
                | Seed new colours from the last one so the  |
                | user has something to adjust.              |
                \*-----------------------------------------*/
                const Rgb seed = app->spec.colors.empty() ? Rgb(0xFF, 0xFF, 0xFF)
                                                          : app->spec.colors.back();
                app->spec.colors.push_back(Rgb::from_hsv(app->spec.colors.size() * 67.0, 1.0, 1.0));
                (void)seed;

                refresh_effect_controls(*app);
                schedule_apply(*app);
            }

            void remove_color_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                const int minimum = std::max(1, app->spec.colors_min());
                if(static_cast<int>(app->spec.colors.size()) <= minimum)
                {
                    set_status(*app, "this effect needs at least " + std::to_string(minimum) + " colours");
                    return;
                }

                app->spec.colors.pop_back();
                refresh_effect_controls(*app);
                schedule_apply(*app);
            }

            void add_color_button_row(AppState& app)
            {
                const int count = static_cast<int>(app.spec.colors.size());

                for(int i = 0; i < count; i++)
                {
                    const GdkRGBA swatch = to_gdk(app.spec.colors[static_cast<std::size_t>(i)]);
                    GtkWidget* button = gtk_color_button_new_with_rgba(&swatch);
                    const std::string title = "Colour " + std::to_string(i + 1);
                    gtk_color_button_set_title(GTK_COLOR_BUTTON(button), title.c_str());
                    gtk_widget_set_size_request(button, 44, 30);

                    /*-------------------------------------*\
                    | A colour button is a compound widget and |
                    | publishes no accessible name of its own, |
                    | so name it for screen readers and for    |
                    | anything else driving the window.        |
                    \*-------------------------------------*/
                    atk_object_set_name(gtk_widget_get_accessible(button), title.c_str());
                    g_object_set_data(G_OBJECT(button), "slot-index", GINT_TO_POINTER(i));
                    g_signal_connect(button, "color-set", G_CALLBACK(color_changed_cb), &app);
                    gtk_box_pack_start(GTK_BOX(app.colors_box), button, FALSE, FALSE, 0);
                }

                if(app.spec.colors_max() > count)
                {
                    GtkWidget* add = gtk_button_new_with_label("+");
                    gtk_widget_set_tooltip_text(add, "Add another colour");
                    g_signal_connect(add, "clicked", G_CALLBACK(add_color_cb), &app);
                    gtk_box_pack_start(GTK_BOX(app.colors_box), add, FALSE, FALSE, 0);
                }

                if(count > std::max(1, app.spec.colors_min()))
                {
                    GtkWidget* remove = gtk_button_new_with_label("-");
                    gtk_widget_set_tooltip_text(remove, "Remove the last colour");
                    g_signal_connect(remove, "clicked", G_CALLBACK(remove_color_cb), &app);
                    gtk_box_pack_start(GTK_BOX(app.colors_box), remove, FALSE, FALSE, 0);
                }
            }
        }

        void refresh_effect_controls(AppState& app)
        {
            /*-------------------------------------------------*\
            | Colour swatches                                    |
            \*-------------------------------------------------*/
            if(app.colors_box != nullptr)
            {
                gtk_container_foreach(GTK_CONTAINER(app.colors_box),
                    [](GtkWidget* child, gpointer)
                    {
                        gtk_widget_destroy(child);
                    }, nullptr);

                if(app.spec.uses_colors())
                {
                    /*-----------------------------------------*\
                    | Keep the colour list inside the range the  |
                    | firmware accepts for this mode.            |
                    \*-----------------------------------------*/
                    const int minimum = std::max(1, app.spec.colors_min());
                    const int maximum = app.spec.colors_max();

                    if(app.spec.colors.empty())
                    {
                        app.spec.colors.push_back(Rgb(0xFF, 0xFF, 0xFF));
                    }
                    while(static_cast<int>(app.spec.colors.size()) < minimum)
                    {
                        app.spec.colors.push_back(app.spec.colors.front());
                    }
                    while(static_cast<int>(app.spec.colors.size()) > maximum)
                    {
                        app.spec.colors.pop_back();
                    }

                    add_color_button_row(app);
                }
            }

            if(app.color_note != nullptr)
            {
                const std::string note = app.spec.uses_colors()
                    ? std::to_string(app.spec.colors.size()) + " of at most " +
                      std::to_string(app.spec.colors_max()) + " colours"
                    : std::string("this effect generates its own colours");
                gtk_label_set_text(GTK_LABEL(app.color_note), note.c_str());
            }

            /*-------------------------------------------------*\
            | Controls the effect does not use get greyed out     |
            \*-------------------------------------------------*/
            if(app.direction != nullptr)
            {
                gtk_widget_set_sensitive(app.direction, app.spec.uses_direction());
            }
            if(app.random_check != nullptr)
            {
                gtk_widget_set_sensitive(app.random_check, app.spec.supports_random());
            }
            if(app.speed_scale != nullptr)
            {
                /*-----------------------------------------*\
                | Static has nothing to animate.             |
                \*-----------------------------------------*/
                gtk_widget_set_sensitive(app.speed_scale,
                                         app.spec.mode != protocol::Mode::Static);
            }

            update_labels(app);
        }

        /*---------------------------------------------------------*\
        | A design file is deliberately dull: one frame slot and a   |
        | hex colour per line, plus a comment header. That makes it  |
        | diffable, hand editable and scriptable.                    |
        \*---------------------------------------------------------*/
        std::string default_design_name_for_new(AppState& app)
        {
            if(!app.design_name.empty())
            {
                return app.design_name;
            }

            /*-------------------------------------------------*\
            | An unsaved canvas still has to be saveable: this    |
            | is the name it gets, and the one it keeps. Nobody   |
            | expects a painting program to lose the painting     |
            | because they did not think about file names yet.    |
            \*-------------------------------------------------*/
            return "My design";
        }

        bool save_design(AppState& app, const std::string& name, std::string& error)
        {
            if(app.layout == nullptr)
            {
                error = "nothing to save until a keyboard is connected";
                return false;
            }

            const std::string safe = design_name_from(name);
            if(safe.empty())
            {
                error = "a design needs a name";
                return false;
            }

            if(!save_named_design(safe, app.design, app.layout->id, error))
            {
                return false;
            }

            app.design_name = safe;
            return true;
        }

        void refresh_design_picker(AppState& app, const std::string& select)
        {
            if(app.design_combo == nullptr)
            {
                return;
            }

            /*-------------------------------------------------*\
            | Rebuilding the list must not look like the user     |
            | picking something, or every refresh would load a    |
            | design underneath them.                             |
            \*-------------------------------------------------*/
            app.design_picker_busy = true;

            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app.design_combo));

            const std::vector<std::string> names  = list_designs();
            const std::string              startup = default_design_name();

            std::string chosen = select;
            if(chosen.empty())
            {
                chosen = app.design_name;
            }

            int index = -1;

            for(const std::string& name : names)
            {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.design_combo), name.c_str());
                index++;

                if(name == chosen)
                {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(app.design_combo), index);
                }
            }

            /*-------------------------------------------------*\
            | A canvas that has never been saved is still worth   |
            | showing in the picker, labelled as such.            |
            \*-------------------------------------------------*/
            if(chosen.empty() && !names.empty())
            {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app.design_combo), 0);
            }

            if(app.default_check != nullptr)
            {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.default_check),
                                             !startup.empty() && startup == chosen);
            }

            app.design_picker_busy = false;
        }

        bool load_design(AppState& app, const std::string& name, std::string& error)
        {
            if(app.design.empty())
            {
                error = "connect a keyboard before loading a design";
                return false;
            }

            const std::string safe = design_name_from(name);
            if(safe.empty())
            {
                error = "no such design";
                return false;
            }

            if(!load_design_file(design_path_for(safe), app.design, error))
            {
                return false;
            }

            /*---------------------------------------------*\
            | Loading is an edit like any other, so it shows   |
            | up on the keyboard too.                          |
            \*---------------------------------------------*/
            app.design_name   = safe;
            app.design_edited = true;
            return true;
        }
    }
}
