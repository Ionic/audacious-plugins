/*
 * Audacious
 * Copyright (c) 2006-2007 Audacious development team.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 * The Audacious team does not consider modular code linking to
 * Audacious or using our public API to be a derived work.
 */

#include <glib.h>
#include <math.h>

#include <audacious/plugin.h>
#include <audacious/input.h>

#include "ui_equalizer.h"
#include "ui_main.h"
#include "ui_playlist.h"
#include "ui_skinned_playstatus.h"
#include "ui_skinned_textbox.h"
#include "ui_skinned_window.h"
#include "skins_cfg.h"

static gint song_info_timeout_source = 0;

static void
ui_main_evlistener_title_change(gpointer hook_data, gpointer user_data)
{
    gchar * title = aud_playback_get_title ();

    mainwin_set_song_title (title);
    g_free (title);
}

static void
ui_main_evlistener_hide_seekbar(gpointer hook_data, gpointer user_data)
{
    mainwin_disable_seekbar();
}

static void
ui_main_evlistener_volume_change(gpointer hook_data, gpointer user_data)
{
    gint *h_vol = (gint *) hook_data;
    gint vl, vr, b, v;

    vl = CLAMP(h_vol[0], 0, 100);
    vr = CLAMP(h_vol[1], 0, 100);
    v = MAX(vl, vr);
    if (vl > vr)
        b = (gint) rint(((gdouble) vr / vl) * 100) - 100;
    else if (vl < vr)
        b = 100 - (gint) rint(((gdouble) vl / vr) * 100);
    else
        b = 0;

    mainwin_set_volume_slider(v);
    equalizerwin_set_volume_slider(v);
    mainwin_set_balance_slider(b);
    equalizerwin_set_balance_slider(b);
}

void ui_main_evlistener_playback_begin (void * hook_data, void * user_data)
{
    ui_vis_clear_data(mainwin_vis);
    ui_svis_clear_data(mainwin_svis);
    mainwin_disable_seekbar();
    mainwin_update_song_info();

    gtk_widget_show (mainwin_stime_min);
    gtk_widget_show (mainwin_stime_sec);
    gtk_widget_show (mainwin_sposition);
    gtk_widget_show (mainwin_minus_num);
    gtk_widget_show (mainwin_10min_num);
    gtk_widget_show (mainwin_min_num);
    gtk_widget_show (mainwin_10sec_num);
    gtk_widget_show (mainwin_sec_num);
    gtk_widget_show (mainwin_position);

    song_info_timeout_source =
        g_timeout_add (250, (GSourceFunc) mainwin_update_song_info, NULL);

    ui_skinned_playstatus_set_status(mainwin_playstatus, STATUS_PLAY);
}

static void
ui_main_evlistener_playback_stop(gpointer hook_data, gpointer user_data)
{
    if (song_info_timeout_source)
        g_source_remove(song_info_timeout_source);

    mainwin_clear_song_info ();
    mainwin_set_stopaftersong (FALSE);
}

void ui_main_evlistener_playback_pause (void * hook_data, void * user_data)
{
    ui_skinned_playstatus_set_status(mainwin_playstatus, STATUS_PAUSE);
}

static void
ui_main_evlistener_playback_unpause(gpointer hook_data, gpointer user_data)
{
    ui_skinned_playstatus_set_status(mainwin_playstatus, STATUS_PLAY);
}

static void
ui_main_evlistener_playback_play_file(gpointer hook_data, gpointer user_data)
{
    if (config.random_skin_on_play)
        skin_set_random_skin();
}

static void seek_cb (void * unused, void * another)
{
    ui_vis_clear_data (mainwin_vis);
    ui_svis_clear_data (mainwin_svis);
}

static void info_change (void * hook_data, void * user_data)
{
    gint bitrate, samplerate, channels;

    audacious_drct_get_info (& bitrate, & samplerate, & channels);
    mainwin_set_song_info (bitrate, samplerate, channels);
}

static void
ui_main_evlistener_mainwin_set_always_on_top(gpointer hook_data, gpointer user_data)
{
    gboolean *ontop = (gboolean*)hook_data;
    mainwin_set_always_on_top(*ontop);
}

static void
ui_main_evlistener_mainwin_show(gpointer hook_data, gpointer user_data)
{
    gboolean *show = (gboolean*)hook_data;
    mainwin_show(*show);
}

