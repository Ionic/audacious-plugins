/*
 * Audacious: Cross platform multimedia player
 * Copyright (c) 2009-2014 Audacious Team
 *
 * Driver for Game_Music_Emu library. See details at:
 * http://www.slack.net/~ant/libs/
 */

#ifndef CONSOLE_PLUGIN_H
#define CONSOLE_PLUGIN_H

#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>

class ConsolePlugin : public InputPlugin
{
public:
    static const char about[];
    static const char * const exts[];
    static const char * const defaults[];
    static const PreferencesWidget widgets[];
    static const PluginPreferences prefs;

    static constexpr PluginInfo info = {
        N_("Game Console Music Decoder"),
        PACKAGE,
        about,
        & prefs
    };

    static constexpr auto iinfo = InputInfo (FlagSubtunes)
        .with_exts (exts);

    constexpr ConsolePlugin () : InputPlugin (info, iinfo) {}

    bool init ();
    void cleanup ();

    bool is_our_file (const char * filename, VFSFile & file)
        { return false; }

    Tuple read_tuple (const char * filename, VFSFile & file);
    bool play (const char * filename, VFSFile & file);
};

#endif // CONSOLE_PLUGIN_H
