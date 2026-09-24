/*---------------------------------------------------------*\
| settings.cpp - reading and writing the user's preferences |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <unordered_map>

namespace claymore
{
    namespace
    {
        bool write_text_atomic(const std::string& path, const std::string& text,
                               std::string& error)
        {
            const std::filesystem::path target(path);
            const std::string temporary = path + ".tmp." + std::to_string(::getpid());

            std::ofstream out(temporary, std::ios::trunc);
            if(!out)
            {
                error = "cannot write " + temporary;
                return false;
            }
            out << text;
            out.close();
            if(!out)
            {
                error = "failed while writing " + temporary;
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }

            std::error_code rename_error;
            std::filesystem::rename(temporary, target, rename_error);
            if(rename_error)
            {
                error = "cannot replace " + path + ": " + rename_error.message();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
            return true;
        }
    }

    std::string config_dir()
    {
        /*-----------------------------------------------------*\
        | CLAYMORE_CONFIG_DIR exists so that tests - and anyone   |
        | scripting this - can work somewhere other than a real   |
        | home directory.                                         |
        \*-----------------------------------------------------*/
        if(const char* override_dir = std::getenv("CLAYMORE_CONFIG_DIR"))
        {
            if(*override_dir != '\0')
            {
                return override_dir;
            }
        }

        if(const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        {
            if(*xdg != '\0')
            {
                return std::string(xdg) + "/claymore";
            }
        }

        if(const char* home = std::getenv("HOME"))
        {
            if(*home != '\0')
            {
                return std::string(home) + "/.config/claymore";
            }
        }

        return "claymore";
    }

    std::string settings_path()
    {
        return config_dir() + "/settings";
    }

    namespace
    {
        /*-----------------------------------------------------*\
        | One pass over the file, keeping the last value for a key |
        | so that a hand-edited duplicate cannot win by accident.  |
        \*-----------------------------------------------------*/
        std::unordered_map<std::string, std::string> read_settings()
        {
            std::unordered_map<std::string, std::string> values;

            std::ifstream in(settings_path());
            if(!in)
            {
                return values;
            }

            std::string line;

            while(std::getline(in, line))
            {
                const std::size_t comment = line.find('#');
                if(comment != std::string::npos)
                {
                    line.erase(comment);
                }

                std::istringstream fields(line);
                std::string        key;
                std::string        value;

                if(!(fields >> key >> value))
                {
                    continue;
                }

                values[key] = value;
            }

            return values;
        }
    }

    std::vector<std::pair<std::string, std::string>> list_settings()
    {
        std::unordered_map<std::string, std::string> values = read_settings();

        std::vector<std::pair<std::string, std::string>> out(values.begin(), values.end());

        std::sort(out.begin(), out.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        return out;
    }

    std::string setting_get(const std::string& key, const std::string& fallback)
    {
        const auto values = read_settings();

        const auto found = values.find(key);
        if(found == values.end())
        {
            return fallback;
        }

        return found->second;
    }

    bool setting_set(const std::string& key, const std::string& value, std::string& error)
    {
        if(key.empty())
        {
            error = "a setting needs a name";
            return false;
        }

        /*-----------------------------------------------------*\
        | Rewrite the whole file: it is a handful of lines, and    |
        | preserving the keys we do not know about is what keeps  |
        | a downgrade from silently discarding settings.          |
        \*-----------------------------------------------------*/
        std::unordered_map<std::string, std::string> values = read_settings();

        if(value.empty())
        {
            values.erase(key);
        }
        else
        {
            values[key] = value;
        }

        std::error_code ignored;
        std::filesystem::create_directories(config_dir(), ignored);
        if(ignored)
        {
            error = "cannot create " + config_dir() + ": " + ignored.message();
            return false;
        }

        std::ostringstream out;
        out << "# rog-claymore-linux settings\n";
        out << "# key value - one per line; '#' starts a comment\n";

        std::vector<std::pair<std::string, std::string>> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        for(const auto& entry : sorted)
        {
            out << entry.first << " " << entry.second << "\n";
        }

        return write_text_atomic(settings_path(), out.str(), error);
    }

    bool setting_get_bool(const std::string& key, bool fallback)
    {
        std::string value = setting_get(key);

        if(value.empty())
        {
            return fallback;
        }

        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if(value == "on" || value == "yes" || value == "true" || value == "1")
        {
            return true;
        }

        if(value == "off" || value == "no" || value == "false" || value == "0")
        {
            return false;
        }

        /*-----------------------------------------------------*\
        | Something unreadable is not a reason to refuse to start: |
        | fall back and let the user fix the file.                 |
        \*-----------------------------------------------------*/
        return fallback;
    }

    bool setting_set_bool(const std::string& key, bool value, std::string& error)
    {
        return setting_set(key, value ? "on" : "off", error);
    }

    bool start_in_tray()
    {
        return setting_get_bool(SETTING_START_IN_TRAY, false);
    }

    bool set_start_in_tray(bool on, std::string& error)
    {
        return setting_set_bool(SETTING_START_IN_TRAY, on, error);
    }
}
