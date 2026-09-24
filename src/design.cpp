/*---------------------------------------------------------*\
| design.cpp - reading and writing painted designs          |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/design.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

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

    std::string format_design(const Frame& design, const std::string& layout_id)
    {
        std::ostringstream out;

        out << "# rog-claymore-linux design\n";
        out << "# layout " << layout_id << "\n";
        out << "# slot colour\n";

        for(std::size_t slot = 0; slot < design.size(); slot++)
        {
            out << slot << " " << design[slot].to_hex() << "\n";
        }

        return out.str();
    }

    bool parse_design(const std::string& text, Frame& frame, int& entries, std::string& error)
    {
        entries = 0;

        if(frame.empty())
        {
            error = "the design has nowhere to go: no layout is selected";
            return false;
        }

        std::istringstream input(text);
        std::string        line;
        int                line_number = 0;

        while(std::getline(input, line))
        {
            line_number++;

            const std::size_t comment = line.find('#');
            if(comment != std::string::npos)
            {
                line.erase(comment);
            }

            std::istringstream fields(line);
            int                slot = 0;
            std::string        hex;

            if(!(fields >> slot >> hex))
            {
                continue;
            }

            /*-------------------------------------------------*\
            | A slot outside the layout is not an error: a design  |
            | painted on a keyboard with the numpad on the left    |
            | still loads, minus the numpad, if the numpad is      |
            | moved to the right.                                  |
            \*-------------------------------------------------*/
            if(slot < 0 || static_cast<std::size_t>(slot) >= frame.size())
            {
                continue;
            }

            const auto color = Rgb::parse(hex);
            if(!color)
            {
                error = "line " + std::to_string(line_number) + ": '" + hex + "' is not a colour";
                return false;
            }

            frame[static_cast<std::size_t>(slot)] = *color;
            entries++;
        }

        if(entries == 0)
        {
            error = "no usable entries found";
            return false;
        }

        return true;
    }

    bool load_design_file(const std::string& path, Frame& frame, std::string& error)
    {
        std::ifstream in(path);
        if(!in)
        {
            error = "cannot read " + path;
            return false;
        }

        std::ostringstream text;
        text << in.rdbuf();

        int entries = 0;
        if(!parse_design(text.str(), frame, entries, error))
        {
            /*-------------------------------------------------*\
            | Keep the file name, which is what the user typed and |
            | the only part of the message they can act on.        |
            \*-------------------------------------------------*/
            error = path + ": " + error;
            return false;
        }

        return true;
    }

    bool save_design_file(const std::string& path, const Frame& design,
                          const std::string& layout_id, std::string& error)
    {
        if(design.empty())
        {
            error = "nothing to save until a keyboard is connected";
            return false;
        }

        std::error_code ignored;
        const std::filesystem::path target(path);
        if(target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path(), ignored);
            if(ignored)
            {
                error = "cannot create " + target.parent_path().string() + ": " + ignored.message();
                return false;
            }
        }

        return write_text_atomic(path, format_design(design, layout_id), error);
    }

    /*=================================================================*\
    | Named designs                                                      |
    \*=================================================================*/

    std::string designs_dir()
    {
        return config_dir() + "/designs";
    }

    std::string design_path_for(const std::string& name)
    {
        return designs_dir() + "/" + name + ".txt";
    }

    std::string default_design_name()
    {
        std::ifstream in(config_dir() + "/default-design");
        if(!in)
        {
            return {};
        }

        std::string name;
        std::getline(in, name);

        /*-----------------------------------------------------*\
        | Re-sanitise on the way out: the file is editable by     |
        | hand, and its contents end up in a path.                |
        \*-----------------------------------------------------*/
        return design_name_from(name);
    }

    std::string design_name_from(const std::string& raw)
    {
        /*-----------------------------------------------------*\
        | The rule is deliberately blunt: letters, digits,        |
        | spaces, dashes and underscores survive, everything      |
        | else - including '.' and '/' - becomes an underscore.   |
        |                                                        |
        | A design name ends up in a path, and "blunt" is easier  |
        | to be sure about than "clever". A name with no letter   |
        | or digit left over is rejected, which is what rules out |
        | "..", "..." and "/".                                    |
        \*-----------------------------------------------------*/
        std::string out;
        bool        has_alnum = false;

        for(char c : raw)
        {
            const unsigned char uc = static_cast<unsigned char>(c);

            if(std::isalnum(uc) != 0)
            {
                out.push_back(c);
                has_alnum = true;
            }
            else if(c == ' ' || c == '-' || c == '_')
            {
                out.push_back(c);
            }
            else
            {
                out.push_back('_');
            }
        }

        while(!out.empty() && out.front() == ' ')
        {
            out.erase(out.begin());
        }
        while(!out.empty() && out.back() == ' ')
        {
            out.pop_back();
        }

        if(!has_alnum)
        {
            return {};
        }

        if(out.size() > 64)
        {
            out.resize(64);
        }

        return out;
    }

    std::vector<std::string> list_designs()
    {
        std::vector<std::string> names;

        std::error_code ignored;
        const std::filesystem::path dir(designs_dir());

        if(!std::filesystem::is_directory(dir, ignored))
        {
            return names;
        }

        for(const auto& entry : std::filesystem::directory_iterator(dir, ignored))
        {
            if(!entry.is_regular_file(ignored))
            {
                continue;
            }

            if(entry.path().extension() != ".txt")
            {
                continue;
            }

            names.push_back(entry.path().stem().string());
        }

        std::sort(names.begin(), names.end());
        return names;
    }

    bool save_named_design(const std::string& name, const Frame& design,
                           const std::string& layout_id, std::string& error)
    {
        const std::string safe = design_name_from(name);
        if(safe.empty())
        {
            error = "a design needs a name";
            return false;
        }

        return save_design_file(design_path_for(safe), design, layout_id, error);
    }

    bool delete_named_design(const std::string& name, std::string& error)
    {
        const std::string safe = design_name_from(name);
        if(safe.empty())
        {
            error = "no design named that";
            return false;
        }

        std::error_code ignored;
        const std::filesystem::path path(design_path_for(safe));

        if(!std::filesystem::is_regular_file(path, ignored))
        {
            error = "no design called " + safe;
            return false;
        }

        if(!std::filesystem::remove(path, ignored) || ignored)
        {
            error = "could not delete " + path.string();
            return false;
        }

        /*-----------------------------------------------------*\
        | Deleting the design that was loaded at startup must not |
        | leave a dangling default behind.                        |
        \*-----------------------------------------------------*/
        if(default_design_name() == safe)
        {
            std::filesystem::remove(config_dir() + "/default-design", ignored);
        }

        return true;
    }

    bool set_default_design(const std::string& name, std::string& error)
    {
        std::error_code ignored;
        std::filesystem::create_directories(config_dir(), ignored);

        const std::string path = config_dir() + "/default-design";

        /*-----------------------------------------------------*\
        | An empty name clears the default, which is how the GUI  |
        | turns the startup design off again.                     |
        \*-----------------------------------------------------*/
        if(name.empty())
        {
            std::filesystem::remove(path, ignored);
            return true;
        }

        const std::string safe = design_name_from(name);
        if(safe.empty())
        {
            error = "a design needs a name";
            return false;
        }

        return write_text_atomic(path, safe + "\n", error);
    }

    bool load_default_design(Frame& frame, std::string& name, std::string& error)
    {
        name = default_design_name();

        if(name.empty())
        {
            error = "no design is set to load at startup";
            return false;
        }

        return load_design_file(design_path_for(name), frame, error);
    }
}
