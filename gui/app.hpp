/*---------------------------------------------------------*\
| app.hpp - shared GUI state                                |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>
#include <vector>

#include "claymore/device.hpp"
#include "claymore/effects.hpp"
#include "claymore/layout.hpp"
#include "claymore/sync_server.hpp"
#include "keyboard_view.hpp"

namespace claymore
{
    namespace gui
    {
        struct AppState
        {
            /*---------------------------------------------*\
            | Model                                        |
            \*---------------------------------------------*/
            DetectOptions                  detect;
            std::unique_ptr<ClaymoreDevice> device;
            const LayoutVariant*           layout = nullptr;

            EffectSpec                     spec;
            Frame                          design;      /* custom per key design  */
            std::string                    profile_path;

            Brush                          brush       = Brush::Paint;
            Rgb                            brush_color = Rgb(0xFF, 0x00, 0x3C);

            bool                           live_apply    = false;

            /*-------------------------------------------*\
            | Applying an effect always commits it to     |
            | flash on this keyboard, so last_applied     |
            | lets us skip redundant writes when nothing  |
            | actually changed.                           |
            \*-------------------------------------------*/
            std::string                    last_applied;

            /*-------------------------------------------*\
            | Custom design plumbing.                     |
            |                                             |
            | `stream_custom` is what the user asked for, |
            | `streaming_now` is what the keyboard is     |
            | actually being driven with. They differ     |
            | until the design has been edited, because   |
            | holding direct mode pauses the firmware     |
            | effects and grabbing it at startup would    |
            | make the Effects tab look broken.           |
            |                                             |
            | An edit is what makes the user expect to    |
            | see their paint on the board, so the first  |
            | one starts the stream by itself.            |
            \*-------------------------------------------*/
            bool                           stream_custom  = true;
            bool                           design_edited  = false;
            bool                           streaming_now  = false;
            bool                           sync_running   = false;

            /*-------------------------------------------*\
            | Set when the design has been handed to the  |
            | keyboard on purpose. The Claymore keeps the |
            | last frame it is given, so NOT revoking on   |
            | exit is what makes a design survive the      |
            | window closing, the way a firmware effect    |
            | survives it. It also means the board's own   |
            | effects stay paused until 'claymore release' |
            | or another effect is applied, so it is an    |
            | explicit action rather than a side effect.   |
            \*-------------------------------------------*/
            bool                           design_kept    = false;

            std::unique_ptr<SyncServer>    sync;
            std::string                    socket_path;
            int                            sync_messages = 0;

            double                         now           = 0.0;
            double                         apply_due     = -1.0; /* debounce for live apply */
            std::string                    selected_preset;

            /*-------------------------------------------*\
            | False once the window has been destroyed.   |
            | The redraw tick is a GLib timeout, so it    |
            | keeps firing after the widgets are gone and |
            | would queue draws on freed memory. Nothing  |
            | else closes the window, which is why this    |
            | went unnoticed until a test did.            |
            \*-------------------------------------------*/
            bool                           running       = true;

            /*-------------------------------------------*\
            | The name of the design being edited, empty  |
            | until it has been saved once. The file's    |
            | own contents are the only copy, so saving   |
            | happens on every edit that matters and on   |
            | exit.                                       |
            \*-------------------------------------------*/
            std::string                    design_name;

            /*-------------------------------------------*\
            | True once the saved startup design has been |
            | offered to the canvas. Doing it once on the |
            | first tick means the design wins on launch  |
            | instead of racing the preset selection.     |
            \*-------------------------------------------*/
            bool                           design_picker_busy = false;

            /*-------------------------------------------*\
            | Whether to open a window at all.              |
            |                                              |
            | Filled in from the 'start-in-tray' setting so |
            | that an autostart entry needs no arguments at |
            | all, with --tray and --window overriding it   |
            | for one run. The window appearing at login is |
            | the thing this exists to avoid.               |
            \*-------------------------------------------*/
            bool                           start_hidden          = false;
            bool                           load_default_on_start = true;

            /*---------------------------------------------*\
            | Widgets that mirror model state                |
            \*---------------------------------------------*/
            GtkWidget*    window        = nullptr;
            GtkStatusIcon* tray_icon    = nullptr;

            KeyboardView* preview       = nullptr;
            KeyboardView* custom        = nullptr;
            KeyboardView* sync_view     = nullptr;

            GtkWidget*    device_label  = nullptr;
            GtkWidget*    status_label  = nullptr;
            GtkWidget*    preset_list   = nullptr;
            GtkWidget*    preset_title  = nullptr;
            GtkWidget*    preset_note   = nullptr;
            GtkWidget*    speed_scale   = nullptr;
            GtkWidget*    speed_label   = nullptr;
            GtkWidget*    bright_scale  = nullptr;
            GtkWidget*    bright_label  = nullptr;
            GtkWidget*    direction     = nullptr;
            GtkWidget*    random_check  = nullptr;
            GtkWidget*    live_check    = nullptr;
            GtkWidget*    colors_box    = nullptr;
            GtkWidget*    color_note    = nullptr;
            GtkWidget*    brush_paint   = nullptr;
            GtkWidget*    brush_erase   = nullptr;
            GtkWidget*    brush_pick    = nullptr;
            GtkWidget*    brush_color_b = nullptr;
            GtkWidget*    canvas_frame  = nullptr;
            GtkWidget*    stream_check  = nullptr;
            GtkWidget*    design_combo  = nullptr; /* saved designs           */
            GtkWidget*    default_check = nullptr; /* load this one at startup */
            GtkWidget*    tray_check    = nullptr; /* start in the panel      */
            GtkWidget*    sync_label    = nullptr;
            GtkWidget*    sync_button   = nullptr;
            GtkWidget*    sync_socket   = nullptr;
        };

        void set_status(AppState& app, const std::string& message);

        /*-----------------------------------------------------*\
        | Pushes the current layout, design and brush into the   |
        | three canvases. Must be called again whenever they are  |
        | (re)created, because a canvas built after the device    |
        | was opened would otherwise never learn the layout and   |
        | would draw its "no keyboard" placeholder forever.      |
        \*-----------------------------------------------------*/
        void attach_views(AppState& app);

        /*-----------------------------------------------------*\
        | Opens a device and adopts its layout. Returns false    |
        | when nothing could be opened, leaving any previous     |
        | device in place would be wrong, so it clears it.       |
        \*-----------------------------------------------------*/
        bool connect_device(AppState& app, const std::string& device_path);

        /*-----------------------------------------------------*\
        | Sends the current spec to the keyboard right now.      |
        |                                                       |
        | No-ops when the spec has not changed since the last    |
        | successful apply, because committing an effect writes  |
        | the keyboard's flash and there is no reason to do it   |
        | twice for identical settings.                          |
        \*-----------------------------------------------------*/
        void apply_effect_now(AppState& app);

        /*-----------------------------------------------------*\
        | Queues an apply a fraction of a second in the future   |
        | so dragging a slider does not flood the keyboard.      |
        \*-----------------------------------------------------*/
        void schedule_apply(AppState& app);

        /*-----------------------------------------------------*\
        | Rebuilds the colour swatch row and enables or disables |
        | controls that the current effect does not use.         |
        \*-----------------------------------------------------*/
        void refresh_effect_controls(AppState& app);

        /*-----------------------------------------------------*\
        | The frame the custom tab should show and stream.       |
        \*-----------------------------------------------------*/
        Frame current_custom_frame(const AppState& app);

        /*-----------------------------------------------------*\
        | Design file IO. The format and the store live in        |
        | claymore/design.hpp so that the CLI, the GUI and a      |
        | systemd unit all read and write exactly the same thing. |
        |                                                         |
        | A design is saved under a name in the user's config     |
        | directory, which is what makes it something the user    |
        | can pick again, and pick as the startup design.         |
        \*-----------------------------------------------------*/
        std::string default_design_name_for_new(AppState& app);
        bool save_design(AppState& app, const std::string& name, std::string& error);
        bool load_design(AppState& app, const std::string& name, std::string& error);

        /*-----------------------------------------------------*\
        | Rebuilds the design picker from what is on disk, and    |
        | selects `select` if it is there.                        |
        \*-----------------------------------------------------*/
        void refresh_design_picker(AppState& app, const std::string& select);

        GtkWidget* build_header(AppState& app);
        GtkWidget* build_effects_tab(AppState& app);
        GtkWidget* build_custom_tab(AppState& app);
        GtkWidget* build_sync_tab(AppState& app);
    }
}
