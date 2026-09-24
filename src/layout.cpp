/*---------------------------------------------------------*\
| layout.cpp - Claymore layout lookup helpers               |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "claymore/layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <sstream>

namespace claymore
{
    /*---------------------------------------------------------*\
    | Provided by the generated layout_data.cpp                  |
    \*---------------------------------------------------------*/
    namespace layout_data
    {
        extern const LayoutVariant* const all_claymore_variants;
        extern const int claymore_variant_count;
    }

    namespace
    {
        std::string lowered(const std::string& text)
        {
            std::string out = text;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        int variant_index(const std::string& id)
        {
            for(int i = 0; i < layout_data::claymore_variant_count; i++)
            {
                if(id == layout_data::all_claymore_variants[i].id)
                {
                    return i;
                }
            }
            return -1;
        }
    }

    std::string variant_id(Numpad numpad, Region region)
    {
        std::string id;
        switch(numpad)
        {
            case Numpad::Right: id = "numpad-right"; break;
            case Numpad::Left:  id = "numpad-left";  break;
            case Numpad::None:  id = "no-numpad";    break;
        }
        id += (region == Region::UK) ? "-uk" : "-us";
        return id;
    }

    const LayoutVariant* find_variant(const std::string& id)
    {
        const int index = variant_index(id);
        if(index < 0)
        {
            return nullptr;
        }
        return &layout_data::all_claymore_variants[index];
    }

    std::vector<std::string> all_variant_ids()
    {
        std::vector<std::string> ids;
        ids.reserve(static_cast<std::size_t>(layout_data::claymore_variant_count));
        for(int i = 0; i < layout_data::claymore_variant_count; i++)
        {
            ids.emplace_back(layout_data::all_claymore_variants[i].id);
        }
        return ids;
    }

    std::optional<KeyPos> find_key_pos(const LayoutVariant& variant, int frame_slot)
    {
        for(int row = 0; row < variant.rows; row++)
        {
            for(int col = 0; col < variant.cols; col++)
            {
                if(variant.matrix[row * variant.cols + col] == frame_slot)
                {
                    return KeyPos{ row, col };
                }
            }
        }
        return std::nullopt;
    }

    /*---------------------------------------------------------*\
    | Canvas geometry. See layout.hpp for why this is not in the  |
    | GUI: it is the part of the canvas worth testing.            |
    \*---------------------------------------------------------*/
    KeyRect key_rect(const LayoutVariant& variant, int row, int col,
                     double canvas_width, double canvas_height, const KeyGrid& grid)
    {
        KeyRect rect;

        if(variant.rows <= 0 || variant.cols <= 0)
        {
            return rect;
        }

        const double available_width  = std::max(1.0, canvas_width  - 2.0 * grid.padding);
        const double available_height = std::max(1.0, canvas_height - 2.0 * grid.padding);

        const double cell_width  = (available_width  - (variant.cols - 1) * grid.gap) / variant.cols;
        const double cell_height = (available_height - (variant.rows - 1) * grid.gap) / variant.rows;

        rect.width  = std::max(2.0, cell_width);
        rect.height = std::max(2.0, cell_height);
        rect.x      = grid.padding + col * (cell_width  + grid.gap);
        rect.y      = grid.padding + row * (cell_height + grid.gap);
        return rect;
    }

    int hit_test_slot(const LayoutVariant& variant, double px, double py,
                      double canvas_width, double canvas_height,
                      const KeyGrid& grid, double snap)
    {
        if(variant.matrix == nullptr || variant.rows <= 0 || variant.cols <= 0)
        {
            return -1;
        }

        int    nearest      = -1;
        double nearest_gap  = snap;

        for(int row = 0; row < variant.rows; row++)
        {
            for(int col = 0; col < variant.cols; col++)
            {
                const int slot = variant.matrix[row * variant.cols + col];
                if(slot < 0)
                {
                    continue;
                }

                const KeyRect rect = key_rect(variant, row, col, canvas_width, canvas_height, grid);

                /*---------------------------------------------*\
                | On the key: done, and the first hit wins so    |
                | overlapping tolerances cannot steal each other. |
                \*---------------------------------------------*/
                if(px >= rect.x && px <= rect.x + rect.width &&
                   py >= rect.y && py <= rect.y + rect.height)
                {
                    return slot;
                }

                /*---------------------------------------------*\
                | In the gap: remember the closest key edge that |
                | is still within reach.                        |
                \*---------------------------------------------*/
                const double dx = std::max(0.0, std::max(rect.x - px, px - (rect.x + rect.width)));
                const double dy = std::max(0.0, std::max(rect.y - py, py - (rect.y + rect.height)));
                const double distance = std::sqrt(dx * dx + dy * dy);

                if(distance < nearest_gap)
                {
                    nearest_gap = distance;
                    nearest     = slot;
                }
            }
        }

        return nearest;
    }

    const KeyName* find_key(const LayoutVariant& variant, const std::string& query)
    {
        const std::string wanted = lowered(query);

        /*-----------------------------------------------------*\
        | Numeric device index: decimal or 0x-prefixed           |
        \*-----------------------------------------------------*/
        {
            char*        end   = nullptr;
            errno = 0;
            const long   value = std::strtol(query.c_str(), &end, 0);
            const bool   plain = !query.empty() && errno != ERANGE &&
                                 end != nullptr && *end == '\0' &&
                                 value >= 0 && value <= 0xFF;
            const bool   dec   = plain &&
                                 std::all_of(query.begin(), query.end(), [](unsigned char c)
                                 {
                                     return std::isdigit(c) != 0;
                                 });

            if(plain && (dec || query.rfind("0x", 0) == 0 || query.rfind("0X", 0) == 0))
            {
                for(int i = 0; i < variant.key_count; i++)
                {
                    if(variant.keys[i].index == static_cast<std::uint8_t>(value))
                    {
                        return &variant.keys[i];
                    }
                }
                return nullptr;
            }
        }

        for(int i = 0; i < variant.key_count; i++)
        {
            if(wanted == lowered(variant.keys[i].slug) ||
               wanted == lowered(variant.keys[i].name))
            {
                return &variant.keys[i];
            }
        }
        return nullptr;
    }

    std::string render_layout(const LayoutVariant& variant)
    {
        /*-----------------------------------------------------*\
        | Sub-cell width so that two LEDs fit comfortably.       |
        \*-----------------------------------------------------*/
        constexpr int cell_width = 5;
        constexpr int cell_height = 1;

        std::ostringstream out;
        out << variant.id << "\n";

        /*-----------------------------------------------------*\
        | Column ruler (tens/units)                              |
        \*-----------------------------------------------------*/
        out << "    ";
        for(int col = 0; col < variant.cols; col++)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%-*d", cell_width, col / 10);
            out << buffer;
        }
        out << "\n    ";
        for(int col = 0; col < variant.cols; col++)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%-*d", cell_width, col % 10);
            out << buffer;
        }
        out << "\n";

        for(int row = 0; row < variant.rows; row++)
        {
            for(int line = 0; line < cell_height; line++)
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%3d ", row);
                out << buffer;
                for(int col = 0; col < variant.cols; col++)
                {
                    const int slot = variant.matrix[row * variant.cols + col];
                    char cell[16];
                    if(slot < 0)
                    {
                        std::snprintf(cell, sizeof(cell), "%-*s", cell_width, ".");
                    }
                    else
                    {
                        std::snprintf(cell, sizeof(cell), "%-*d", cell_width, slot);
                    }
                    out << cell;
                }
                out << "\n";
            }
        }

        out << "\nLegend is the frame slot (0.." << variant.key_count - 1
            << "); use 'claymore keys' for device indices.\n";
        return out.str();
    }
}