static void
ui_main_evlistener_equalizerwin_show(gpointer hook_data, gpointer user_data)
{
    gboolean *show = (gboolean*)hook_data;
    equalizerwin_show(*show);
}

static void
ui_main_evlistener_visualization_timeout(gpointer hook_data, gpointer user_data)
{
    VisNode *vis = (VisNode*) hook_data;

    guint8 intern_vis_data[512];
    gint16 mono_freq[2][256];
    gboolean mono_freq_calced = FALSE;
    gint16 mono_pcm[2][512], stereo_pcm[2][512];
    gboolean mono_pcm_calced = FALSE, stereo_pcm_calced = FALSE;
    gint i;

    if (! vis || config.vis_type == VIS_OFF)
        return;

    if (config.vis_type == VIS_ANALYZER) {
            /* Spectrum analyzer */
            /* 76 values */
            const gint long_xscale[] =
                { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                17, 18,
                19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
                34,
                35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                50, 51,
                52, 53, 54, 55, 56, 57, 58, 61, 66, 71, 76, 81, 87, 93,
                100, 107,
                114, 122, 131, 140, 150, 161, 172, 184, 255
            };
            /* 20 values */
            const int short_xscale[] =
                { 0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 15, 20, 27,
                36, 47, 62, 82, 107, 141, 184, 255
            };
            const double y_scale = 3.60673760222;   /* 20.0 / log(256) */
            const int *xscale;
            gint j, y, max;

            if (!mono_freq_calced)
                aud_calc_mono_freq(mono_freq, vis->data, vis->nch);

            memset(intern_vis_data, 0, 75);

            if (config.analyzer_type == ANALYZER_BARS) {
                if (config.player_shaded) {
                    max = 13;
                }
                else {
                    max = 19;
                }
                xscale = short_xscale;
            }
            else {
                if (config.player_shaded) {
                    max = 37;
                }
                else {
                    max = 75;
                }
                xscale = long_xscale;
            }

            for (i = 0; i < max; i++) {
                for (j = xscale[i], y = 0; j < xscale[i + 1]; j++) {
                    if (mono_freq[0][j] > y)
                        y = mono_freq[0][j];
                }
                y >>= 7;
                if (y != 0) {
                    intern_vis_data[i] = log(y) * y_scale;
                    if (intern_vis_data[i] > 15)
                        intern_vis_data[i] = 15;
                }
                else
                    intern_vis_data[i] = 0;
            }
    }
    else if(config.vis_type == VIS_VOICEPRINT){
        if (config.player_shaded) {
            /* VU */
            gint vu, val;

            if (!stereo_pcm_calced)
                aud_calc_stereo_pcm(stereo_pcm, vis->data, vis->nch);
            vu = 0;
            for (i = 0; i < 512; i++) {
                val = abs(stereo_pcm[0][i]);
                if (val > vu)
                    vu = val;
            }
            intern_vis_data[0] = (vu * 37) >> 15;
            if (intern_vis_data[0] > 37)
                intern_vis_data[0] = 37;
            if (vis->nch == 2) {
                vu = 0;
                for (i = 0; i < 512; i++) {
                    val = abs(stereo_pcm[1][i]);
                    if (val > vu)
                        vu = val;
                }
                intern_vis_data[1] = (vu * 37) >> 15;
                if (intern_vis_data[1] > 37)
                    intern_vis_data[1] = 37;
            }
            else
                intern_vis_data[1] = intern_vis_data[0];
        }
        else { /*Voiceprint*/
            if (!mono_freq_calced)
                aud_calc_mono_freq(mono_freq, vis->data, vis->nch);
            memset(intern_vis_data, 0, 256);

            /* For the values [0-16] we use the frequency that's 3/2 as much.
               If we assume the 512 values calculated by calc_mono_freq to
               cover 0-22kHz linearly we get a range of
               [0-16] * 3/2 * 22000/512 = [0-1,031] Hz.
               Most stuff above that is harmonics and we want to utilize the
               16 samples we have to the max[tm]
               */
            for (i = 0; i < 50 ; i+=3){
                intern_vis_data[i/3] += (mono_freq[0][i/2] >> 5);

                /*Boost frequencies above 257Hz a little*/
                //if(i > 4 * 3)
                //  intern_vis_data[i/3] += 8;
            }
        }
    }
    else { /* (config.vis_type == VIS_SCOPE) */

        /* Oscilloscope */
        gint pos, step;

        if (!mono_pcm_calced)
            aud_calc_mono_pcm(mono_pcm, vis->data, vis->nch);

        step = (vis->length << 8) / 74;
        for (i = 0, pos = 0; i < 75; i++, pos += step) {
            intern_vis_data[i] = ((mono_pcm[0][pos >> 8]) >> 12) + 7;
            if (intern_vis_data[i] == 255)
                intern_vis_data[i] = 0;
            else if (intern_vis_data[i] > 12)
                intern_vis_data[i] = 12;
            /* Do not see the point of that? (comparison always false) -larne.
               if (intern_vis_data[i] < 0)
               intern_vis_data[i] = 0; */
        }
    }

    if (config.player_shaded)
        ui_svis_timeout_func(mainwin_svis, intern_vis_data);
    else
        ui_vis_timeout_func(mainwin_vis, intern_vis_data);
}

