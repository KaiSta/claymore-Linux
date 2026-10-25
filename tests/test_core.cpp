/*---------------------------------------------------------*\
| test_core.cpp - hardware-free test suite                  |
|                                                           |
|   Part of Claymore@Linux                                  |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
|                                                           |
|   Everything here runs without a keyboard attached, which  |
|   matters because the packet layouts are the part most     |
|   likely to be broken by a careless edit.                  |
\*---------------------------------------------------------*/

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "claymore/design.hpp"
#include "claymore/device.hpp"
#include "claymore/effects.hpp"
#include "claymore/layout.hpp"
#include "claymore/protocol.hpp"
#include "claymore/settings.hpp"
#include "claymore/sync_server.hpp"

using namespace claymore;

namespace
{
    int checks_run    = 0;
    int checks_failed = 0;
    std::string current_section;

    void section(const std::string& name)
    {
        current_section = name;
        std::printf("\n%s\n", name.c_str());
    }

    void check(bool condition, const std::string& description)
    {
        checks_run++;
        if(condition)
        {
            return;
        }
        checks_failed++;
        std::printf("  FAIL  %s\n", description.c_str());
    }

    void check_eq(long long actual, long long expected, const std::string& description)
    {
        checks_run++;
        if(actual == expected)
        {
            return;
        }
        checks_failed++;
        std::printf("  FAIL  %s (expected %lld, got %lld)\n",
                    description.c_str(), expected, actual);
    }

    void check_text(const std::string& actual, const std::string& expected,
                    const std::string& description)
    {
        check(actual == expected,
              description + " (expected '" + expected + "', got '" + actual + "')");
    }

    /*---------------------------------------------------------*\
    | Colour parsing                                              |
    \*---------------------------------------------------------*/
    void test_colors()
    {
        section("colours");

        const Rgb magenta = *Rgb::parse("ff00ff");
        check_eq(magenta.r, 255, "ff00ff parses red channel");
        check_eq(magenta.g, 0,   "ff00ff parses green channel");
        check_eq(magenta.b, 255, "ff00ff parses blue channel");

        check(*Rgb::parse("#ff00ff") == magenta, "# prefix accepted");
        check(*Rgb::parse("0xff00ff") == magenta, "0x prefix accepted");
        check(*Rgb::parse("f0f") == magenta, "three digit shorthand expanded");
        check(*Rgb::parse("FF00FF") == magenta, "uppercase accepted");
        check(*Rgb::parse("magenta") == magenta, "named colour accepted");
        check(*Rgb::parse("rog-red") == Rgb(0xFF, 0x00, 0x3C), "hyphenated name accepted");

        check(*Rgb::parse("255,0,255") == magenta, "decimal triple accepted");
        check(*Rgb::parse("300,0,255") == Rgb(255, 0, 255), "decimal triple clamped");

        check(!Rgb::parse("nonsense").has_value(), "garbage rejected");
        check(!Rgb::parse("").has_value(), "empty string rejected");
        check(!Rgb::parse("ff00f").has_value(), "wrong length rejected");

        check_eq(magenta.to_hex() == "FF00FF", true, "to_hex round trips");

        /*-----------------------------------------------------*\
        | HSV                                                        |
        \*-----------------------------------------------------*/
        check(*Rgb::parse("ff0000") == Rgb::from_hsv(0, 1, 1),     "hsv 0 is red");
        check(*Rgb::parse("00ff00") == Rgb::from_hsv(120, 1, 1),   "hsv 120 is green");
        check(*Rgb::parse("0000ff") == Rgb::from_hsv(240, 1, 1),   "hsv 240 is blue");
        check(Rgb::from_hsv(400, 1, 1) == Rgb::from_hsv(40, 1, 1), "hue wraps");
        check(Rgb(0, 0, 0) == Rgb::from_hsv(123, 0, 0),            "zero value is black");

        const Rgb half = scale(Rgb(200, 100, 50), 0.5);
        check_eq(half.r, 100, "scale halves red");
        check_eq(half.g, 50,  "scale halves green");
        check_eq(half.b, 25,  "scale halves blue");

        const Rgb middle = lerp(Rgb(0, 0, 0), Rgb(200, 100, 40), 0.5);
        check_eq(middle.r, 100, "lerp midpoint red");
        check_eq(middle.g, 50,  "lerp midpoint green");
    }

    /*---------------------------------------------------------*\
    | Canvas geometry.                                            |
    |                                                             |
    | "Clicking a key in the design tab does nothing" is a bug      |
    | that no other test can see, so the arithmetic behind the      |
    | canvas is checked here rather than with a mouse.              |
    \*---------------------------------------------------------*/
    void test_canvas_hit_testing()
    {
        section("canvas geometry");

        const LayoutVariant* variant = find_variant("numpad-left-uk");
        check(variant != nullptr, "a variant is available to hit test against");
        if(variant == nullptr)
        {
            return;
        }

        constexpr double WIDTH  = 1160.0;
        constexpr double HEIGHT = 606.0;
        const KeyGrid    grid{};

        /*-----------------------------------------------------*\
        | Every key must be inside the canvas and must not       |
        | overlap its neighbour.                                 |
        \*-----------------------------------------------------*/
        {
            bool   all_inside = true;
            bool   no_overlap = true;
            double widest     = 0.0;

            for(int row = 0; row < variant->rows; row++)
            {
                for(int col = 0; col < variant->cols; col++)
                {
                    if(variant->matrix[row * variant->cols + col] < 0)
                    {
                        continue;
                    }

                    const KeyRect rect = key_rect(*variant, row, col, WIDTH, HEIGHT, grid);

                    all_inside = all_inside &&
                                 rect.x >= 0.0 && rect.y >= 0.0 &&
                                 rect.x + rect.width  <= WIDTH  + 0.001 &&
                                 rect.y + rect.height <= HEIGHT + 0.001;

                    widest = std::max(widest, rect.width);

                    if(col + 1 < variant->cols)
                    {
                        const KeyRect right = key_rect(*variant, row, col + 1, WIDTH, HEIGHT, grid);
                        no_overlap = no_overlap && rect.x + rect.width <= right.x + 0.001;
                    }
                }
            }

            check(all_inside, "every key fits inside the canvas");
            check(no_overlap, "neighbouring keys do not overlap");
            check(widest > 20.0, "keys are big enough to aim at");
        }

        /*-----------------------------------------------------*\
        | The centre of a key must hit that same key. This is    |
        | the check that would have caught a canvas whose        |
        | painting and clicking disagreed.                       |
        \*-----------------------------------------------------*/
        {
            int hits = 0;
            int keys = 0;

            for(int row = 0; row < variant->rows; row++)
            {
                for(int col = 0; col < variant->cols; col++)
                {
                    const int slot = variant->matrix[row * variant->cols + col];
                    if(slot < 0)
                    {
                        continue;
                    }

                    keys++;

                    const KeyRect rect = key_rect(*variant, row, col, WIDTH, HEIGHT, grid);
                    const int     hit  = hit_test_slot(*variant,
                                                       rect.x + rect.width  / 2.0,
                                                       rect.y + rect.height / 2.0,
                                                       WIDTH, HEIGHT, grid, 5.0);
                    if(hit == slot)
                    {
                        hits++;
                    }
                }
            }

            check_eq(hits, keys, "clicking the centre of a key selects that key");
        }

        /*-----------------------------------------------------*\
        | Clicks that land in the gaps between keys, or a little |
        | outside the canvas, should still paint the nearest     |
        | key rather than being silently dropped.                |
        \*-----------------------------------------------------*/
        {
            /*---------------------------------------------*\
            | Find a pair of real neighbours: the top rows    |
            | start with empty cells (the Escape row is not    |
            | a full keyboard row).                            |
            \*---------------------------------------------*/
            int row = -1;
            int col = -1;

            for(int r = 0; r < variant->rows && row < 0; r++)
            {
                for(int c = 0; c + 1 < variant->cols; c++)
                {
                    if(variant->matrix[r * variant->cols + c]     >= 0 &&
                       variant->matrix[r * variant->cols + c + 1] >= 0)
                    {
                        row = r;
                        col = c;
                        break;
                    }
                }
            }

            check(row >= 0, "the layout has adjacent keys to test the gaps between");

            if(row >= 0)
            {
                const KeyRect left  = key_rect(*variant, row, col,     WIDTH, HEIGHT, grid);
                const KeyRect right = key_rect(*variant, row, col + 1, WIDTH, HEIGHT, grid);

                const double in_gap_x = (left.x + left.width + right.x) / 2.0;
                const int    in_gap   = hit_test_slot(*variant, in_gap_x,
                                                      left.y + left.height / 2.0,
                                                      WIDTH, HEIGHT, grid, 5.0);
                check(in_gap >= 0, "a click in the gap between two keys still hits a key");

                /*-----------------------------------------*\
                | The tolerance must stay small: a point far  |
                | above the row is not any key's business.    |
                \*-----------------------------------------*/
                const int well_above = hit_test_slot(*variant, left.x + left.width / 2.0,
                                                     left.y - 40.0,
                                                     WIDTH, HEIGHT, grid, 5.0);
                check(well_above != variant->matrix[row * variant->cols + col],
                      "a click 40px above a key does not paint it");
            }

            const int outside = hit_test_slot(*variant, -200.0, -200.0, WIDTH, HEIGHT, grid, 5.0);
            check_eq(outside, -1, "a click far outside the canvas hits nothing");

            const int beyond = hit_test_slot(*variant, WIDTH + 50.0, HEIGHT + 50.0,
                                             WIDTH, HEIGHT, grid, 5.0);
            check_eq(beyond, -1, "a click beyond the bottom right corner hits nothing");
        }

        /*-----------------------------------------------------*\
        | A tiny canvas must not produce negative or zero sized  |
        | keys, which would make hit testing meaningless.        |
        \*-----------------------------------------------------*/
        {
            const KeyRect tiny = key_rect(*variant, 0, 0, 4.0, 4.0, grid);
            check(tiny.width >= 2.0 && tiny.height >= 2.0, "degenerate canvases still give a rect");
        }
    }

