/*---------------------------------------------------------*\
| panels.cpp - GUI tabs                                     |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "claymore/design.hpp"
#include "tray.hpp"

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

            GtkWidget* wrap_label(const std::string& text, std::size_t max_chars = 0)
            {
                std::string out = text;
                if(max_chars > 0 && out.size() > max_chars)
                {
                    out = out.substr(0, max_chars - 1) + "...";
                }

                GtkWidget* label = gtk_label_new(nullptr);
                gtk_label_set_markup(GTK_LABEL(label), out.c_str());
                gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
                gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
                return label;
            }

            GtkWidget* section(const std::string& title, GtkWidget* child)
            {
                GtkWidget* frame = gtk_frame_new(title.c_str());
                gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
                gtk_container_add(GTK_CONTAINER(frame), child);
                return frame;
            }

            GtkWidget* titled(const std::string& text)
            {
                std::string markup = "<b>" + text + "</b>";
                GtkWidget*  label  = gtk_label_new(nullptr);
                gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
                gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
                return label;
            }

            /*=================================================================*\
            | Effects tab                                                        |
            \*=================================================================*/

            void preset_selected_cb(GtkListBox*, GtkListBoxRow* row, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                if(row == nullptr)
                {
                    return;
                }

                const int index = gtk_list_box_row_get_index(row);
                const std::vector<EffectPreset> presets = builtin_presets();
                if(index < 0 || index >= static_cast<int>(presets.size()))
                {
                    return;
                }

                const EffectPreset& preset = presets[static_cast<std::size_t>(index)];

                /*-------------------------------------------------------*\
                | Adopt the preset but keep the user's speed and brightness |
                | choices, which are personal taste rather than part of the |
                | effect itself.                                            |
                \*-------------------------------------------------------*/
                const int    kept_speed  = app->spec.speed_percent;
                const auto   kept_bright = app->spec.brightness;
                const bool   kept_live   = app->live_apply;

                app->spec           = preset.spec;
                app->spec.speed_percent = kept_speed;
                app->spec.brightness    = kept_bright;
                app->live_apply         = kept_live;
                app->selected_preset    = preset.id;

                if(app->speed_scale != nullptr)
                {
                    gtk_range_set_value(GTK_RANGE(app->speed_scale), app->spec.speed_percent);
                }
                if(app->bright_scale != nullptr && app->spec.brightness.has_value())
                {
                    gtk_range_set_value(GTK_RANGE(app->bright_scale),
                                        static_cast<double>(*app->spec.brightness));
                }
                if(app->random_check != nullptr)
                {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->random_check),
                                                 app->spec.random_colors);
                }
                if(app->direction != nullptr)
                {
                    int active = 0;
                    switch(app->spec.direction)
                    {
                        case protocol::Direction::Left:  active = 0; break;
                        case protocol::Direction::Right: active = 1; break;
                        case protocol::Direction::Up:    active = 2; break;
                        case protocol::Direction::Down:  active = 3; break;
                    }
                    gtk_combo_box_set_active(GTK_COMBO_BOX(app->direction), active);
                }

                refresh_effect_controls(*app);
                set_status(*app, preset.name + ": " + preset.description);
                schedule_apply(*app);
            }

            void speed_changed_cb(GtkRange* range, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->spec.speed_percent = static_cast<int>(std::lround(gtk_range_get_value(range)));
                app->selected_preset.clear();

                if(app->speed_label != nullptr)
                {
                    char text[64];
                    std::snprintf(text, sizeof(text), "%d%%", app->spec.speed_percent);
                    gtk_label_set_text(GTK_LABEL(app->speed_label), text);
                }

                schedule_apply(*app);
            }

            void brightness_changed_cb(GtkRange* range, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->spec.brightness = static_cast<int>(std::lround(gtk_range_get_value(range)));
                app->selected_preset.clear();

                if(app->bright_label != nullptr)
                {
                    const char* names[] = { "1 of 4", "2 of 4", "3 of 4", "4 of 4" };
                    const int level = std::clamp(*app->spec.brightness, 0, 3);
                    gtk_label_set_text(GTK_LABEL(app->bright_label), names[level]);
                }

                schedule_apply(*app);
            }

            void direction_changed_cb(GtkComboBox* combo, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                switch(gtk_combo_box_get_active(combo))
                {
                    case 1:  app->spec.direction = protocol::Direction::Right; break;
                    case 2:  app->spec.direction = protocol::Direction::Up;    break;
                    case 3:  app->spec.direction = protocol::Direction::Down;  break;
                    default: app->spec.direction = protocol::Direction::Left;  break;
                }

                app->selected_preset.clear();
                schedule_apply(*app);
            }

            void random_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->spec.random_colors = gtk_toggle_button_get_active(button);
                app->selected_preset.clear();
                schedule_apply(*app);
            }

            void live_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->live_apply = gtk_toggle_button_get_active(button);

                if(app->live_apply)
                {
                    set_status(*app, "live apply on: changes reach the keyboard after a short delay");
                    schedule_apply(*app);
                }
                else
                {
                    app->apply_due = -1.0;
                    set_status(*app, "live apply off: use Apply to send the effect");
                }
            }

            void apply_clicked_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                apply_effect_now(*app);
            }

        }

        GtkWidget* build_effects_tab(AppState& app)
        {
            GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

            /*-----------------------------------------------------*\
            | Left: the preset gallery.                              |
            \*-----------------------------------------------------*/
            GtkWidget* list = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);

            for(const EffectPreset& preset : builtin_presets())
            {
                GtkWidget* row = gtk_list_box_row_new();

                /*---------------------------------------------*\
                | Colourless effects still need a swatch, and    |
                | to_hex() has no prefix, so add exactly one #.  |
                \*---------------------------------------------*/
                std::string swatch = "#4a4a4a";
                if(!preset.spec.colors.empty())
                {
                    swatch = "#" + preset.spec.colors.front().to_hex();
                }

                const std::string markup =
                    "<b>" + preset.name + "</b>\n"
                    "<span foreground=\"" + swatch + "\">\u2588\u2588\u2588</span>  "
                    "<small>" + preset.description + "</small>";

                GtkWidget* content = wrap_label(markup);

                /* A GtkLabel is not a GtkContainer in GTK 3, so use margins. */
                gtk_widget_set_margin_start(content, 6);
                gtk_widget_set_margin_end(content, 6);
                gtk_widget_set_margin_top(content, 6);
                gtk_widget_set_margin_bottom(content, 6);
                gtk_container_add(GTK_CONTAINER(row), content);
                gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
            }

            app.preset_list = list;
            g_signal_connect(list, "row-selected", G_CALLBACK(preset_selected_cb), &app);

            GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                           GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_widget_set_size_request(scroller, 300, -1);
            gtk_container_add(GTK_CONTAINER(scroller), list);
            gtk_paned_pack1(GTK_PANED(paned), section("Effects", scroller), FALSE, FALSE);

            /*-----------------------------------------------------*\
            | Right: preview plus parameters.                        |
            \*-----------------------------------------------------*/
            GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_container_set_border_width(GTK_CONTAINER(right), 6);

            app.preview = new KeyboardView();
            app.preview->set_frame(Frame());
            gtk_widget_set_size_request(app.preview->widget(), -1, 210);
            gtk_box_pack_start(GTK_BOX(right), app.preview->widget(), FALSE, FALSE, 0);

            GtkWidget* title = gtk_label_new(nullptr);
            gtk_label_set_markup(GTK_LABEL(title), "<b>Pick an effect on the left</b>");
            gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
            app.preset_title = title;
            gtk_box_pack_start(GTK_BOX(right), title, FALSE, FALSE, 0);

            /*-----------------------------------------------------*\
            | Sliders                                                |
            \*-----------------------------------------------------*/
            GtkWidget* grid = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
            gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

            app.speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
            gtk_range_set_value(GTK_RANGE(app.speed_scale), 50);
            gtk_widget_set_size_request(app.speed_scale, 240, -1);
            g_signal_connect(app.speed_scale, "value-changed", G_CALLBACK(speed_changed_cb), &app);
            gtk_grid_attach(GTK_GRID(grid), wrap_label("Speed"), 0, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), app.speed_scale, 1, 0, 1, 1);
            app.speed_label = gtk_label_new("50%");
            gtk_label_set_xalign(GTK_LABEL(app.speed_label), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), app.speed_label, 2, 0, 1, 1);

            app.bright_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 3, 1);
            gtk_range_set_value(GTK_RANGE(app.bright_scale), 3);
            gtk_widget_set_size_request(app.bright_scale, 240, -1);
            g_signal_connect(app.bright_scale, "value-changed", G_CALLBACK(brightness_changed_cb), &app);
            gtk_grid_attach(GTK_GRID(grid), wrap_label("Brightness"), 0, 1, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), app.bright_scale, 1, 1, 1, 1);
            app.bright_label = gtk_label_new("4 of 4");
            gtk_label_set_xalign(GTK_LABEL(app.bright_label), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), app.bright_label, 2, 1, 1, 1);

            app.direction = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.direction), "Left");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.direction), "Right");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.direction), "Up");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.direction), "Down");
            gtk_combo_box_set_active(GTK_COMBO_BOX(app.direction), 0);
            g_signal_connect(app.direction, "changed", G_CALLBACK(direction_changed_cb), &app);
            gtk_grid_attach(GTK_GRID(grid), wrap_label("Direction"), 0, 2, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), app.direction, 1, 2, 1, 1);

            app.random_check = gtk_check_button_new_with_label("Random colours");
            gtk_widget_set_tooltip_text(app.random_check,
                "Let the firmware choose colours for the effects that support it");
            g_signal_connect(app.random_check, "toggled", G_CALLBACK(random_toggled_cb), &app);
            gtk_grid_attach(GTK_GRID(grid), app.random_check, 0, 3, 2, 1);

            gtk_box_pack_start(GTK_BOX(right), grid, FALSE, FALSE, 0);

            /*-----------------------------------------------------*\
            | Colours                                                |
            \*-----------------------------------------------------*/
            GtkWidget* colors_frame = gtk_frame_new("Colours");
            GtkWidget* colors_box_v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_container_set_border_width(GTK_CONTAINER(colors_box_v), 6);

            app.colors_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_box_pack_start(GTK_BOX(colors_box_v), app.colors_box, FALSE, FALSE, 0);

            app.color_note = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(app.color_note), 0.0f);
            gtk_box_pack_start(GTK_BOX(colors_box_v), app.color_note, FALSE, FALSE, 0);

            gtk_container_add(GTK_CONTAINER(colors_frame), colors_box_v);
            gtk_box_pack_start(GTK_BOX(right), colors_frame, FALSE, FALSE, 0);

            /*-----------------------------------------------------*\
            | Apply / save                                           |
            \*-----------------------------------------------------*/
            GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            GtkWidget* apply = gtk_button_new_with_label("Apply to keyboard");
            gtk_widget_set_tooltip_text(apply, "Send this effect now");
            g_signal_connect(apply, "clicked", G_CALLBACK(apply_clicked_cb), &app);
            gtk_box_pack_start(GTK_BOX(buttons), apply, FALSE, FALSE, 0);

            app.live_check = gtk_check_button_new_with_label("Live apply");
            gtk_widget_set_tooltip_text(app.live_check,
                "Send changes automatically while you adjust the controls");
            g_signal_connect(app.live_check, "toggled", G_CALLBACK(live_toggled_cb), &app);
            gtk_box_pack_start(GTK_BOX(buttons), app.live_check, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(right), buttons, FALSE, FALSE, 0);

            /*-----------------------------------------------------*\
            | There is deliberately no "don't save" option. On the   |
            | Claymore the 0x50/0x55 packet is what commits the      |
            | effect, so an effect that is not committed is not      |
            | shown at all: it flashes briefly and then the board    |
            | reverts to the previous effect.                        |
            \*-----------------------------------------------------*/
            gtk_box_pack_start(GTK_BOX(right), wrap_label(
                "<small>Applying an effect also stores it in the keyboard, because the\n"
                "firmware needs the commit packet in order to show it at all.</small>"),
                FALSE, FALSE, 0);

            gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
            return paned;
        }

        /*=================================================================*\
        | Custom tab                                                         |
        \*=================================================================*/

        namespace
        {
            /*-------------------------------------------------*\
            | The frame title doubles as the tool's own label, so  |
            | "what does this brush do" is answered by the canvas   |
            | itself instead of by a tooltip nobody hovers over.   |
            \*-------------------------------------------------*/
            void set_canvas_hint(AppState* app, const char* text)
            {
                if(app->canvas_frame != nullptr)
                {
                    gtk_frame_set_label(GTK_FRAME(app->canvas_frame), text);
                }
            }

            void brush_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                if(!gtk_toggle_button_get_active(button))
                {
                    return;
                }

                if(button == GTK_TOGGLE_BUTTON(app->brush_erase))      app->brush = Brush::Erase;
                else if(button == GTK_TOGGLE_BUTTON(app->brush_pick))  app->brush = Brush::Picker;
                else                                                   app->brush = Brush::Paint;

                if(app->custom != nullptr)
                {
                    app->custom->set_brush(app->brush);
                }

                /*---------------------------------------------*\
                | Say what the tool does. The eyedropper changes  |
                | nothing on the canvas by design, and a user who |
                | selects it by accident sees every click being   |
                | ignored unless the window tells them otherwise. |
                \*---------------------------------------------*/
                switch(app->brush)
                {
                    case Brush::Erase:
                        set_canvas_hint(app, "Click or drag to erase");
                        set_status(*app, "erase: clicks clear keys, drag to wipe across them");
                        break;

                    case Brush::Picker:
                        set_canvas_hint(app, "Click a key to copy its colour");
                        set_status(*app, "eyedropper: clicks copy a key's colour into the brush, they do not paint");
                        break;

                    case Brush::Paint:
                        set_canvas_hint(app, "Click or drag to paint");
                        set_status(*app, "paint: clicks colour keys with the brush, middle click erases");
                        break;
                }
            }

            void to_tray_cb(GtkButton*, gpointer data)
            {
                hide_to_tray(*static_cast<AppState*>(data));
            }

            void brush_color_cb(GtkColorButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                GdkRGBA rgba;
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
                app->brush_color = from_gdk(rgba);

                if(app->custom != nullptr)
                {
                    app->custom->set_brush_color(app->brush_color);
                }
            }

            void paint_cb(int slot, const Rgb& color, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(std::getenv("CLAYMORE_GUI_TRACE") != nullptr)
                {
                    std::fprintf(stderr, "[trace] paint_cb slot=%d color=%s design=%zu\n",
                                 slot, color.to_hex().c_str(), app->design.size());
                }

                if(slot < 0 || static_cast<std::size_t>(slot) >= app->design.size())
                {
                    return;
                }

                app->design[static_cast<std::size_t>(slot)] = color;

                /*---------------------------------------------*\
                | An edit is a request to see this on the board.  |
                | Until one happens the keyboard is left alone,   |
                | so opening the tab never steals direct mode.    |
                \*---------------------------------------------*/
                app->design_edited = true;
            }

            void custom_view_paint_cb(int slot, const Rgb& color, gpointer data)
            {
                paint_cb(slot, color, data);
            }

            void fill_all_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                std::fill(app->design.begin(), app->design.end(), app->brush_color);
                app->design_edited = true;
                set_status(*app, "filled every key with " + app->brush_color.to_hex());
            }

            void clear_all_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                std::fill(app->design.begin(), app->design.end(), Rgb(0, 0, 0));
                app->design_edited = true;
                set_status(*app, "cleared the design");
            }

            void rainbow_fill_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(app->layout == nullptr)
                {
                    set_status(*app, "connect a keyboard first");
                    return;
                }

                /*---------------------------------------------*\
                | Hue follows the key's position so the result is |
                | a pleasing diagonal sweep rather than noise.    |
                \*---------------------------------------------*/
                for(int slot = 0; slot < app->layout->key_count; slot++)
                {
                    const auto position = find_key_pos(*app->layout, slot);
                    double hue = 0.0;

                    if(position)
                    {
                        hue = (static_cast<double>(position->col) / std::max(1, app->layout->cols - 1)) * 200.0 +
                              (static_cast<double>(position->row) / std::max(1, app->layout->rows - 1)) * 160.0;
                    }

                    app->design[static_cast<std::size_t>(slot)] = Rgb::from_hsv(hue, 1.0, 1.0);
                }

                app->design_edited = true;
                set_status(*app, "rainbow fill applied");
            }

            void stream_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);
                app->stream_custom = gtk_toggle_button_get_active(button);

                if(!app->stream_custom)
                {
                    /*---------------------------------------------*\
                    | Unticking means "stop lighting my keyboard with  |
                    | this design", so hand the board back rather than |
                    | leaving it frozen on a frame. The design itself  |
                    | is still on screen and one tick away.            |
                    \*---------------------------------------------*/
                    const bool was_ours = app->design_kept;
                    app->design_kept = false;

                    std::string error;
                    if(was_ours && app->device != nullptr && app->device->release(error))
                    {
                        set_status(*app, "handed the keyboard back to its own effects");
                    }
                    else
                    {
                        set_status(*app, "stopped showing the design on the keyboard");
                    }
                    return;
                }

                if(app->device == nullptr)
                {
                    app->stream_custom = false;
                    gtk_toggle_button_set_active(button, FALSE);
                    set_status(*app, "connect a keyboard before streaming");
                    return;
                }

                /*---------------------------------------------*\
                | Ticking this is an explicit request to drive    |
                | the board now, so it counts as an edit and      |
                | cancels any effect apply that is still queued.  |
                \*---------------------------------------------*/
                app->device->set_release_on_destroy(false);
                app->design_edited = true;
                app->apply_due     = -1.0;
                set_status(*app, "streaming your design in direct mode");
            }

            /*-------------------------------------------------*\
            | Asks for a name, the way a "Save as" should.         |
            |                                                       |
            | Returns false when the user cancels, which is not an   |
            | error and must not be reported as one.                 |
            \*-------------------------------------------------*/
            bool ask_for_design_name(GtkWindow* parent, const std::string& initial, std::string& out)
            {
                GtkWidget* dialog = gtk_dialog_new_with_buttons(
                    "Name this design", parent,
                    GTK_DIALOG_MODAL,
                    "Cancel", GTK_RESPONSE_CANCEL,
                    "Save",   GTK_RESPONSE_ACCEPT,
                    nullptr);

                GtkWidget* entry = gtk_entry_new();
                gtk_entry_set_text(GTK_ENTRY(entry), initial.c_str());
                gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
                gtk_widget_set_size_request(entry, 280, -1);
                atk_object_set_name(gtk_widget_get_accessible(entry), "Design name");

                GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);

                GtkWidget* hint = wrap_label(
                    "<small>Saved designs live in your config directory, and one of them "
                    "can be loaded every time this starts.</small>");
                gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 8);

                gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
                gtk_widget_show_all(dialog);

                const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
                if(response == GTK_RESPONSE_ACCEPT)
                {
                    const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
                    out = (text == nullptr) ? std::string() : std::string(text);
                }

                gtk_widget_destroy(dialog);
                return response == GTK_RESPONSE_ACCEPT;
            }

            void design_picker_changed_cb(GtkComboBox* combo, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                /*---------------------------------------------*\
                | Ignore the change signals that come from         |
                | rebuilding the list, or every edit would reload  |
                | the design file over the user's painting.        |
                \*---------------------------------------------*/
                if(app->design_picker_busy)
                {
                    return;
                }

                char* name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
                if(name == nullptr)
                {
                    return;
                }

                const std::string chosen = name;
                g_free(name);

                if(chosen.empty() || chosen == app->design_name)
                {
                    return;
                }

                std::string error;
                if(load_design(*app, chosen, error))
                {
                    refresh_design_picker(*app, chosen);
                    set_status(*app, "loaded design '" + chosen + "'");
                }
                else
                {
                    set_status(*app, error);
                }
            }

            void design_save_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                std::string name = app->design_name;
                if(name.empty())
                {
                    if(!ask_for_design_name(GTK_WINDOW(app->window),
                                            default_design_name_for_new(*app), name))
                    {
                        return;
                    }
                }

                std::string error;
                if(save_design(*app, name, error))
                {
                    refresh_design_picker(*app, app->design_name);
                    set_status(*app, "saved '" + app->design_name + "'");
                }
                else
                {
                    set_status(*app, error);
                }
            }

            void design_save_as_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                std::string name;
                if(!ask_for_design_name(GTK_WINDOW(app->window),
                                        default_design_name_for_new(*app), name))
                {
                    return;
                }

                std::string error;
                if(save_design(*app, name, error))
                {
                    refresh_design_picker(*app, app->design_name);
                    set_status(*app, "saved '" + app->design_name + "'");
                }
                else
                {
                    set_status(*app, error);
                }
            }

            void design_delete_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(app->design_name.empty())
                {
                    set_status(*app, "that design has not been saved yet");
                    return;
                }

                /*---------------------------------------------*\
                | Ask first. A misclick should not delete         |
                | something the user spent time painting.         |
                \*---------------------------------------------*/
                GtkWidget* confirm = gtk_message_dialog_new(
                    GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                    GTK_BUTTONS_NONE,
                    "Delete the design '%s'?", app->design_name.c_str());
                gtk_dialog_add_buttons(GTK_DIALOG(confirm),
                                       "Cancel", GTK_RESPONSE_CANCEL,
                                       "Delete", GTK_RESPONSE_ACCEPT,
                                       nullptr);

                const gint response = gtk_dialog_run(GTK_DIALOG(confirm));
                gtk_widget_destroy(confirm);

                if(response != GTK_RESPONSE_ACCEPT)
                {
                    return;
                }

                const std::string removed = app->design_name;
                std::string       error;

                if(delete_named_design(removed, error))
                {
                    /*-----------------------------------------*\
                    | The canvas keeps what was painted; only the  |
                    | file is gone, and the design becomes an      |
                    | unnamed one that can be saved again.         |
                    \*-----------------------------------------*/
                    app->design_name = {};
                    refresh_design_picker(*app, {});
                    set_status(*app, "deleted '" + removed + "'; the canvas still has it");
                }
                else
                {
                    set_status(*app, error);
                }
            }

            void design_default_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(app->design_picker_busy)
                {
                    return;
                }

                std::string       error;
                const std::string name = gtk_toggle_button_get_active(button)
                                       ? app->design_name
                                       : std::string();

                if(gtk_toggle_button_get_active(button) && name.empty())
                {
                    /*-----------------------------------------*\
                    | Making an unsaved canvas the startup design  |
                    | means saving it first: the startup loader    |
                    | reads a file, and there is not one yet.      |
                    \*-----------------------------------------*/
                    std::string chosen;
                    if(!ask_for_design_name(GTK_WINDOW(app->window),
                                            default_design_name_for_new(*app), chosen))
                    {
                        app->design_picker_busy = true;
                        gtk_toggle_button_set_active(button, FALSE);
                        app->design_picker_busy = false;
                        return;
                    }

                    if(!save_design(*app, chosen, error))
                    {
                        set_status(*app, error);
                        return;
                    }

                    refresh_design_picker(*app, app->design_name);
                    app->design_picker_busy = true;
                    gtk_toggle_button_set_active(button, TRUE);
                    app->design_picker_busy = false;
                }

                if(!set_default_design(name, error))
                {
                    set_status(*app, error);
                    return;
                }

                if(name.empty())
                {
                    set_status(*app, "no design will be loaded at startup");
                }
                else
                {
                    set_status(*app, "'" + name + "' will be loaded every time this starts");
                }
            }

            /*-------------------------------------------------*\
            | 'Start in the panel' exists for one purpose: so     |
            | that an autostart entry can be the bare binary     |
            | name with no arguments and still not throw a       |
            | window at the user at login.                       |
            |                                                    |
            | It is a saved preference rather than a flag because |
            | an autostart entry has nowhere nice to put a flag.  |
            \*-------------------------------------------------*/
            void start_in_tray_toggled_cb(GtkToggleButton* button, gpointer data)
            {
                AppState*   app = static_cast<AppState*>(data);
                const bool  on  = gtk_toggle_button_get_active(button) != FALSE;

                std::string error;
                if(!set_start_in_tray(on, error))
                {
                    set_status(*app, "could not save the setting: " + error);
                    return;
                }

                set_status(*app, on
                    ? "the window will stay away next time: it will start in the panel"
                    : "the window will open next time");
            }
        }

        GtkWidget* build_custom_tab(AppState& app)
        {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_container_set_border_width(GTK_CONTAINER(box), 8);

            /*-----------------------------------------------------*\
            | Tool row                                               |
            \*-----------------------------------------------------*/
            GtkWidget* tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            app.brush_paint = gtk_radio_button_new_with_label(nullptr, "Paint");
            app.brush_erase = gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(app.brush_paint), "Erase");
            app.brush_pick  = gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(app.brush_paint), "Eyedropper");
            gtk_widget_set_tooltip_text(app.brush_pick,
                "Copies a key's colour into the brush. It does not paint.");

            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.brush_paint), TRUE);

            for(GtkWidget* widget : { app.brush_paint, app.brush_erase, app.brush_pick })
            {
                g_signal_connect(widget, "toggled", G_CALLBACK(brush_toggled_cb), &app);
                gtk_box_pack_start(GTK_BOX(tools), widget, FALSE, FALSE, 0);
            }

            const GdkRGBA brush_rgba = to_gdk(app.brush_color);
            app.brush_color_b = gtk_color_button_new_with_rgba(&brush_rgba);
            gtk_color_button_set_title(GTK_COLOR_BUTTON(app.brush_color_b), "Brush colour");

            /*---------------------------------------------*\
            | Same as the effect swatches: a compound widget  |
            | has no accessible name unless it is given one.  |
            \*---------------------------------------------*/
            atk_object_set_name(gtk_widget_get_accessible(app.brush_color_b), "Brush colour");
            g_signal_connect(app.brush_color_b, "color-set", G_CALLBACK(brush_color_cb), &app);
            gtk_box_pack_start(GTK_BOX(tools), wrap_label("Brush"), FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(tools), app.brush_color_b, FALSE, FALSE, 0);

            GtkWidget* fill = gtk_button_new_with_label("Fill all");
            g_signal_connect(fill, "clicked", G_CALLBACK(fill_all_cb), &app);
            gtk_box_pack_start(GTK_BOX(tools), fill, FALSE, FALSE, 0);

            GtkWidget* rainbow = gtk_button_new_with_label("Rainbow fill");
            gtk_widget_set_tooltip_text(rainbow, "Colours computed from each key's position");
            g_signal_connect(rainbow, "clicked", G_CALLBACK(rainbow_fill_cb), &app);
            gtk_box_pack_start(GTK_BOX(tools), rainbow, FALSE, FALSE, 0);

            GtkWidget* clear = gtk_button_new_with_label("Clear");
            g_signal_connect(clear, "clicked", G_CALLBACK(clear_all_cb), &app);
            gtk_box_pack_start(GTK_BOX(tools), clear, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(box), tools, FALSE, FALSE, 0);

            /*-----------------------------------------------------*\
            | Canvas, which is the whole point of this tab.           |
            \*-----------------------------------------------------*/
            app.custom = new KeyboardView();
            app.custom->set_editable(true);
            app.custom->set_brush_color(app.brush_color);
            app.custom->on_paint = [&app](int slot, const Rgb& color)
            {
                custom_view_paint_cb(slot, color, &app);
            };
            app.custom->on_message = [&app](const std::string& message)
            {
                set_status(app, message);
            };

            app.canvas_frame = gtk_frame_new("Click or drag to paint");
            gtk_container_add(GTK_CONTAINER(app.canvas_frame), app.custom->widget());
            gtk_box_pack_start(GTK_BOX(box), app.canvas_frame, TRUE, TRUE, 0);

            /*-----------------------------------------------------*\
            | Streaming and file IO                                  |
            \*-----------------------------------------------------*/
            GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            app.stream_check = gtk_check_button_new_with_label("Show on keyboard");
            gtk_widget_set_tooltip_text(app.stream_check,
                "Hold the keyboard in direct mode and push this design at 30 frames per second.\n"
                "Started automatically by your first edit.");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.stream_check), app.stream_custom);
            g_signal_connect(app.stream_check, "toggled", G_CALLBACK(stream_toggled_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), app.stream_check, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(bottom), wrap_label("Design"), FALSE, FALSE, 0);

            app.design_combo = gtk_combo_box_text_new();
            gtk_widget_set_size_request(app.design_combo, 200, -1);
            atk_object_set_name(gtk_widget_get_accessible(app.design_combo), "Design");
            gtk_widget_set_tooltip_text(app.design_combo, "Pick a saved design to load it");
            g_signal_connect(app.design_combo, "changed",
                             G_CALLBACK(design_picker_changed_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), app.design_combo, FALSE, FALSE, 0);

            GtkWidget* save = gtk_button_new_with_label("Save");
            gtk_widget_set_tooltip_text(save, "Save over the current design, or name it if it has none");
            g_signal_connect(save, "clicked", G_CALLBACK(design_save_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), save, FALSE, FALSE, 0);

            GtkWidget* save_as = gtk_button_new_with_label("Save as...");
            g_signal_connect(save_as, "clicked", G_CALLBACK(design_save_as_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), save_as, FALSE, FALSE, 0);

            GtkWidget* remove = gtk_button_new_with_label("Delete");
            g_signal_connect(remove, "clicked", G_CALLBACK(design_delete_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), remove, FALSE, FALSE, 0);

            app.default_check = gtk_check_button_new_with_label("Load at startup");
            gtk_widget_set_tooltip_text(app.default_check,
                "Load this design every time the program starts, and let\n"
                "'claymore paint --default' apply it at login");
            g_signal_connect(app.default_check, "toggled",
                             G_CALLBACK(design_default_toggled_cb), &app);
            gtk_box_pack_start(GTK_BOX(bottom), app.default_check, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(box), bottom, FALSE, FALSE, 0);

            GtkWidget* note = wrap_label(
                "<small>Your first edit puts the design on the keyboard by itself, and it stays\n"
                "there after you close this window - the keyboard keeps the last frame it is\n"
                "given, so nothing has to keep running. Untick 'Show on keyboard' to hand the\n"
                "keyboard back to its own effects. To keep a design across a reboot, save it\n"
                "and let contrib/claymore-design.service re-apply it at login.</small>");
            gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);

            return box;
        }

        /*=================================================================*\
        | Sync tab                                                           |
        \*=================================================================*/

        namespace
        {
            void sync_button_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                if(app->sync != nullptr)
                {
                    app->sync.reset();
                    app->sync_running = false;
                    if(app->device != nullptr)
                    {
                        std::string release_error;
                        app->device->set_release_on_destroy(false);
                        (void)app->device->release(release_error);
                    }
                    set_status(*app, "sync server stopped");
                    if(app->sync_button != nullptr)
                    {
                        gtk_button_set_label(GTK_BUTTON(app->sync_button), "Start sync server");
                    }
                    return;
                }

                if(app->device == nullptr)
                {
                    set_status(*app, "connect a keyboard before starting the sync server");
                    return;
                }

                if(app->streaming_now)
                {
                    set_status(*app, "stop showing the design on the keyboard first; both want direct mode");
                    return;
                }

                std::string error;
                app->device->set_release_on_destroy(true);
                app->sync = SyncServer::create(app->socket_path, app->layout->key_count, error);

                if(app->sync == nullptr)
                {
                    set_status(*app, "could not start the sync server: " + error);
                    return;
                }

                app->sync_running  = true;
                app->sync_messages = 0;

                if(app->sync_button != nullptr)
                {
                    gtk_button_set_label(GTK_BUTTON(app->sync_button), "Stop sync server");
                }

                set_status(*app, "sync server listening on " + app->socket_path);
            }
        }

        GtkWidget* build_sync_tab(AppState& app)
        {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_container_set_border_width(GTK_CONTAINER(box), 8);

            gtk_box_pack_start(GTK_BOX(box), titled("Aura Sync for Linux"), FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(box), wrap_label(
                "Start the server and any application can push colours to the keyboard over a\n"
                "UNIX datagram socket. This is the Linux stand in for the shared Aura Sync\n"
                "interface, and it lets several programs take turns driving the lighting."), FALSE, FALSE, 0);

            GtkWidget* format = wrap_label(
                "<tt>"
                "offset 0   4 bytes  magic \"RGBC\"\n"
                "offset 4   1 byte   version 1\n"
                "offset 5   1 byte   flags: bit0 sparse, bit1 release\n"
                "offset 6   2 bytes  entry count, little endian\n"
                "offset 8   payload  dense: count * RGB\n"
                "                    sparse: count * (slot, RGB)"
                "</tt>");
            gtk_box_pack_start(GTK_BOX(box), section("Wire format", format), FALSE, FALSE, 0);

            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            app.sync_button = gtk_button_new_with_label("Start sync server");
            g_signal_connect(app.sync_button, "clicked", G_CALLBACK(sync_button_cb), &app);
            gtk_box_pack_start(GTK_BOX(row), app.sync_button, FALSE, FALSE, 0);

            app.sync_label = gtk_label_new(nullptr);
            gtk_label_set_xalign(GTK_LABEL(app.sync_label), 0.0f);
            gtk_box_pack_start(GTK_BOX(row), app.sync_label, TRUE, TRUE, 0);

            gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(box), wrap_label(
                "Send a test frame with:\n"
                "<tt>printf 'RGBC\\x01\\x00\\x01\\x00\\xff\\x00\\x3c' | socat - UNIX-SENDTO:" 
                "</tt>"), FALSE, FALSE, 0);

            app.sync_view = new KeyboardView();
            app.sync_view->set_editable(false);
            GtkWidget* view_frame = gtk_frame_new("Colours arriving on the socket");
            gtk_container_add(GTK_CONTAINER(view_frame), app.sync_view->widget());
            gtk_box_pack_start(GTK_BOX(box), view_frame, TRUE, TRUE, 0);

            return box;
        }

        /*=================================================================*\
        | Header                                                             |
        \*=================================================================*/

        namespace
        {
            void reconnect_cb(GtkButton*, gpointer data)
            {
                AppState* app = static_cast<AppState*>(data);

                const char* text = gtk_entry_get_text(GTK_ENTRY(app->device_label));
                std::string path = (text == nullptr) ? std::string() : std::string(text);

                /*---------------------------------------------*\
                | The header label doubles as the device entry, so |
                | a value typed in is treated as the node to use.  |
                \*---------------------------------------------*/
                if(path == "not connected" || path.rfind("/dev/", 0) != 0)
                {
                    path.clear();
                }

                if(!connect_device(*app, path))
                {
                    set_status(*app, "still not connected; is the udev rule installed and the "
                                     "keyboard plugged in?");
                }
            }
        }

        GtkWidget* build_header(AppState& app)
        {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_container_set_border_width(GTK_CONTAINER(box), 6);

            app.device_label = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(app.device_label), "not connected");
            gtk_editable_set_editable(GTK_EDITABLE(app.device_label), FALSE);
            gtk_widget_set_size_request(app.device_label, 480, -1);
            gtk_widget_set_tooltip_text(app.device_label, "Detected device");
            gtk_box_pack_start(GTK_BOX(box), app.device_label, TRUE, TRUE, 0);

            GtkWidget* reconnect = gtk_button_new_with_label("Reconnect");
            gtk_widget_set_tooltip_text(reconnect, "Look for the keyboard again");
            g_signal_connect(reconnect, "clicked", G_CALLBACK(reconnect_cb), &app);
            gtk_box_pack_start(GTK_BOX(box), reconnect, FALSE, FALSE, 0);

            /*---------------------------------------------*\
            | Put the window away without stopping the work:  |
            | the design is kept applied, and the panel icon  |
            | brings the window back.                         |
            \*---------------------------------------------*/
            GtkWidget* to_tray = gtk_button_new_with_label("To panel");
            gtk_widget_set_tooltip_text(to_tray,
                "Hide the window and keep running with an icon in the panel.\n"
                "The design stays on the keyboard either way.");
            g_signal_connect(to_tray, "clicked", G_CALLBACK(to_tray_cb), &app);
            gtk_box_pack_start(GTK_BOX(box), to_tray, FALSE, FALSE, 0);

            app.tray_check = gtk_check_button_new_with_label("Start in the panel");
            gtk_widget_set_tooltip_text(app.tray_check,
                "Do not open a window when the program starts - just the panel\n"
                "icon. This is the setting to turn on before adding it to your\n"
                "desktop's autostart: at login the design is applied quietly and\n"
                "no window appears. 'claymore-gui --window' opens one anyway.");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.tray_check), start_in_tray());
            g_signal_connect(app.tray_check, "toggled",
                             G_CALLBACK(start_in_tray_toggled_cb), &app);
            gtk_box_pack_start(GTK_BOX(box), app.tray_check, FALSE, FALSE, 0);

            return box;
        }
    }
}