void
ui_main_evlistener_init(void)
{
    aud_hook_associate("title change", ui_main_evlistener_title_change, NULL);
    aud_hook_associate("hide seekbar", ui_main_evlistener_hide_seekbar, NULL);
    aud_hook_associate("volume set", ui_main_evlistener_volume_change, NULL);
    aud_hook_associate("playback begin", ui_main_evlistener_playback_begin, NULL);
    aud_hook_associate("playback stop", ui_main_evlistener_playback_stop, NULL);
    aud_hook_associate("playback pause", ui_main_evlistener_playback_pause, NULL);
    aud_hook_associate("playback unpause", ui_main_evlistener_playback_unpause, NULL);
    aud_hook_associate("playback play file", ui_main_evlistener_playback_play_file, NULL);
    aud_hook_associate ("playback seek", seek_cb, 0);
    aud_hook_associate ("info change", info_change, NULL);
    aud_hook_associate("mainwin set always on top", ui_main_evlistener_mainwin_set_always_on_top, NULL);
    aud_hook_associate("mainwin show", ui_main_evlistener_mainwin_show, NULL);
    aud_hook_associate("equalizerwin show", ui_main_evlistener_equalizerwin_show, NULL);

    aud_hook_associate("playback audio error", (void *) mainwin_stop_pushed, NULL);
    aud_hook_associate("playback audio error", (void *) run_no_output_device_dialog, NULL);

    aud_hook_associate("playback seek", (HookFunction) mainwin_update_song_info, NULL);
}

void
ui_main_evlistener_dissociate(void)
{
    aud_hook_dissociate("title change", ui_main_evlistener_title_change);
    aud_hook_dissociate("hide seekbar", ui_main_evlistener_hide_seekbar);
    aud_hook_dissociate("volume set", ui_main_evlistener_volume_change);
    aud_hook_dissociate("playback begin", ui_main_evlistener_playback_begin);
    aud_hook_dissociate("playback stop", ui_main_evlistener_playback_stop);
    aud_hook_dissociate("playback pause", ui_main_evlistener_playback_pause);
    aud_hook_dissociate("playback unpause", ui_main_evlistener_playback_unpause);
    aud_hook_dissociate("playback play file", ui_main_evlistener_playback_play_file);
    aud_hook_dissociate ("playback seek", seek_cb);
    aud_hook_dissociate ("info change", info_change);
    aud_hook_dissociate("mainwin set always on top", ui_main_evlistener_mainwin_set_always_on_top);
    aud_hook_dissociate("mainwin show", ui_main_evlistener_mainwin_show);
    aud_hook_dissociate("equalizerwin show", ui_main_evlistener_equalizerwin_show);

    aud_hook_dissociate("playback audio error", (void *) mainwin_stop_pushed);
    aud_hook_dissociate("playback audio error", (void *) run_no_output_device_dialog);

    aud_hook_dissociate("playback seek", (HookFunction) mainwin_update_song_info);
}

void start_stop_visual (void)
{
    static char started = 0;

    if (config.player_visible && config.vis_type != VIS_OFF)
    {
        if (! started)
        {
            ui_vis_clear_data (mainwin_vis);
            ui_svis_clear_data (mainwin_svis);
            aud_hook_associate ("visualization timeout",
             ui_main_evlistener_visualization_timeout, 0);
            started = 1;
        }
    }
    else
    {
        if (started)
        {
            aud_hook_dissociate ("visualization timeout",
             ui_main_evlistener_visualization_timeout);
            ui_vis_clear_data (mainwin_vis);
            ui_svis_clear_data (mainwin_svis);
            started = 0;
        }
    }
}