    /*---------------------------------------------------------*\
    | Design files.                                               |
    |                                                             |
    | The GUI saves these, the CLI loads them, and a systemd unit  |
    | is expected to re-apply one at login, so the format has to   |
    | survive a round trip through text exactly.                   |
    \*---------------------------------------------------------*/
    void test_design_files()
    {
        section("design files");

        const LayoutVariant* variant = find_variant("numpad-left-uk");
        check(variant != nullptr, "a layout is available for design tests");
        if(variant == nullptr)
        {
            return;
        }

        const std::size_t keys = static_cast<std::size_t>(variant->key_count);

        /*-----------------------------------------------------*\
        | Round trip.                                           |
        \*-----------------------------------------------------*/
        {
            Frame design(keys, Rgb(0, 0, 0));
            design[0]  = Rgb(0xFF, 0x00, 0x3C);
            design[5]  = Rgb(0x00, 0x00, 0xFF);
            design[99] = Rgb(0xFF, 0xFF, 0xFF);

            const std::string text = format_design(design, variant->id);
            check(text.find("# layout numpad-left-uk") != std::string::npos,
                  "a saved design records which layout it was painted on");

            Frame       reloaded(keys, Rgb(1, 2, 3));
            int         entries = 0;
            std::string error;
            check(parse_design(text, reloaded, entries, error),
                  "a saved design loads back: " + error);
            check_eq(entries, static_cast<long long>(keys), "every key came back");
            check(reloaded == design, "the round trip is lossless");
        }

        /*-----------------------------------------------------*\
        | Comments, blank lines and slots from a layout that     |
        | has since changed.                                     |
        \*-----------------------------------------------------*/
        {
            const std::string text =
                "# a comment\n"
                "\n"
                "   \n"
                "3 ff8000   # trailing comment\n"
                "9999 ffffff\n"
                "-4 000000\n";

            Frame       frame(keys, Rgb(0, 0, 0));
            int         entries = 0;
            std::string error;
            check(parse_design(text, frame, entries, error),
                  "comments and out of range slots are tolerated: " + error);
            check_eq(entries, 1, "only the one usable entry was applied");
            check(frame[3] == Rgb(0xFF, 0x80, 0x00), "the usable entry landed on the right slot");
            check(frame[4].is_black(), "untouched slots stay black");
        }

        /*-----------------------------------------------------*\
        | A typo must report itself rather than silently         |
        | producing a half loaded design.                        |
        \*-----------------------------------------------------*/
        {
            Frame       frame(keys, Rgb(0, 0, 0));
            int         entries = 0;
            std::string error;
            check(!parse_design("3 notacolour\n", frame, entries, error),
                  "a bad colour fails the load");
            check(error.find("notacolour") != std::string::npos,
                  "the error names what was wrong: " + error);

            check(!parse_design("# nothing but comments\n", frame, entries, error),
                  "a design with no entries fails rather than silently doing nothing");

            Frame empty;
            check(!parse_design("0 ff0000\n", empty, entries, error),
                  "a design needs a layout to land in");
        }

        /*-----------------------------------------------------*\
        | Writing to a directory that does not exist yet, which  |
        | is the normal case on a fresh machine: ~/.config/      |
        | claymore is created on demand.                         |
        \*-----------------------------------------------------*/
        {
            Frame design(keys, Rgb(0, 0, 0));
            design[1] = Rgb(0x12, 0x34, 0x56);

            const std::string dir  = "/tmp/claymore-design-test/nested";
            const std::string path = dir + "/design.txt";

            std::error_code ignored;
            std::filesystem::remove_all("/tmp/claymore-design-test", ignored);

            std::string error;
            check(save_design_file(path, design, variant->id, error),
                  "saving creates the config directory: " + error);

            Frame loaded(keys, Rgb(0, 0, 0));
            check(load_design_file(path, loaded, error), "the saved file loads: " + error);
            check(loaded[1] == Rgb(0x12, 0x34, 0x56), "the saved colour came back");

            Frame       missing(keys, Rgb(0, 0, 0));
            check(!load_design_file(dir + "/nothing-here.txt", missing, error),
                  "a missing file fails with an error");
            check(error.find("nothing-here") != std::string::npos,
                  "the error names the file: " + error);

            std::filesystem::remove_all("/tmp/claymore-design-test", ignored);
        }

        /*-----------------------------------------------------*\
        | Named designs and the startup default.                 |
        |                                                        |
        | CLAYMORE_CONFIG_DIR keeps this out of a real home       |
        | directory, which also makes the seam itself worth       |
        | checking: if the override stopped working, these tests  |
        | would start writing into ~/.config.                     |
        \*-----------------------------------------------------*/
        {
            std::error_code ignored;
            const std::string sandbox = "/tmp/claymore-design-store-test";
            std::filesystem::remove_all(sandbox, ignored);
            setenv("CLAYMORE_CONFIG_DIR", sandbox.c_str(), 1);

            check_text(config_dir(), sandbox, "the config directory honours the override");
            check_text(designs_dir(), sandbox + "/designs", "designs live in a subdirectory");

            check_text(design_name_from("Desk 1"), "Desk 1", "a plain name is left alone");
            check_text(design_name_from("  spaced  "), "spaced", "names are trimmed");
            check_text(design_name_from("desk.v2"), "desk_v2", "odd characters become underscores");

            /*---------------------------------------------*\
            | The property that matters: whatever is typed,  |
            | the resulting file stays inside the designs    |
            | directory. A name that cannot be saved is      |
            | rejected rather than sanitised into a surprise. |
            |                                               |
            | One '..' anywhere in a design name would be    |
            | enough to write outside the user's config      |
            | directory, so this is worth testing rather     |
            | than reading.                                  |
            \*---------------------------------------------*/
            int rejected = 0;
            for(const char* raw : { "../../etc/passwd", "/etc/shadow", "..", "...",
                                    "./..", "a/b/c", "  ", "---", "~/.ssh/authorized_keys" })
            {
                const std::string safe = design_name_from(raw);
                if(safe.empty())
                {
                    rejected++;
                    continue;
                }

                check(safe.find('/') == std::string::npos,
                      std::string("no path separator survives sanitising: ") + raw +
                      " -> " + safe);
                check(safe.find("..") == std::string::npos,
                      std::string("no parent directory survives sanitising: ") + raw +
                      " -> " + safe);
                check(design_path_for(safe).rfind(designs_dir() + "/", 0) == 0,
                      "a sanitised name stays inside the designs directory: " + safe);
            }

            check(rejected >= 4, "names made only of punctuation are rejected outright");

            check(list_designs().empty(), "a fresh install has no saved designs");

            Frame design(keys, Rgb(0, 0, 0));
            design[7]  = Rgb(0xAA, 0xBB, 0xCC);
            design[11] = Rgb(0x00, 0xFF, 0x00);

            std::string error;
            check(save_named_design("Desk", design, variant->id, error),
                  "a named design saves: " + error);
            check(save_named_design("Late Night", design, variant->id, error),
                  "a second named design saves: " + error);

            const std::vector<std::string> names = list_designs();
            check_eq(static_cast<long long>(names.size()), 2, "both designs are listed");
            check_text(names[0], "Desk", "designs are listed by name, sorted");

            check(set_default_design("Desk", error), "a design can be made the default: " + error);
            check_text(default_design_name(), "Desk", "the default is remembered");

            Frame loaded(keys, Rgb(0, 0, 0));
            std::string loaded_name;
            check(load_default_design(loaded, loaded_name, error),
                  "the default design loads on its own: " + error);
            check_text(loaded_name, "Desk", "and it reports which design it was");
            check(loaded == design, "the loaded default is what was saved");

            /*---------------------------------------------*\
            | Deleting the default must not leave a startup  |
            | pointing at a file that is gone.               |
            \*---------------------------------------------*/
            check(delete_named_design("Desk", error), "a design can be deleted: " + error);
            check(default_design_name().empty(),
                  "deleting the default clears the startup design");
            check_eq(static_cast<long long>(list_designs().size()), 1, "one design is left");
            check(!load_default_design(loaded, loaded_name, error),
                  "with nothing set, there is no startup design to load");

            check(!delete_named_design("Desk", error), "deleting it twice fails cleanly");

            /*---------------------------------------------*\
            | Turning the startup design off.                |
            \*---------------------------------------------*/
            check(set_default_design("Late Night", error), "another design becomes the default");
            check(set_default_design("", error), "the default can be cleared");
            check(default_design_name().empty(), "clearing it takes effect");

            std::filesystem::remove_all(sandbox, ignored);
        }
    }

