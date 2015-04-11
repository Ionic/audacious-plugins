/*
 * playlist_util.c
 * Copyright 2010-2011 Michał Lipski and John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <gtk/gtk.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/playlist.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/list.h>

#include "playlist_util.h"
#include "ui_playlist_notebook.h"

GtkWidget * playlist_get_treeview (int playlist)
{
    GtkWidget * page = gtk_notebook_get_nth_page (UI_PLAYLIST_NOTEBOOK, playlist);

    if (!page)
        return nullptr;

    return (GtkWidget *) g_object_get_data ((GObject *) page, "treeview");
}

int playlist_count_selected_in_range (int list, int top, int length)
{
    int selected = 0;

    for (int count = 0; count < length; count ++)
    {
        if (aud_playlist_entry_get_selected (list, top + count))
            selected ++;
    }

    return selected;
}

void playlist_song_info ()
{
    int list = aud_playlist_get_active ();
    int focus = aud_playlist_get_focus (list);

    if (focus < 0)
        return;

    audgui_infowin_show (list, focus);
}

void playlist_open_folder ()
{
    int list = aud_playlist_get_active ();
    int focus = aud_playlist_get_focus (list);

    if (focus < 0)
        return;

    StringBuf filename = uri_to_filename (aud_playlist_entry_get_filename (list, focus));
    if (! filename)
        return;

    char * dirname = g_path_get_dirname (filename);
    StringBuf uri = filename_to_uri (dirname);
    g_free (dirname);

    GdkScreen * screen = gdk_screen_get_default ();
    gtk_show_uri (screen, uri, GDK_CURRENT_TIME, nullptr);
}

void playlist_queue_toggle ()
{
    int list = aud_playlist_get_active ();
    int focus = aud_playlist_get_focus (list);

    if (focus < 0)
        return;

    /* make sure focused row is selected */
    if (! aud_playlist_entry_get_selected (list, focus))
    {
        aud_playlist_select_all (list, false);
        aud_playlist_entry_set_selected (list, focus, true);
    }

    int at = aud_playlist_queue_find_entry (list, focus);

    if (at < 0)
        aud_playlist_queue_insert_selected (list, -1);
    else
        aud_playlist_queue_delete_selected (list);
}

void playlist_delete_selected ()
{
    int list = aud_playlist_get_active ();
    aud_playlist_delete_selected (list);
    aud_playlist_entry_set_selected (list, aud_playlist_get_focus (list), true);
}

void playlist_copy ()
{
    Index<char> text = audgui_urilist_create_from_selected (aud_playlist_get_active ());
    if (! text.len ())
        return;

    gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), text.begin (), text.len ());
}

void playlist_cut ()
{
    playlist_copy ();
    playlist_delete_selected ();
}

void playlist_paste ()
{
    char * text = gtk_clipboard_wait_for_text (gtk_clipboard_get
     (GDK_SELECTION_CLIPBOARD));
    if (! text)
        return;

    int list = aud_playlist_get_active ();
    audgui_urilist_insert (list, aud_playlist_get_focus (list), text);
    g_free (text);
}

void playlist_shift (int offset)
{
    int list = aud_playlist_get_active ();
    int focus = aud_playlist_get_focus (list);

    if (focus < 0 || ! aud_playlist_entry_get_selected (list, focus))
        return;

    aud_playlist_shift (list, focus, offset);
}
