/*---------------------------------------------------------*\
| design.hpp - the on disk format for a painted design      |
|                                                           |
|   Part of rog-claymore-linux                              |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>

#include "claymore/effects.hpp"
#include "claymore/layout.hpp"

/*---------------------------------------------------------*\
| config_dir() lives with the other user preferences, in      |
| claymore/settings.hpp: a design is one of the things stored |
| under the config directory, not the definition of it.       |
\*---------------------------------------------------------*/
#include "claymore/settings.hpp"

namespace claymore
{
    /*---------------------------------------------------------*\
    | A design is the frame the user painted, stored as text:    |
    |                                                            |
    |     # slot colour                                          |
    |     0 ff003c                                               |
    |     1 000000                                               |
    |                                                            |
    | '#' starts a comment, unknown slots are ignored and a line |
    | that does not parse stops the load with a message naming   |
    | the line.                                                  |
    |                                                            |
    | The format is deliberately dull so that it is diffable,    |
    | hand editable and scriptable - which is what makes it      |
    | possible to re-apply a design from a systemd unit at login |
    | and get the same lighting every time.                      |
    |                                                            |
    | There used to be one parser in the CLI and another in the  |
    | GUI, which is one parser too many for a file that both are |
    | expected to read.                                          |
    \*---------------------------------------------------------*/
    std::string format_design(const Frame& design, const std::string& layout_id);

    /*---------------------------------------------------------*\
    | Parses `text` into `frame`, which must already be sized to  |
    | the layout. Returns false with a human readable reason when |
    | nothing could be used, so a typo reports itself.            |
    \*---------------------------------------------------------*/
    bool parse_design(const std::string& text, Frame& frame, int& entries, std::string& error);

    /*---------------------------------------------------------*\
    | Same, straight from a file.                                 |
    \*---------------------------------------------------------*/
    bool load_design_file(const std::string& path, Frame& frame, std::string& error);

    /*---------------------------------------------------------*\
    | Writes a design out, creating the parent directory if it    |
    | does not exist. Designs live in the user's config directory |
    | by default, and that directory is not there on a fresh      |
    | machine.                                                    |
    \*---------------------------------------------------------*/
    bool save_design_file(const std::string& path, const Frame& design,
                          const std::string& layout_id, std::string& error);

    /*=================================================================*\
    | Named designs                                                      |
    |                                                                    |
    |     <config>/designs/<name>.txt    one design per file             |
    |     <config>/default-design        the name loaded at startup      |
    |                                                                    |
    | The default is stored as a name rather than a path, so moving the   |
    | designs directory cannot leave it pointing at nothing.             |
    \*=================================================================*/

    std::string designs_dir();
    std::string design_path_for(const std::string& name);
    std::string default_design_name();

    /*---------------------------------------------------------*\
    | Turns whatever the user typed into something safe to use as |
    | a file name, so a stray '/' cannot write outside the designs|
    | directory. Returns an empty string when nothing usable is   |
    | left.                                                       |
    \*---------------------------------------------------------*/
    std::string design_name_from(const std::string& raw);

    std::vector<std::string> list_designs();

    bool save_named_design(const std::string& name, const Frame& design,
                           const std::string& layout_id, std::string& error);
    bool delete_named_design(const std::string& name, std::string& error);
    bool set_default_design(const std::string& name, std::string& error);

    /*---------------------------------------------------------*\
    | The frame the user asked to have loaded at startup, if any.  |
    | Returns false when nothing is configured, which is not an    |
    | error worth reporting.                                       |
    \*---------------------------------------------------------*/
    bool load_default_design(Frame& frame, std::string& name, std::string& error);
}