    /*---------------------------------------------------------*\
    | Layout tables                                               |
    \*---------------------------------------------------------*/
    /*---------------------------------------------------------*\
    | The settings file.                                          |
    |                                                            |
    | Small, but it is what an autostart entry depends on and it  |
    | is read by both binaries, so it gets the same treatment as  |
    | the design store: a sandboxed config directory and checks   |
    | on the invariants rather than the happy path only.          |
    \*---------------------------------------------------------*/
    void test_settings()
    {
        section("settings");

        std::error_code ignored;
        const std::string sandbox = "/tmp/claymore-settings-test";
        std::filesystem::remove_all(sandbox, ignored);
        setenv("CLAYMORE_CONFIG_DIR", sandbox.c_str(), 1);

        check_text(settings_path(), sandbox + "/settings",
                   "settings live in the config directory");

        check(!start_in_tray(), "a fresh install does not start in the panel");
        check(list_settings().empty(), "and has no settings file to speak of");

        std::string error;
        check(set_start_in_tray(true, error), "start in tray can be turned on: " + error);
        check(start_in_tray(), "and it is remembered");
        check_eq(static_cast<long long>(list_settings().size()), 1, "one setting is listed");
        check_text(list_settings()[0].first, SETTING_START_IN_TRAY, "under the documented key");

        check(set_start_in_tray(false, error), "it can be turned off again");
        check(!start_in_tray(), "and off is remembered too");

        /*-----------------------------------------------------*\
        | The file is meant to be edited by hand, so anything    |
        | that reads as yes has to read as on.                   |
        \*-----------------------------------------------------*/
        for(const char* word : { "on", "yes", "true", "1" })
        {
            setting_set(SETTING_START_IN_TRAY, word, error);
            check(start_in_tray(), std::string("'") + word + "' reads as on");
        }

        for(const char* word : { "off", "no", "false", "0" })
        {
            setting_set(SETTING_START_IN_TRAY, word, error);
            check(!start_in_tray(), std::string("'") + word + "' reads as off");
        }

        setting_set(SETTING_START_IN_TRAY, "banana", error);
        check(!start_in_tray(), "nonsense falls back to the default instead of refusing to start");

        /*-----------------------------------------------------*\
        | A key this build does not know about has to survive a  |
        | write: that is the difference between a downgrade      |
        | keeping the user's settings and silently eating them.  |
        \*-----------------------------------------------------*/
        setting_set("future-thing", "kept", error);
        check_text(setting_get("future-thing"), "kept", "an unknown key is stored as typed");
        check(set_start_in_tray(true, error), "writing one key succeeds");
        check_text(setting_get("future-thing"), "kept", "and leaves the other keys alone");
        check_eq(static_cast<long long>(list_settings().size()), 2, "both keys are present");

        /*-----------------------------------------------------*\
        | Comments and blank lines are ignored, which is what     |
        | makes the file safe to leave notes in.                  |
        \*-----------------------------------------------------*/
        {
            std::ofstream out(settings_path());
            out << "# a note to myself\n\nstart-in-tray on\n";
        }

        check(start_in_tray(), "comments and blank lines are ignored");
        check_eq(static_cast<long long>(list_settings().size()), 1, "a comment is not a setting");
        check_text(setting_get("nothing-here", "fallback"), "fallback",
                   "a missing key returns the fallback");

        setting_set("", "x", error);
        check(!setting_set("", "x", error), "a setting with no name is refused");

        std::filesystem::remove_all(sandbox, ignored);
    }

