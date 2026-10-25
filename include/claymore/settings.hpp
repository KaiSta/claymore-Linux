/*---------------------------------------------------------*\
| settings.hpp - the user's preferences                     |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace claymore
{
    /*---------------------------------------------------------*\
    | Preferences that outlive a single run live in one small    |
    | text file:                                                 |
    |                                                            |
    |     # ~/.config/claymore/settings                          |
    |     start-in-tray on                                       |
    |                                                            |
    | Deliberately a key/value file rather than a growing pile of |
    | one-value files, and deliberately in the core rather than   |
    | in the GUI: the autostart story is that the GUI reads this  |
    | at launch, and the CLI can set it from a script during      |
    | installation. Two implementations of the same file is the   |
    | bug this avoids - the design file taught that lesson        |
    | already.                                                    |
    |                                                            |
    | Unknown keys are preserved on write, so a newer version's   |
    | setting is not dropped by an older one.                     |
    \*---------------------------------------------------------*/

    /*---------------------------------------------------------*\
    | ~/.config/claymore, honouring XDG_CONFIG_HOME, and          |
    | CLAYMORE_CONFIG_DIR for tests that must not touch a real    |
    | home directory.                                             |
    \*---------------------------------------------------------*/
    std::string config_dir();

    std::string settings_path();

    /*---------------------------------------------------------*\
    | The whole file, sorted by key. Empty when there is none.     |
    \*---------------------------------------------------------*/
    std::vector<std::pair<std::string, std::string>> list_settings();

    std::string setting_get(const std::string& key, const std::string& fallback = {});

    /*---------------------------------------------------------*\
    | Writes one key. An empty value removes it, which is how a    |
    | setting goes back to its default.                            |
    \*---------------------------------------------------------*/
    bool setting_set(const std::string& key, const std::string& value, std::string& error);

    /*---------------------------------------------------------*\
    | Booleans are stored as on/off. Anything else in the file -    |
    | 'yes', '1', 'true' - also reads as on, because hand editing  |
    | the file is expected.                                        |
    \*---------------------------------------------------------*/
    bool setting_get_bool(const std::string& key, bool fallback);
    bool setting_set_bool(const std::string& key, bool value, std::string& error);

    /*---------------------------------------------------------*\
    | Start with the window hidden and only the panel icon shown.  |
    |                                                            |
    | This is the setting that makes an autostart entry usable:    |
    | at login nobody wants a window, they want the design applied |
    | quietly. The GUI reads it at launch and --tray / --no-tray   |
    | override it for one run.                                    |
    \*---------------------------------------------------------*/
    constexpr const char* SETTING_START_IN_TRAY = "start-in-tray";

    bool start_in_tray();
    bool set_start_in_tray(bool on, std::string& error);
}