    void test_layouts()
    {
        section("layout tables");

        const std::vector<std::string> variants = all_variant_ids();
        check_eq(static_cast<long long>(variants.size()), 6, "six Claymore variants exist");

        for(const std::string& id : variants)
        {
            const LayoutVariant* variant = find_variant(id);
            check(variant != nullptr, "variant " + id + " resolves");
            if(variant == nullptr)
            {
                continue;
            }

            check(variant->key_count > 0, id + " has keys");
            check(variant->rows > 0 && variant->cols > 0, id + " has a matrix");

            /*---------------------------------------------*\
            | Every matrix cell must reference a valid slot,  |
            | and no slot may appear twice: a duplicate would |
            | mean two physical keys share one LED.           |
            \*---------------------------------------------*/
            std::set<int> seen;
            bool          duplicates = false;
            bool          out_of_range = false;
            int           cells = 0;

            for(int i = 0; i < variant->rows * variant->cols; i++)
            {
                const int slot = variant->matrix[i];
                if(slot < 0)
                {
                    continue;
                }
                cells++;
                if(slot >= variant->key_count)
                {
                    out_of_range = true;
                }
                if(!seen.insert(slot).second)
                {
                    duplicates = true;
                }
            }

            check(!out_of_range, id + " has no out of range slots");
            check(!duplicates, id + " has no duplicate slots");
            check(cells > 0 && cells <= variant->key_count,
                  id + " matrix cell count is sane");

            /*---------------------------------------------*\
            | Device LED indices should be unique per key.    |
            \*---------------------------------------------*/
            std::set<int> led_indices;
            bool          led_duplicates = false;
            for(int k = 0; k < variant->key_count; k++)
            {
                if(!led_indices.insert(variant->keys[k].index).second)
                {
                    led_duplicates = true;
                }
            }
            check(!led_duplicates, id + " has unique device LED indices");

            /*---------------------------------------------*\
            | Slugs must be unique so the CLI is unambiguous. |
            \*---------------------------------------------*/
            std::set<std::string> slugs;
            bool                  slug_duplicates = false;
            for(int k = 0; k < variant->key_count; k++)
            {
                if(!slugs.insert(variant->keys[k].slug).second)
                {
                    slug_duplicates = true;
                }
            }
            check(!slug_duplicates, id + " has unique key slugs");
        }

        /*-----------------------------------------------------*\
        | Spot checks against the reverse engineered tables      |
        \*-----------------------------------------------------*/
        const LayoutVariant* ansi = find_variant("no-numpad-us");
        check(ansi != nullptr, "no-numpad-us exists");
        if(ansi != nullptr)
        {
            check_eq(ansi->key_count, 88, "no-numpad-us has 88 LEDs");
            check_eq(ansi->rows, 7, "no-numpad-us has 7 rows");
            check_eq(ansi->cols, 19, "no-numpad-us has 19 columns");

            /*---------------------------------------------*\
            | Escape sits at row 0 col 0 and is device LED    |
            | 0x00 in the ANSI table.                        |
            \*---------------------------------------------*/
            check_eq(ansi->matrix[0], 0, "escape is the top left cell");

            const KeyName* escape = find_key(*ansi, "escape");
            check(escape != nullptr, "escape lookup by name works");
            if(escape != nullptr)
            {
                check_eq(escape->index, 0x00, "escape is device LED 0x00");
            }

            const KeyName* shift = find_key(*ansi, "left-shift");
            check(shift != nullptr, "left-shift lookup by slug works");
            if(shift != nullptr)
            {
                check_eq(shift->index, 0x04, "left-shift is device LED 0x04");
            }

            /*---------------------------------------------*\
            | Space is a good GPS check: 0x25 in the ANSI    |
            | table, on the bottom row.                      |
            \*---------------------------------------------*/
            const KeyName* space = find_key(*ansi, "space");
            check(space != nullptr, "space lookup works");
            if(space != nullptr)
            {
                check_eq(space->index, 0x25, "space is device LED 0x25");
            }

            check(find_key(*ansi, "0x25") == space, "lookup by hex device index works");
        }

        const LayoutVariant* left = find_variant("numpad-left-uk");
        check(left != nullptr, "numpad-left-uk exists");
        if(left != nullptr)
        {
            check_eq(left->key_count, 106, "numpad-left-uk has 106 LEDs");
            check_eq(left->cols, 24, "numpad-left-uk has 24 columns");
        }

        check_eq(variant_id(Numpad::Right, Region::UK) == "numpad-right-uk", true,
                 "variant_id composes correctly");
        check_eq(variant_id(Numpad::None, Region::US) == "no-numpad-us", true,
                 "variant_id composes correctly for no numpad");
        check(find_variant("does-not-exist") == nullptr, "unknown variant returns null");

        /*-----------------------------------------------------*\
        | Matrix position lookup must invert the matrix.         |
        \*-----------------------------------------------------*/
        if(ansi != nullptr)
        {
            const auto position = find_key_pos(*ansi, 0);
            check(position.has_value(), "slot 0 has a position");
            if(position.has_value())
            {
                check_eq(position->row, 0, "slot 0 is on row 0");
                check_eq(position->col, 0, "slot 0 is on column 0");
            }
        }
    }

    /*---------------------------------------------------------*\
    | Packet layouts                                              |
    \*---------------------------------------------------------*/
    void test_packets()
    {
        section("protocol packets");

        check_eq(protocol::REPORT_LENGTH, 65, "reports are 65 bytes");
        check_eq(protocol::AURA_VENDOR_ID, 0x0B05, "Aura vendor id");
        check_eq(protocol::CLAYMORE_PRODUCT_ID, 0x184D, "Claymore product id");
        check_eq(protocol::AURA_INTERFACE, 1, "Aura control interface");
        check_eq(protocol::AURA_USAGE_PAGE, 0xFF00, "Aura usage page");

        /*-----------------------------------------------------*\
        | Remote control handshake                                |
        \*-----------------------------------------------------*/
        {
            const protocol::Report grant  = protocol::allow_remote_control(protocol::RemoteControl::Grant);
            const protocol::Report direct = protocol::allow_remote_control(protocol::RemoteControl::GrantDirect);
            const protocol::Report revoke = protocol::allow_remote_control(protocol::RemoteControl::Revoke);

            check_eq(grant[0], 0x00,  "remote control report number is 0");
            check_eq(grant[1], 0x41,  "remote control opcode is 0x41");
            check_eq(grant[2],  1,    "grant is type 1");
            check_eq(direct[2], 3,    "grant direct is type 3");
            check_eq(revoke[2], 0,    "revoke is type 0");

            for(std::size_t i = 3; i < protocol::REPORT_LENGTH; i++)
            {
                if(grant[i] != 0)
                {
                    check(false, "remote control packet is zero padded");
                    break;
                }
            }
        }

        /*-----------------------------------------------------*\
        | Save, mode select and the numpad query                  |
        \*-----------------------------------------------------*/
        {
            const protocol::Report save = protocol::save_mode();
            check_eq(save[1], 0x50, "save opcode is 0x50");
            check_eq(save[2], 0x55, "save subcommand is 0x55");

            const protocol::Report mode = protocol::update_mode(protocol::Mode::Quicksand);
            check_eq(mode[1], 0x51, "update mode opcode is 0x51");
            check_eq(mode[2], 0x28, "update mode subcommand is 0x28");
            check_eq(mode[5], 7,    "update mode carries the mode value");

            const protocol::Report numpad = protocol::get_numpad_location();
            check_eq(numpad[1], 0x40, "numpad query opcode is 0x40");
            check_eq(numpad[2], 0x60, "numpad query subcommand is 0x60");
        }

        /*-----------------------------------------------------*\
        | Version handshake                                       |
        \*-----------------------------------------------------*/
        {
            const protocol::Report request = protocol::get_version_request();
            check_eq(request[1], 0x10, "version request opcode is 0x10");
            check_eq(request[2], 0x01, "version request subcommand is 0x01");

            const protocol::Report read = protocol::get_version_read();
            check_eq(read[1], 0x12, "version read opcode is 0x12");
            check_eq(read[2], 0x22, "version read subcommand is 0x22");

            std::uint8_t reply[65] = {};
            reply[8]  = 0x01;
            reply[9]  = 0x0A;
            reply[10] = 0x2B;
            check_eq(protocol::parse_version_reply(reply, sizeof(reply)) == "01.0A.2B", true,
                     "version reply decodes to dotted hex");
            check_eq(protocol::parse_version_reply(nullptr, 0) == "", true,
                     "version reply tolerates a null buffer");

            std::uint8_t numpad_reply[65] = {};
            numpad_reply[4] = 1;
            numpad_reply[5] = 1;
            check_eq(protocol::parse_numpad_reply(numpad_reply, sizeof(numpad_reply)), 3,
                     "numpad reply decodes as byte5*2+byte4");
        }

        /*-----------------------------------------------------*\
        | Effect packet                                           |
        \*-----------------------------------------------------*/
        {
            const std::vector<Rgb> colors = { Rgb(0xFF, 0x00, 0x3C), Rgb(0x14, 0x00, 0x24) };
            const protocol::Report report = protocol::effect(
                protocol::Mode::Breathing,
                protocol::speed_from_percent(50),
                protocol::Direction::Left,
                protocol::ColorMode::Specific,
                false,
                colors);

            check_eq(report[1], 0x51, "effect opcode is 0x51");
            check_eq(report[2], 0x2C, "effect subcommand is 0x2C");
            check_eq(report[3], 0x00, "effect byte 3 is zero");
            check_eq(report[4], 0x00, "effect byte 4 is zero");
            check_eq(report[5], 1,    "effect mode is breathing");
            check_eq(report[6], 127,  "speed 50 percent maps to 127");
            check_eq(report[7], 0,    "specific colour, no pattern, direction left");
            check_eq(report[8], 0xFF, "byte 8 is 0xFF");
            check_eq(report[9], 0xFF, "byte 9 is 0xFF");
            check_eq(report[10], 0xFF, "brightness default is 0xFF");

            check_eq(report[11], 0xFF, "first colour red channel");
            check_eq(report[12], 0x00, "first colour green channel");
            check_eq(report[13], 0x3C, "first colour blue channel");
            check_eq(report[14], 0x14, "second colour red channel");
            check_eq(report[16], 0x24, "second colour blue channel");

            /*---------------------------------------------*\
            | The four undocumented 0xFF block stamps.        |
            \*---------------------------------------------*/
            check_eq(report[17], 0xFF, "block stamp at 17");
            check_eq(report[29], 0xFF, "block stamp at 29");
            check_eq(report[41], 0xFF, "block stamp at 41");
            check_eq(report[53], 0xFF, "block stamp at 53");

            /*---------------------------------------------*\
            | Colour cycle overrides the light byte with 0x80.|
            \*---------------------------------------------*/
            const protocol::Report cycle = protocol::effect(
                protocol::Mode::ColorCycle, 100, protocol::Direction::Left,
                protocol::ColorMode::Specific, false, {});
            check_eq(cycle[10], 0x80, "colour cycle uses the 0x80 light byte");

            /*---------------------------------------------*\
            | Random makes bit 7, pattern makes bit 5.        |
            \*---------------------------------------------*/
            const protocol::Report random = protocol::effect(
                protocol::Mode::Reactive, 10, protocol::Direction::Left,
                protocol::ColorMode::Random, false, {});
            check_eq(random[7], 128, "random sets bit 7");

            const protocol::Report patterned = protocol::effect(
                protocol::Mode::Wave, 10, protocol::Direction::Right,
                protocol::ColorMode::Pattern, false, { Rgb(1, 2, 3) });
            check_eq(patterned[7], 32 + 4, "pattern sets bit 5 and right is direction 4");

            /*---------------------------------------------*\
            | A coloured Wave must set bit 5 by itself.      |
            |                                                |
            | Regression: without this the firmware runs its |
            | own rainbow and ignores the palette, so a      |
            | cyan/red wave came out cyan/blue with no red.  |
            \*---------------------------------------------*/
            const protocol::Report colour_wave = protocol::effect(
                protocol::Mode::Wave, 60, protocol::Direction::Left,
                protocol::ColorMode::Specific, false,
                { Rgb(0x00, 0xC8, 0xFF), Rgb(0xFF, 0x00, 0x3C) });
            check_eq(colour_wave[7], 32, "a coloured wave sets the pattern bit on its own");
            check_eq(colour_wave[5], 4,  "a coloured wave is still mode 4");
            check_eq(colour_wave[11], 0x00, "and still carries the first colour");
            check_eq(colour_wave[14], 0xFF, "and still carries the second colour");

            /*---------------------------------------------*\
            | A colourless Wave is the vendor rainbow, so it |
            | must NOT have the pattern bit set.             |
            \*---------------------------------------------*/
            const protocol::Report rainbow_wave = protocol::effect(
                protocol::Mode::Wave, 60, protocol::Direction::Left,
                protocol::ColorMode::Specific, false, {});
            check_eq(rainbow_wave[7], 0, "a colourless wave leaves the pattern bit clear");

            /*---------------------------------------------*\
            | Random wins bit 7 and must not also set bit 5.  |
            \*---------------------------------------------*/
            const protocol::Report random_wave = protocol::effect(
                protocol::Mode::Wave, 60, protocol::Direction::Left,
                protocol::ColorMode::Random, false, { Rgb(1, 2, 3) });
            check_eq(random_wave[7], 128, "a random wave sets bit 7 only");

            /*---------------------------------------------*\
            | Other effects must not pick up the pattern bit  |
            | just because they were given colours.          |
            \*---------------------------------------------*/
            for(protocol::Mode mode : { protocol::Mode::Static, protocol::Mode::Breathing,
                                        protocol::Mode::Reactive, protocol::Mode::Ripple,
                                        protocol::Mode::StarryNight, protocol::Mode::Current,
                                        protocol::Mode::RainDrop })
            {
                const protocol::Report other = protocol::effect(
                    mode, 60, protocol::Direction::Left, protocol::ColorMode::Specific, false,
                    { Rgb(0xFF, 0x00, 0x3C), Rgb(0x00, 0xC8, 0xFF) });
                check_eq(other[7], 0, std::string("mode ") + protocol::mode_name(mode) +
                                      " does not set the pattern bit");
            }

            /*---------------------------------------------*\
            | An explicit brightness overrides the default.   |
            \*---------------------------------------------*/
            const protocol::Report dim = protocol::effect(
                protocol::Mode::Static, 0, protocol::Direction::Left,
                protocol::ColorMode::Specific, false, { Rgb(0xFF, 0xFF, 0xFF) },
                std::optional<std::uint8_t>(0x55));
            check_eq(dim[10], 0x55, "explicit light byte wins");

            /*---------------------------------------------*\
            | Rainbow wave with no colour sends white, which  |
            | is how the firmware represents a full gradient. |
            \*---------------------------------------------*/
            const protocol::Report rainbow = protocol::effect(
                protocol::Mode::Wave, 0, protocol::Direction::Left,
                protocol::ColorMode::Specific, false, {});
            check_eq(rainbow[11], 0xFF, "colourless wave defaults to white");
            check_eq(rainbow[13], 0xFF, "colourless wave defaults to white");
        }

        /*-----------------------------------------------------*\
        | Quicksand colours                                       |
        \*-----------------------------------------------------*/
        {
            std::vector<Rgb> six;
            for(int i = 0; i < 6; i++)
            {
                six.push_back(Rgb(static_cast<std::uint8_t>(10 * (i + 1)), 0, 0));
            }

            const protocol::Report report = protocol::quicksand_colors(six);
            check_eq(report[1], 0x51, "quicksand opcode is 0x51");
            check_eq(report[2], 0x91, "quicksand subcommand is 0x91");
            check_eq(report[5], 10, "quicksand first colour");
            check_eq(report[8], 20, "quicksand second colour");

            /*---------------------------------------------*\
            | The effect packet for quicksand carries no      |
            | colours, they ride in this separate packet.     |
            \*---------------------------------------------*/
            const protocol::Report effect = protocol::effect(
                protocol::Mode::Quicksand, 40, protocol::Direction::Down,
                protocol::ColorMode::Specific, false, six);
            check_eq(effect[11], 0x00, "quicksand effect packet has no inline colours");
            check_eq(effect[17], 0xFF, "quicksand effect packet is still stamped");
        }

        /*-----------------------------------------------------*\
        | Direct mode LED streaming                               |
        \*-----------------------------------------------------*/
        {
            std::vector<protocol::LedColor> leds;
            for(int i = 0; i < 88; i++)
            {
                leds.push_back(protocol::LedColor{
                    static_cast<std::uint8_t>(i),
                    Rgb(static_cast<std::uint8_t>(i), 0x10, 0x20) });
            }

            const std::vector<protocol::Report> packets = protocol::direct_led_packets(leds);

            /*---------------------------------------------*\
            | 88 LEDs at 15 per report is six reports, the    |
            | last one partially filled.                      |
            \*---------------------------------------------*/
            check_eq(static_cast<long long>(packets.size()), 6, "88 LEDs needs 6 reports");
            check_eq(packets[0][1], 0xC0, "direct opcode is 0xC0");
            check_eq(packets[0][2], 0x81, "direct subcommand is 0x81");
            check_eq(packets[0][3], 15,   "first report carries 15 LEDs");
            check_eq(packets[0][4], 0x00, "direct byte 4 is zero");
            check_eq(packets[5][3], 13,   "last report carries 13 LEDs");

            check_eq(packets[0][5], 0,    "first LED index");
            check_eq(packets[0][6], 0,    "first LED red");
            check_eq(packets[0][7], 0x10, "first LED green");
            check_eq(packets[0][8], 0x20, "first LED blue");

            /*---------------------------------------------*\
            | The 15th LED of the first report sits at        |
            | 5 + 14 * 4 = 61.                                |
            \*---------------------------------------------*/
            check_eq(packets[0][61], 14, "15th LED index lands at 61");
            check_eq(packets[0][64], 0x20, "15th LED blue lands at 64");

            check_eq(packets[5][5], 75, "last report starts at LED 75");
            check_eq(packets[5][53], 87, "last report ends at LED 87");

            /*---------------------------------------------*\
            | A short list must still produce exactly one     |
            | report, not zero.                               |
            \*---------------------------------------------*/
            const std::vector<protocol::Report> single =
                protocol::direct_led_packets({ protocol::LedColor{ 0x45, Rgb(1, 2, 3) } });
            check_eq(static_cast<long long>(single.size()), 1, "one LED still needs one report");
            check_eq(single[0][3], 1, "single LED report count is 1");
            check_eq(single[0][5], 0x45, "single LED index");

            const std::vector<protocol::Report> none = protocol::direct_led_packets({});
            check_eq(static_cast<long long>(none.size()), 0, "no LEDs means no reports");

            const protocol::Report one = protocol::direct_single_led(0x45, Rgb(0xAA, 0xBB, 0xCC));
            check_eq(one[3], 0x01, "single LED command count is 1");
            check_eq(one[5], 0x45, "single LED index");
            check_eq(one[6], 0xAA, "single LED red");
            check_eq(one[7], 0xBB, "single LED green");
            check_eq(one[8], 0xCC, "single LED blue");
        }

        /*-----------------------------------------------------*\
        | Speed mapping                                               |
        \*-----------------------------------------------------*/
        {
            check_eq(protocol::speed_from_percent(100), 0,   "fastest is 0");
            check_eq(protocol::speed_from_percent(0), 254,   "slowest is 254");
            check_eq(protocol::speed_from_percent(50), 127,  "half is 127");
            check_eq(protocol::speed_to_percent(0), 100,     "0 reads back as fastest");
            check_eq(protocol::speed_to_percent(254), 0,     "254 reads back as slowest");
            check_eq(protocol::speed_to_percent(127), 50,    "127 reads back as half");
            check_eq(protocol::speed_from_percent(-20), 254, "negative percent clamps");
            check_eq(protocol::speed_from_percent(500), 0,   "huge percent clamps");

            /*---------------------------------------------*\
            | Per effect defaults must land on the raw value |
            | the vendor software uses, within rounding.     |
            \*---------------------------------------------*/
            struct { protocol::Mode mode; int raw; } expectations[] =
            {
                { protocol::Mode::Breathing,   107 },
                { protocol::Mode::ColorCycle,  121 },
                { protocol::Mode::Reactive,     56 },
                { protocol::Mode::Wave,         50 },
                { protocol::Mode::Ripple,      108 },
                { protocol::Mode::StarryNight,  54 },
                { protocol::Mode::Quicksand,   103 },
            };

            for(const auto& expectation : expectations)
            {
                const int percent = default_speed_percent(expectation.mode);
                const int raw     = protocol::speed_from_percent(percent);
                const int delta   = raw - expectation.raw;
                check(delta >= -1 && delta <= 1,
                      std::string("default speed for ") + protocol::mode_name(expectation.mode) +
                      " lands within one step of the vendor value");
            }

            /*---------------------------------------------*\
            | Static has nothing to animate.                 |
            \*---------------------------------------------*/
            check_eq(default_speed_percent(protocol::Mode::Static), 0,
                     "static has no speed of its own");
        }

        /*-----------------------------------------------------*\
        | Mode names                                                      |
        \*-----------------------------------------------------*/
        {
            protocol::Mode mode = protocol::Mode::Static;
            check(protocol::parse_mode("starry-night", mode), "starry-night parses");
            check_eq(static_cast<long long>(mode), 6, "starry-night is mode 6");

            check(protocol::parse_mode("RAINBOW", mode), "aliases are case insensitive");
            check_eq(static_cast<long long>(mode), 2, "rainbow aliases to colour cycle");

            check(protocol::parse_mode("rainbow-wave", mode), "rainbow-wave alias resolves");
            check_eq(static_cast<long long>(mode), 4, "rainbow-wave is wave");

            check(!protocol::parse_mode("sparkle", mode), "unknown mode rejected");
            check_eq(static_cast<long long>(protocol::hardware_modes().size()), 10,
                     "ten firmware effects are exposed");
            check_eq(protocol::mode_name(protocol::Mode::Direct) == "direct", true,
                     "direct mode has a name");
        }
    }

    /*---------------------------------------------------------*\
    | The effect apply sequence                                   |
    |                                                            |
    | This exists because of a real bug: the 0x50/0x55 packet was |
    | treated as "write to flash" and skipped when the user asked |
    | not to persist. On the Claymore it is actually what COMMITS |
    | the effect, so skipping it made the keyboard flash the new  |
    | effect and then snap back to the old one.                   |
    \*---------------------------------------------------------*/
    void test_effect_sequence()
    {
        section("effect apply sequence");

        EffectSpec breathing;
        breathing.mode          = protocol::Mode::Breathing;
        breathing.colors        = { Rgb(0xFF, 0x00, 0x3C), Rgb(0x14, 0x00, 0x24) };
        breathing.speed_percent = 45;

        const std::vector<protocol::Report> reports = effect_reports(breathing);

        check_eq(static_cast<long long>(reports.size()), 5,
                 "a plain effect needs five reports");

        /*-----------------------------------------------------*\
        | Order matters: grant, upload, select, commit, release  |
        
        \*-----------------------------------------------------*/
        check_eq(reports[0][1], 0x41, "first report is remote control");
        check_eq(reports[0][2], 1,    "first report grants control");

        check_eq(reports[1][1], 0x51, "second report is the effect");
        check_eq(reports[1][2], 0x2C, "second report is the 0x2C subcommand");
        check_eq(reports[1][5], 1,    "second report carries the breathing mode");

        check_eq(reports[2][1], 0x51, "third report selects the slot");
        check_eq(reports[2][2], 0x28, "third report is the 0x28 subcommand");
        check_eq(reports[2][5], 1,    "third report selects breathing");

        check_eq(reports[3][1], 0x50, "fourth report is the commit");
        check_eq(reports[3][2], 0x55, "fourth report is the 0x55 commit");

        check_eq(reports[4][1], 0x41, "fifth report is remote control");
        check_eq(reports[4][2], 0,    "fifth report releases control");

        /*-----------------------------------------------------*\
        | The regression itself: the commit must be present even  |
        | when the caller explicitly asked not to persist.        |
        \*-----------------------------------------------------*/
        EffectSpec no_persist = breathing;
        no_persist.persist    = false;

        const std::vector<protocol::Report> stubborn = effect_reports(no_persist);
        check_eq(static_cast<long long>(stubborn.size()), 5,
                 "persist=false still produces the commit packet");
        check_eq(stubborn[3][1], 0x50, "the commit is present regardless of persist");
        check_eq(stubborn[3][2], 0x55, "the commit is present regardless of persist");

        /*-----------------------------------------------------*\
        | Release must always be last, or the board is left with  |
        | remote control granted and nothing driving it.         |
        \*-----------------------------------------------------*/
        check_eq(reports.back()[1], 0x41, "the sequence always ends with remote control");
        check_eq(reports.back()[2], 0,    "the sequence always ends by releasing control");

        /*-----------------------------------------------------*\
        | Quicksand needs its six colours in a separate packet.   |
        \*-----------------------------------------------------*/
        EffectSpec quicksand;
        quicksand.mode   = protocol::Mode::Quicksand;
        quicksand.colors = { Rgb(1, 2, 3), Rgb(4, 5, 6), Rgb(7, 8, 9),
                             Rgb(10, 11, 12), Rgb(13, 14, 15), Rgb(16, 17, 18) };

        const std::vector<protocol::Report> sand = effect_reports(quicksand);
        check_eq(static_cast<long long>(sand.size()), 6,
                 "quicksand adds one extra packet");
        check_eq(sand[2][1], 0x51, "quicksand colour packet follows the effect");
        check_eq(sand[2][2], 0x91, "quicksand colour packet is 0x91");
        check_eq(sand[2][5], 1,    "quicksand first colour red channel");
        check_eq(sand[3][1], 0x51, "slot selection still follows");
        check_eq(sand[3][2], 0x28, "slot selection is 0x28");
        check_eq(sand[4][1], 0x50, "commit still follows");

        /*-----------------------------------------------------*\
        | Commit detection is what makes the device wait longer   |
        | for the flash write.                                   |
        \*-----------------------------------------------------*/
        check(is_commit_report(reports[3]), "the commit packet is detected");
        check(!is_commit_report(reports[1]), "an effect packet is not a commit");
        check(!is_commit_report(reports[0]), "a grant is not a commit");

        /*-----------------------------------------------------*\
        | Every mode must produce a well formed sequence.         |
        \*-----------------------------------------------------*/
        bool all_valid = true;
        for(protocol::Mode mode : protocol::hardware_modes())
        {
            EffectSpec spec;
            spec.mode   = mode;
            spec.colors = { Rgb(0xFF, 0x00, 0x3C), Rgb(0x00, 0xC8, 0xFF), Rgb(0xFF, 0xFF, 0x00),
                            Rgb(0x00, 0xFF, 0x40), Rgb(0x80, 0x00, 0xFF), Rgb(0xFF, 0xFF, 0xFF) };

            const std::vector<protocol::Report> sequence = effect_reports(spec);

            if(sequence.size() < 5)                                  all_valid = false;
            if(sequence.front()[2] != 1)                             all_valid = false;
            if(sequence.back()[1] != 0x41 || sequence.back()[2] != 0) all_valid = false;
            if(sequence[sequence.size() - 2][1] != 0x50)             all_valid = false;
        }
        check(all_valid, "every firmware effect yields a valid apply sequence");
    }

    /*---------------------------------------------------------*\
    | Sync wire format                                            |
    \*---------------------------------------------------------*/
    void test_sync()
    {
        section("sync wire format");

        {
            SyncMessage message;
            message.sparse = false;
            for(int i = 0; i < 4; i++)
            {
                message.updates.push_back(SyncUpdate{ i, Rgb(static_cast<std::uint8_t>(i * 10), 1, 2) });
            }

            const std::vector<std::uint8_t> bytes = sync_encode(message);
            check_eq(static_cast<long long>(bytes.size()), 8 + 4 * 3, "dense datagram length");
            check_eq(bytes[0], 'R', "magic byte 0");
            check_eq(bytes[1], 'G', "magic byte 1");
            check_eq(bytes[2], 'B', "magic byte 2");
            check_eq(bytes[3], 'C', "magic byte 3");
            check_eq(bytes[4], 1,   "version is 1");
            check_eq(bytes[5], 0,   "dense flags are zero");
            check_eq(bytes[6], 4,   "count low byte");
            check_eq(bytes[7], 0,   "count high byte");

            SyncMessage decoded;
            std::string  error;
            check(sync_decode(bytes.data(), bytes.size(), decoded, error),
                  "dense datagram decodes");
            check_eq(static_cast<long long>(decoded.updates.size()), 4, "four updates decode");
            check_eq(decoded.updates[3].slot, 3, "dense slot is positional");
            check_eq(decoded.updates[3].color.r, 30, "dense colour survives the round trip");
            check(decoded.updates[3].color == Rgb(30, 1, 2), "dense colour is exact");
        }

        {
            SyncMessage message;
            message.sparse = true;
            message.updates.push_back(SyncUpdate{ 7, Rgb(0xAA, 0xBB, 0xCC) });
            message.updates.push_back(SyncUpdate{ 42, Rgb(1, 2, 3) });

            const std::vector<std::uint8_t> bytes = sync_encode(message);
            check_eq(static_cast<long long>(bytes.size()), 8 + 2 * 4, "sparse datagram length");
            check_eq(bytes[5], 1, "sparse flag is bit 0");

            SyncMessage decoded;
            std::string  error;
            check(sync_decode(bytes.data(), bytes.size(), decoded, error),
                  "sparse datagram decodes");
            check(decoded.sparse, "sparse flag survives");
            check_eq(decoded.updates[0].slot, 7, "sparse slot survives");
            check_eq(decoded.updates[1].slot, 42, "sparse slot survives");
            check(decoded.updates[0].color == Rgb(0xAA, 0xBB, 0xCC), "sparse colour survives");

            message.updates.push_back(SyncUpdate{ 256, Rgb(1, 2, 3) });
            check(sync_encode(message).empty(),
                  "sparse slots above the uint8 wire limit are rejected");
        }

        {
            SyncMessage message;
            message.release = true;
            const std::vector<std::uint8_t> bytes = sync_encode(message);

            SyncMessage decoded;
            std::string  error;
            check(sync_decode(bytes.data(), bytes.size(), decoded, error), "release decodes");
            check(decoded.release, "release flag survives");
            check(decoded.updates.empty(), "release has no updates");
        }

        /*-----------------------------------------------------*\
        | Malformed input must be rejected, not crash.            |
        \*-----------------------------------------------------*/
        {
            SyncMessage decoded;
            std::string  error;

            check(!sync_decode(nullptr, 0, decoded, error), "null datagram rejected");

            std::uint8_t short_buffer[4] = { 'R', 'G', 'B', 'C' };
            check(!sync_decode(short_buffer, sizeof(short_buffer), decoded, error),
                  "short datagram rejected");

            std::uint8_t bad_magic[8] = { 'X', 'G', 'B', 'C', 1, 0, 0, 0 };
            check(!sync_decode(bad_magic, sizeof(bad_magic), decoded, error),
                  "bad magic rejected");

            std::uint8_t bad_version[8] = { 'R', 'G', 'B', 'C', 9, 0, 0, 0 };
            check(!sync_decode(bad_version, sizeof(bad_version), decoded, error),
                  "bad version rejected");

            /*---------------------------------------------*\
            | Claims 100 dense entries but only carries one.  |
            \*---------------------------------------------*/
            std::uint8_t truncated[11] = { 'R', 'G', 'B', 'C', 1, 0, 100, 0, 1, 2, 3 };
            check(!sync_decode(truncated, sizeof(truncated), decoded, error),
                  "truncated payload rejected");

            /*---------------------------------------------*\
            | Absurd entry count rejected before allocating.  |
            \*---------------------------------------------*/
            std::uint8_t absurd[8] = { 'R', 'G', 'B', 'C', 1, 0, 0xFF, 0xFF };
            check(!sync_decode(absurd, sizeof(absurd), decoded, error),
                  "absurd entry count rejected");
        }

        check(!default_socket_path().empty(), "default socket path is not empty");

        {
            const std::string path = "/tmp/claymore-sync-lifecycle-test.sock";
            std::error_code ignored;
            std::filesystem::remove(path, ignored);

            std::string error;
            auto first = SyncServer::create(path, 8, error);
            check(first != nullptr, "sync server can bind a socket");
            if(first)
            {
                auto second = SyncServer::create(path, 8, error);
                check(second == nullptr, "a live sync socket is not replaced");
                check(error.find("already in use") != std::string::npos,
                      "the live-socket error is explicit: " + error);
                first.reset();
                check(!std::filesystem::exists(path),
                      "sync server removes its socket on destruction");
            }
        }
    }

    /*---------------------------------------------------------*\
    | Software previews                                           |
    \*---------------------------------------------------------*/
    void test_previews()
    {
        section("software previews");

        const LayoutVariant* layout = find_variant("no-numpad-us");
        check(layout != nullptr, "preview layout exists");
        if(layout == nullptr)
        {
            return;
        }

        /*-----------------------------------------------------*\
        | Static fills every slot and stays put over time.        |
        \*-----------------------------------------------------*/
        {
            EffectSpec spec;
            spec.mode   = protocol::Mode::Static;
            spec.colors = { Rgb(0xFF, 0x00, 0x3C) };

            const Frame a = preview::render(spec, *layout, 0.0);
            const Frame b = preview::render(spec, *layout, 12.5);

            check_eq(static_cast<long long>(a.size()), 88, "static frame covers every slot");
            check(a == b, "static is time invariant");

            bool all_correct = true;
            for(const Rgb& color : a)
            {
                if(color != Rgb(0xFF, 0x00, 0x3C))
                {
                    all_correct = false;
                }
            }
            check(all_correct, "static paints the requested colour everywhere");
        }

        /*-----------------------------------------------------*\
        | Brightness dims the preview.                            |
        \*-----------------------------------------------------*/
        {
            EffectSpec spec;
            spec.mode       = protocol::Mode::Static;
            spec.colors     = { Rgb(0xFF, 0xFF, 0xFF) };
            spec.brightness = 0;

            const Frame dim = preview::render(spec, *layout, 0.0);
            check(dim[0].r < 0xFF, "brightness level 0 dims the preview");
            check(dim[0].r > 0,    "brightness level 0 does not go fully dark");

            spec.brightness = 3;
            const Frame full = preview::render(spec, *layout, 0.0);
            check_eq(full[0].r, 0xFF, "brightness level 3 is full");
        }

        /*-----------------------------------------------------*\
        | Every firmware effect must render a full frame without  |
        | throwing, at a range of times.                          |
        \*-----------------------------------------------------*/
        {
            bool all_sized = true;
            bool all_deterministic = true;

            for(protocol::Mode mode : protocol::hardware_modes())
            {
                EffectSpec spec;
                spec.mode          = mode;
                spec.speed_percent = 60;
                spec.colors        =
                {
                    Rgb(0xFF, 0x00, 0x3C), Rgb(0x00, 0xC8, 0xFF), Rgb(0xFF, 0x88, 0x00),
                    Rgb(0x00, 0xFF, 0x40), Rgb(0x80, 0x00, 0xFF), Rgb(0xFF, 0xFF, 0xFF),
                };

                for(double t : { 0.0, 0.37, 3.5, 61.2 })
                {
                    const Frame frame = preview::render(spec, *layout, t);
                    if(frame.size() != 88)
                    {
                        all_sized = false;
                    }
                    if(preview::render(spec, *layout, t) != frame)
                    {
                        all_deterministic = false;
                    }
                }
            }

            check(all_sized, "every firmware effect renders a full 88 slot frame");
            check(all_deterministic, "previews are deterministic for the same time");
        }

        /*-----------------------------------------------------*\
        | Animated effects must actually move.                    |
        \*-----------------------------------------------------*/
        {
            bool moved = false;

            for(protocol::Mode mode : protocol::hardware_modes())
            {
                if(mode == protocol::Mode::Static)
                {
                    continue;
                }

                EffectSpec spec;
                spec.mode          = mode;
                spec.speed_percent = 100;
                spec.colors        = { Rgb(0xFF, 0x00, 0x3C), Rgb(0x00, 0x00, 0x00),
                                       Rgb(0x00, 0xC8, 0xFF), Rgb(0x00, 0x00, 0x00),
                                       Rgb(0xFF, 0xFF, 0x00), Rgb(0x00, 0x00, 0x00) };

                if(preview::render(spec, *layout, 0.0) != preview::render(spec, *layout, 0.9))
                {
                    moved = true;
                }
            }

            check(moved, "at least one effect animates over time");
        }

        /*-----------------------------------------------------*\
        | Custom designs pass through, subject to brightness.     |
        \*-----------------------------------------------------*/
        {
            Frame design(88, Rgb(0x40, 0x80, 0xC0));

            EffectSpec spec;
            spec.mode = protocol::Mode::Direct;

            const Frame rendered = preview::render_custom(design, spec);
            check_eq(static_cast<long long>(rendered.size()), 88, "custom frame size is preserved");
            check(rendered[0] == Rgb(0x40, 0x80, 0xC0), "custom colours pass through unchanged");

            spec.brightness = 1;
            const Frame dimmed = preview::render_custom(design, spec);
            check(dimmed[0].r < 0x40, "custom frame honours brightness");
        }

        /*-----------------------------------------------------*\
        | EffectSpec colour counts                                |
        \*-----------------------------------------------------*/
        {
            EffectSpec quicksand;
            quicksand.mode   = protocol::Mode::Quicksand;
            quicksand.colors = { Rgb(1, 1, 1), Rgb(2, 2, 2) };

            const std::vector<Rgb> normalized = quicksand.normalized_colors();
            check_eq(static_cast<long long>(normalized.size()), 6,
                     "quicksand pads up to six colours");

            EffectSpec single;
            single.mode   = protocol::Mode::Static;
            single.colors = { Rgb(9, 9, 9), Rgb(1, 1, 1), Rgb(2, 2, 2) };
            check_eq(static_cast<long long>(single.normalized_colors().size()), 1,
                     "static trims down to one colour");

            EffectSpec cycle;
            cycle.mode = protocol::Mode::ColorCycle;
            check(cycle.normalized_colors().empty(), "colour cycle takes no colours");

            check_eq(single.colors_min(), 1, "static needs at least one colour");
            check(single.supports_random() == false, "static does not support random");
            check(cycle.uses_colors() == false, "colour cycle reports no colours");
            check(quicksand.uses_direction(), "quicksand uses a direction");
            check(single.uses_direction() == false, "static has no direction");

            /*---------------------------------------------*\
            | Light byte mapping.                              |
            \*---------------------------------------------*/
            EffectSpec levels;
            levels.mode       = protocol::Mode::Static;
            levels.brightness = 0;
            check_eq(levels.lightness_byte(), 0x00, "level 0 light byte");
            levels.brightness = 1;
            check_eq(levels.lightness_byte(), 0x55, "level 1 light byte");
            levels.brightness = 2;
            check_eq(levels.lightness_byte(), 0xAA, "level 2 light byte");
            levels.brightness = 3;
            check_eq(levels.lightness_byte(), 0xFF, "level 3 light byte");
            levels.brightness.reset();
            check_eq(levels.lightness_byte(), 0xFF, "unset brightness defaults to 0xFF");
        }

        /*-----------------------------------------------------*\
        | Presets must be usable as-is.                           |
        \*-----------------------------------------------------*/
        {
            const std::vector<EffectPreset> presets = builtin_presets();
            check(presets.size() >= 8, "there are at least eight built in presets");

            bool ids_unique = true;
            bool all_valid  = true;
            std::set<std::string> ids;

            for(const EffectPreset& preset : presets)
            {
                if(!ids.insert(preset.id).second) ids_unique = false;
                if(preset.name.empty() || preset.description.empty()) all_valid = false;

                /*---------------------------------------------*\
                | Applying a preset must produce a full frame.    |
                \*---------------------------------------------*/
                if(preview::render(preset.spec, *layout, 0.0).size() != 88)
                {
                    all_valid = false;
                }

                if(preset.spec.uses_colors() &&
                   preset.spec.colors.size() < static_cast<std::size_t>(preset.spec.colors_min()))
                {
                    all_valid = false;
                }
            }

            check(ids_unique, "preset ids are unique");
            check(all_valid, "presets are complete and renderable");
        }
    }
}

int main()
{
    std::printf("Claymore@Linux test suite\n");
    std::printf("=============================\n");

    test_colors();
    test_canvas_hit_testing();
    test_design_files();
    test_settings();
    test_layouts();
    test_packets();
    test_effect_sequence();
    test_sync();
    test_previews();

    std::printf("\n=============================\n");
    std::printf("%d checks, %d failures\n", checks_run, checks_failed);

    return checks_failed == 0 ? 0 : 1;
}
